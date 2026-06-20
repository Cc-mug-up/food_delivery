// ============================================================
// 测试: 订单 CRUD + 数据校验 + 状态流转
// ============================================================
#include <iostream>
#include <cassert>
#include <string>
#include "../server/include/order_handler.h"
#include "../server/include/db_pool.h"
#include "../server/include/logger.h"

int main() {
    Logger::instance().set_level(LogLevel::WARN);
    std::cout << "=== Test: Order CRUD & Validation ===" << std::endl;

    // 初始化连接池
    DbPool pool;
    if (!pool.init("127.0.0.1", 3306, "root", "134121", "food_delivery", 2, 10)) {
        std::cerr << "FAIL: Cannot connect to database" << std::endl;
        std::cerr << "Run: mysql -u root < ../server/sql/init.sql" << std::endl;
        return 1;
    }

    OrderHandler handler(&pool);

    // ---- 测试 1: 获取菜单 ----
    std::cout << "[Test 1] GET /api/menu..." << std::endl;
    {
        HttpRequest req;
        req.method = "GET";
        auto resp = handler.get_menu(req);
        assert(resp.code() == 200);
        std::cout << "  PASS (code=200)" << std::endl;
    }

    // ---- 测试 2: 创建订单 (正常) ----
    std::cout << "[Test 2] POST /api/orders (valid)..." << std::endl;
    {
        HttpRequest req;
        req.method = "POST";
        req.body = R"({
            "customer_name": "测试用户",
            "phone": "13800138000",
            "address": "测试宿舍楼101",
            "remark": "不要辣",
            "items": [
                {"menu_item_id": 1, "quantity": 2},
                {"menu_item_id": 7, "quantity": 1}
            ]
        })";
        auto resp = handler.create_order(req);
        assert(resp.code() == 200);
        std::cout << "  PASS (order created successfully)" << std::endl;
    }

    // ---- 测试 3: 创建订单 (手机号无效) ----
    std::cout << "[Test 3] POST /api/orders (invalid phone)..." << std::endl;
    {
        HttpRequest req;
        req.method = "POST";
        req.body = R"({
            "customer_name": "测试",
            "phone": "12345",
            "address": "地址",
            "items": [{"menu_item_id": 1, "quantity": 1}]
        })";
        auto resp = handler.create_order(req);
        assert(resp.code() == 400);
        std::cout << "  PASS (rejected with 400)" << std::endl;
    }

    // ---- 测试 4: 创建订单 (菜单项不存在) ----
    std::cout << "[Test 4] POST /api/orders (invalid menu item)..." << std::endl;
    {
        HttpRequest req;
        req.method = "POST";
        req.body = R"({
            "customer_name": "测试",
            "phone": "13900139000",
            "address": "地址",
            "items": [{"menu_item_id": 99999, "quantity": 1}]
        })";
        auto resp = handler.create_order(req);
        assert(resp.code() == 400);
        std::cout << "  PASS (rejected with 400)" << std::endl;
    }

    // ---- 测试 5: 订单列表 ----
    std::cout << "[Test 5] GET /api/orders..." << std::endl;
    {
        HttpRequest req;
        req.method = "GET";
        auto resp = handler.list_orders(req);
        assert(resp.code() == 200);
        std::cout << "  PASS (code=200)" << std::endl;
    }

    // ---- 测试 6: 获取统计 ----
    std::cout << "[Test 6] GET /api/stats..." << std::endl;
    {
        HttpRequest req;
        req.method = "GET";
        auto resp = handler.get_stats(req);
        assert(resp.code() == 200);
        std::cout << "  PASS (code=200)" << std::endl;
    }

    // ---- 测试 7: 状态流转合法性 ----
    std::cout << "[Test 7] Status transition validation..." << std::endl;
    {
        // 合法流转
        assert(OrderHandler::validate_status_transition("pending", "confirmed") == true);
        assert(OrderHandler::validate_status_transition("pending", "cancelled") == true);
        assert(OrderHandler::validate_status_transition("confirmed", "preparing") == true);
        assert(OrderHandler::validate_status_transition("preparing", "delivering") == true);
        assert(OrderHandler::validate_status_transition("delivering", "delivered") == true);
        // 非法流转
        assert(OrderHandler::validate_status_transition("pending", "delivered") == false);
        assert(OrderHandler::validate_status_transition("delivered", "cancelled") == false);
        assert(OrderHandler::validate_status_transition("cancelled", "confirmed") == false);
        std::cout << "  PASS" << std::endl;
    }

    // ---- 测试 8: 手机号校验 ----
    std::cout << "[Test 8] Phone number validation..." << std::endl;
    {
        assert(OrderHandler::validate_phone("13800138000") == true);
        assert(OrderHandler::validate_phone("15912345678") == true);
        assert(OrderHandler::validate_phone("18888888888") == true);
        assert(OrderHandler::validate_phone("12345678901") == false); // 非1开头
        assert(OrderHandler::validate_phone("1380013800") == false);  // 太短
        assert(OrderHandler::validate_phone("138001380000") == false); // 太长
        assert(OrderHandler::validate_phone("abcdefghijk") == false); // 非数字
        std::cout << "  PASS" << std::endl;
    }

    std::cout << "\n=== All Order tests passed! ===" << std::endl;
    return 0;
}
