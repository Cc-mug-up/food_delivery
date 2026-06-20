// ============================================================
// 订单业务处理实现
// ============================================================
#include "order_handler.h"
#include "push_manager.h"
#include "logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>

std::mutex OrderHandler::order_no_mutex_;
int OrderHandler::order_seq_ = 0;

// ---- 工具函数 ----

std::string OrderHandler::generate_order_no() {
    std::lock_guard<std::mutex> lock(order_no_mutex_);
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto tm  = *std::localtime(&t);
    order_seq_ = (order_seq_ + 1) % 10000;
    std::ostringstream oss;
    oss << "OD"
        << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900)
        << std::setw(2) << (tm.tm_mon + 1)
        << std::setw(2) << tm.tm_mday
        << std::setw(2) << tm.tm_hour
        << std::setw(2) << tm.tm_min
        << std::setw(2) << tm.tm_sec
        << std::setw(4) << order_seq_;
    return oss.str();
}

bool OrderHandler::validate_phone(const std::string& phone) {
    // 中国大陆手机号: 1[3-9]\d{9}
    static std::regex phone_re(R"(^1[3-9]\d{9}$)");
    return std::regex_match(phone, phone_re);
}

bool OrderHandler::validate_status_transition(const std::string& from, const std::string& to) {
    auto it = STATUS_TRANSITIONS.find(from);
    if (it == STATUS_TRANSITIONS.end()) return false;
    const auto& allowed = it->second;
    return std::find(allowed.begin(), allowed.end(), to) != allowed.end();
}

// ============================================================
// GET /api/menu
// ============================================================
HttpResponse OrderHandler::get_menu(const HttpRequest& /*req*/) {
    auto conn = db_pool_->acquire(3);
    if (!conn) {
        return make_error(500, "Database connection failed");
    }

    std::string category = ""; // 可从 query 获取
    std::string sql = "SELECT id, name, category, price, image, description, is_available FROM menu_items";
    if (!category.empty()) {
        sql += " WHERE category = '" + category + "'"; // 简化, 实际应预处理
    }
    sql += " ORDER BY category, id";

    if (mysql_query(conn->mysql, sql.c_str()) != 0) {
        LOG_ERROR(std::string("get_menu query failed: ") + mysql_error(conn->mysql));
        db_pool_->release(conn);
        return make_error(500, "Query failed");
    }

    MYSQL_RES* result = mysql_store_result(conn->mysql);
    if (!result) {
        db_pool_->release(conn);
        return make_error(500, "Store result failed");
    }

    json::Array items;
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(result))) {
        json::Value item;
        item["id"]           = row[0] ? std::stoll(row[0]) : 0;
        item["name"]         = row[1] ? row[1] : "";
        item["category"]     = row[2] ? row[2] : "";
        item["price"]        = row[3] ? std::stod(row[3]) : 0.0;
        item["image"]        = row[4] ? row[4] : "";
        item["description"]  = row[5] ? row[5] : "";
        item["is_available"] = row[6] ? (std::stoi(row[6]) == 1) : false;
        items.push_back(item);
    }
    mysql_free_result(result);
    db_pool_->release(conn);

    json::Value resp_data;
    resp_data["items"] = items;
    resp_data["total"] = (int64_t)items.size();
    return make_ok(resp_data);
}

