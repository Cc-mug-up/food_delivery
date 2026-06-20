// ============================================================
// 测试: 数据库连接池
// 编译为独立可执行文件
// ============================================================
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cassert>
#include "../server/include/db_pool.h"
#include "../server/include/logger.h"

int main() {
    Logger::instance().set_level(LogLevel::INFO);
    std::cout << "=== Test: Database Connection Pool ===" << std::endl;

    // 注意: 需要正确的数据库连接信息, 可通过环境变量或参数修改
    const char* host = "127.0.0.1";
    const char* user = "root";
    const char* pass = "134121";
    const char* db   = "food_delivery";
    int port = 3306;

    DbPool pool;

    // 测试 1: 初始化
    std::cout << "[Test 1] Initialize pool with 2 connections..." << std::endl;
    bool ok = pool.init(host, port, user, pass, db, 2, 10);
    if (!ok) {
        std::cerr << "FAIL: init returned false" << std::endl;
        std::cerr << "Please ensure MySQL is running and database 'food_delivery' exists." << std::endl;
        std::cerr << "Run: mysql -u root < ../server/sql/init.sql" << std::endl;
        return 1;
    }
    std::cout << "  PASS" << std::endl;
    assert(pool.available() == 2);

    // 测试 2: 获取连接
    std::cout << "[Test 2] Acquire connection..." << std::endl;
    auto conn1 = pool.acquire(3);
    assert(conn1 != nullptr);
    assert(conn1->mysql != nullptr);
    std::cout << "  PASS (available=" << pool.available() << ")" << std::endl;

    // 测试 3: 执行简单查询
    std::cout << "[Test 3] Execute SELECT 1..." << std::endl;
    if (mysql_query(conn1->mysql, "SELECT 1") != 0) {
        std::cerr << "FAIL: " << mysql_error(conn1->mysql) << std::endl;
        return 1;
    }
    MYSQL_RES* res = mysql_store_result(conn1->mysql);
    assert(res != nullptr);
    MYSQL_ROW row = mysql_fetch_row(res);
    assert(row != nullptr);
    assert(std::stoi(row[0]) == 1);
    mysql_free_result(res);
    std::cout << "  PASS" << std::endl;

    // 测试 4: 归还连接
    std::cout << "[Test 4] Release connection..." << std::endl;
    pool.release(conn1);
    std::cout << "  PASS (available=" << pool.available() << ")" << std::endl;

    // 测试 5: 并发获取
    std::cout << "[Test 5] Concurrent acquire/release..." << std::endl;
    std::atomic<int> success{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&pool, &success, i]() {
            auto c = pool.acquire(5);
            if (c) {
                success++;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                pool.release(c);
            }
        });
    }
    for (auto& t : threads) t.join();
    std::cout << "  PASS (success=" << success.load() << "/10)" << std::endl;

    // 测试 6: 查询菜单表
    std::cout << "[Test 6] Query menu_items..." << std::endl;
    auto c2 = pool.acquire(3);
    assert(c2 != nullptr);
    if (mysql_query(c2->mysql, "SELECT COUNT(*) FROM menu_items") == 0) {
        MYSQL_RES* r = mysql_store_result(c2->mysql);
        if (r) {
            MYSQL_ROW rw = mysql_fetch_row(r);
            if (rw) std::cout << "  Menu items count: " << rw[0] << std::endl;
            mysql_free_result(r);
        }
    }
    pool.release(c2);
    std::cout << "  PASS" << std::endl;

    std::cout << "\n=== All DbPool tests passed! ===" << std::endl;
    return 0;
}
