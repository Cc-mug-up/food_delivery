// ============================================================
// 订单业务处理 — CRUD + 状态流转 + 数据校验
// ============================================================
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "json.hpp"
#include "http_parser.h"
#include "http_response.h"
#include "db_pool.h"

class OrderHandler {
public:
    explicit OrderHandler(DbPool* pool) : db_pool_(pool) {}

    // GET /api/menu
    HttpResponse get_menu(const HttpRequest& req);

    // POST /api/orders
    HttpResponse create_order(const HttpRequest& req);

    // GET /api/orders
    HttpResponse list_orders(const HttpRequest& req);

    // GET /api/orders/:id
    HttpResponse get_order(const HttpRequest& req, const std::map<std::string, std::string>& params);

    // PUT /api/orders/:id/status
    HttpResponse update_status(const HttpRequest& req, const std::map<std::string, std::string>& params);

    // GET /api/stats
    HttpResponse get_stats(const HttpRequest& req);

    // 校验手机号 (public 供测试使用)
    static bool validate_phone(const std::string& phone);

    // 校验订单状态流转是否合法 (public 供测试使用)
    static bool validate_status_transition(const std::string& from, const std::string& to);

private:
    // 生成订单号 OD20240620153001001
    static std::string generate_order_no();

    DbPool* db_pool_;
    static std::mutex order_no_mutex_;
    static int order_seq_;
};

// 状态流转表: from -> allowed to
static const std::map<std::string, std::vector<std::string>> STATUS_TRANSITIONS = {
    {"pending",    {"confirmed", "cancelled"}},
    {"confirmed",  {"preparing", "cancelled"}},
    {"preparing",  {"delivering"}},
    {"delivering", {"delivered"}},
    {"delivered",  {}},           // 终态
    {"cancelled",  {}},           // 终态
};