// ============================================================
// POST /api/orders  创建订单
// ============================================================
HttpResponse OrderHandler::create_order(const HttpRequest& req) {
    // 解析 JSON body
    json::Value body;
    try {
        body = json::parse(req.body);
    } catch (const std::exception& e) {
        return make_400(std::string("Invalid JSON: ") + e.what());
    }

    // ---- 数据校验 ----
    std::string customer_name = body["customer_name"].as_string();
    std::string phone         = body["phone"].as_string();
    std::string address       = body["address"].as_string();

    if (customer_name.empty()) return make_400("customer_name is required");
    if (phone.empty())        return make_400("phone is required");
    if (address.empty())      return make_400("address is required");
    if (!validate_phone(phone)) return make_400("Invalid phone number format");

    auto items = body["items"].as_array();
    if (items.empty()) return make_400("At least one menu item is required");

    // 校验每个菜单项
    auto conn = db_pool_->acquire(3);
    if (!conn) return make_error(500, "Database connection failed");

    double total_price = 0.0;
    std::vector<std::pair<int64_t, int>> order_items; // menu_item_id, quantity

    for (auto& item : items) {
        int64_t menu_id = item["menu_item_id"].as_int();
        int     qty     = (int)item["quantity"].as_int();

        if (menu_id <= 0) { db_pool_->release(conn); return make_400("Invalid menu_item_id"); }
        if (qty <= 0)     { db_pool_->release(conn); return make_400("quantity must be > 0"); }

        // 查询菜单项是否存在且可用
        char sql_buf[256];
        snprintf(sql_buf, sizeof(sql_buf),
                 "SELECT name, price, is_available FROM menu_items WHERE id = %lld",
                 (long long)menu_id);
        if (mysql_query(conn->mysql, sql_buf) != 0) {
            db_pool_->release(conn);
            return make_error(500, "Query menu item failed");
        }
        MYSQL_RES* res = mysql_store_result(conn->mysql);
        if (!res || mysql_num_rows(res) == 0) {
            if (res) mysql_free_result(res);
            db_pool_->release(conn);
            return make_400("Menu item not found: " + std::to_string(menu_id));
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        int available = row[2] ? std::stoi(row[2]) : 0;
        double price  = row[1] ? std::stod(row[1]) : 0.0;
        mysql_free_result(res);

        if (!available) {
            db_pool_->release(conn);
            return make_400("Menu item not available: " + std::to_string(menu_id));
        }
        total_price += price * qty;
        order_items.push_back({menu_id, qty});
    }

    // 生成订单号
    std::string order_no = generate_order_no();

    // 插入订单
    char insert_sql[1024];
    snprintf(insert_sql, sizeof(insert_sql),
             "INSERT INTO orders (order_no, customer_name, phone, address, total_price, status, remark) "
             "VALUES ('%s', '%s', '%s', '%s', %.2f, 'pending', '%s')",
             order_no.c_str(),
             customer_name.c_str(),
             phone.c_str(),
             address.c_str(),
             total_price,
             body["remark"].as_string().c_str());

    if (mysql_query(conn->mysql, insert_sql) != 0) {
        LOG_ERROR(std::string("Insert order failed: ") + mysql_error(conn->mysql));
        db_pool_->release(conn);
        return make_error(500, "Create order failed");
    }

    int64_t order_id = mysql_insert_id(conn->mysql);

    // 插入订单明细
    for (auto& [menu_id, qty] : order_items) {
        char item_sql[512];
        snprintf(item_sql, sizeof(item_sql),
                 "INSERT INTO order_items (order_id, menu_item_id, quantity, unit_price) "
                 "SELECT %lld, %lld, %d, price FROM menu_items WHERE id = %lld",
                 (long long)order_id, (long long)menu_id, qty, (long long)menu_id);
        if (mysql_query(conn->mysql, item_sql) != 0) {
            LOG_ERROR(std::string("Insert order_items failed: ") + mysql_error(conn->mysql));
        }
    }

    // 记录状态日志
    {
        char log_sql[256];
        snprintf(log_sql, sizeof(log_sql),
                 "INSERT INTO order_status_log (order_id, from_status, to_status) "
                 "VALUES (%lld, '', 'pending')", (long long)order_id);
        mysql_query(conn->mysql, log_sql);
    }

    db_pool_->release(conn);

    // 构建响应
    json::Value resp_data;
    resp_data["order_id"]   = (int64_t)order_id;
    resp_data["order_no"]   = order_no;
    resp_data["total_price"] = total_price;
    resp_data["status"]     = "pending";

    // ---- 实时推送新订单 ----
    json::Value push_data;
    push_data["order_no"]     = order_no;
    push_data["customer_name"] = customer_name;
    push_data["total_price"]  = total_price;
    push_data["status"]       = "pending";
    push_data["created_at"]   = []() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }();

    PushManager::instance().broadcast_new_order(push_data);
    LOG_INFO("New order created: " + order_no + " broadcasted to SSE clients");

    return make_ok(resp_data);
}

// ============================================================
// GET /api/orders  订单列表 (分页)
// ============================================================
HttpResponse OrderHandler::list_orders(const HttpRequest& req) {
    int page     = std::max(1, std::stoi(req.query("page").empty() ? "1" : req.query("page")));
    int page_size = std::min(100, std::max(1, std::stoi(req.query("page_size").empty() ? "20" : req.query("page_size"))));
    std::string status = req.query("status");
    std::string phone  = req.query("phone");

    int offset = (page - 1) * page_size;

    auto conn = db_pool_->acquire(3);
    if (!conn) return make_error(500, "Database connection failed");

    // 构建查询
    std::ostringstream count_sql, data_sql;
    count_sql << "SELECT COUNT(*) FROM orders WHERE 1=1";
    data_sql  << "SELECT id, order_no, customer_name, phone, address, total_price, status, remark, created_at, updated_at "
                 "FROM orders WHERE 1=1";

    if (!status.empty()) {
        count_sql << " AND status = '" << status << "'";
        data_sql  << " AND status = '" << status << "'";
    }
    if (!phone.empty()) {
        count_sql << " AND phone = '" << phone << "'";
        data_sql  << " AND phone = '" << phone << "'";
    }
    data_sql << " ORDER BY id DESC LIMIT " << page_size << " OFFSET " << offset;

    // COUNT
    int64_t total = 0;
    if (mysql_query(conn->mysql, count_sql.str().c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->mysql);
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row) total = std::stoll(row[0]);
            mysql_free_result(r);
        }
    }

    // DATA
    if (mysql_query(conn->mysql, data_sql.str().c_str()) != 0) {
        db_pool_->release(conn);
        return make_error(500, "Query orders failed");
    }

    MYSQL_RES* result = mysql_store_result(conn->mysql);
    json::Array orders;
    if (result) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            json::Value o;
            o["id"]            = row[0] ? std::stoll(row[0]) : 0;
            o["order_no"]      = row[1] ? row[1] : "";
            o["customer_name"] = row[2] ? row[2] : "";
            o["phone"]         = row[3] ? row[3] : "";
            o["address"]       = row[4] ? row[4] : "";
            o["total_price"]   = row[5] ? std::stod(row[5]) : 0.0;
            o["status"]        = row[6] ? row[6] : "";
            o["remark"]        = row[7] ? row[7] : "";
            o["created_at"]    = row[8] ? row[8] : "";
            o["updated_at"]    = row[9] ? row[9] : "";
            orders.push_back(o);
        }
        mysql_free_result(result);
    }
    db_pool_->release(conn);

    json::Value resp_data;
    resp_data["orders"]    = orders;
    resp_data["total"]     = (int64_t)total;
    resp_data["page"]      = (int64_t)page;
    resp_data["page_size"] = (int64_t)page_size;
    return make_ok(resp_data);
}

// ============================================================
// GET /api/orders/:id
// ============================================================
HttpResponse OrderHandler::get_order(const HttpRequest& /*req*/,
                                      const std::map<std::string, std::string>& params) {
    auto it = params.find("id");
    if (it == params.end()) return make_400("Missing order id");

    int64_t order_id = std::stoll(it->second);

    auto conn = db_pool_->acquire(3);
    if (!conn) return make_error(500, "Database connection failed");

    // 查询订单
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, order_no, customer_name, phone, address, total_price, status, remark, created_at, updated_at "
             "FROM orders WHERE id = %lld", (long long)order_id);

    if (mysql_query(conn->mysql, sql) != 0) {
        db_pool_->release(conn);
        return make_error(500, "Query failed");
    }

    MYSQL_RES* res = mysql_store_result(conn->mysql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        db_pool_->release(conn);
        return make_error(404, "Order not found");
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    json::Value order;
    order["id"]            = row[0] ? std::stoll(row[0]) : 0;
    order["order_no"]      = row[1] ? row[1] : "";
    order["customer_name"] = row[2] ? row[2] : "";
    order["phone"]         = row[3] ? row[3] : "";
    order["address"]       = row[4] ? row[4] : "";
    order["total_price"]   = row[5] ? std::stod(row[5]) : 0.0;
    order["status"]        = row[6] ? row[6] : "";
    order["remark"]        = row[7] ? row[7] : "";
    order["created_at"]    = row[8] ? row[8] : "";
    order["updated_at"]    = row[9] ? row[9] : "";
    mysql_free_result(res);

    // 查询订单明细
    char item_sql[256];
    snprintf(item_sql, sizeof(item_sql),
             "SELECT oi.id, oi.menu_item_id, mi.name, oi.quantity, oi.unit_price "
             "FROM order_items oi JOIN menu_items mi ON oi.menu_item_id = mi.id "
             "WHERE oi.order_id = %lld", (long long)order_id);
    json::Array items;
    if (mysql_query(conn->mysql, item_sql) == 0) {
        MYSQL_RES* ires = mysql_store_result(conn->mysql);
        if (ires) {
            MYSQL_ROW irow;
            while ((irow = mysql_fetch_row(ires))) {
                json::Value item;
                item["id"]           = irow[0] ? std::stoll(irow[0]) : 0;
                item["menu_item_id"] = irow[1] ? std::stoll(irow[1]) : 0;
                item["name"]         = irow[2] ? irow[2] : "";
                item["quantity"]     = irow[3] ? std::stoi(irow[3]) : 0;
                item["unit_price"]   = irow[4] ? std::stod(irow[4]) : 0.0;
                items.push_back(item);
            }
            mysql_free_result(ires);
        }
    }
    order["items"] = items;

    db_pool_->release(conn);
    return make_ok(order);
}

// ============================================================
// PUT /api/orders/:id/status
// ============================================================
HttpResponse OrderHandler::update_status(const HttpRequest& req,
                                          const std::map<std::string, std::string>& params) {
    auto it = params.find("id");
    if (it == params.end()) return make_400("Missing order id");

    int64_t order_id = std::stoll(it->second);

    json::Value body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        return make_400("Invalid JSON");
    }

    std::string new_status = body["status"].as_string();
    if (new_status.empty()) return make_400("status is required");

    auto conn = db_pool_->acquire(3);
    if (!conn) return make_error(500, "Database connection failed");

    // 查询当前状态
    char q_sql[256];
    snprintf(q_sql, sizeof(q_sql),
             "SELECT order_no, status FROM orders WHERE id = %lld",
             (long long)order_id);

    std::string current_status, order_no;
    if (mysql_query(conn->mysql, q_sql) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->mysql);
        if (res && mysql_num_rows(res) > 0) {
            MYSQL_ROW row = mysql_fetch_row(res);
            order_no = row[0] ? row[0] : "";
            current_status = row[1] ? row[1] : "";
            mysql_free_result(res);
        } else {
            if (res) mysql_free_result(res);
            db_pool_->release(conn);
            return make_error(404, "Order not found");
        }
    }

    // 校验状态流转
    if (!validate_status_transition(current_status, new_status)) {
        db_pool_->release(conn);
        return make_400("Invalid status transition: " + current_status + " -> " + new_status);
    }

    // 更新状态
    char upd_sql[256];
    snprintf(upd_sql, sizeof(upd_sql),
             "UPDATE orders SET status = '%s' WHERE id = %lld",
             new_status.c_str(), (long long)order_id);
    if (mysql_query(conn->mysql, upd_sql) != 0) {
        db_pool_->release(conn);
        return make_error(500, "Update failed");
    }

    // 记录日志
    char log_sql[256];
    snprintf(log_sql, sizeof(log_sql),
             "INSERT INTO order_status_log (order_id, from_status, to_status) "
             "VALUES (%lld, '%s', '%s')",
             (long long)order_id, current_status.c_str(), new_status.c_str());
    mysql_query(conn->mysql, log_sql);

    db_pool_->release(conn);

    // 响应
    json::Value resp_data;
    resp_data["order_id"] = (int64_t)order_id;
    resp_data["order_no"] = order_no;
    resp_data["old_status"] = current_status;
    resp_data["new_status"] = new_status;

    // SSE 推送状态变更
    json::Value push_data;
    push_data["order_no"]    = order_no;
    push_data["old_status"]  = current_status;
    push_data["new_status"]  = new_status;
    push_data["order_id"]    = (int64_t)order_id;
    PushManager::instance().broadcast_status_update(push_data);
    LOG_INFO("Order status updated: " + order_no + " " + current_status + " -> " + new_status);

    return make_ok(resp_data);
}

// ============================================================
// GET /api/stats  统计数据
// ============================================================
HttpResponse OrderHandler::get_stats(const HttpRequest& /*req*/) {
    auto conn = db_pool_->acquire(3);
    if (!conn) return make_error(500, "Database connection failed");

    json::Value stats;

    // 今日订单数
    if (mysql_query(conn->mysql,
            "SELECT COUNT(*), COALESCE(SUM(total_price), 0) FROM orders "
            "WHERE DATE(created_at) = CURDATE()") == 0) {
        MYSQL_RES* r = mysql_store_result(conn->mysql);
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row) {
                stats["today_orders"] = row[0] ? std::stoll(row[0]) : 0;
                stats["today_revenue"] = row[1] ? std::stod(row[1]) : 0.0;
            }
            mysql_free_result(r);
        }
    }

    // 各状态订单数
    if (mysql_query(conn->mysql,
            "SELECT status, COUNT(*) FROM orders GROUP BY status") == 0) {
        MYSQL_RES* r = mysql_store_result(conn->mysql);
        if (r) {
            MYSQL_ROW row;
            json::Value status_counts;
            while ((row = mysql_fetch_row(r))) {
                status_counts[row[0] ? row[0] : "unknown"] = row[1] ? std::stoll(row[1]) : 0;
            }
            stats["status_counts"] = status_counts;
            mysql_free_result(r);
        }
    }

    db_pool_->release(conn);
    return make_ok(stats);
}
