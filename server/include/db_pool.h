// ============================================================
// MySQL 连接池 — RAII 连接管理
// ============================================================
#pragma once

#include <mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <thread>
#include "logger.h"

class DbPool {
public:
    struct Connection {
        MYSQL* mysql = nullptr;
        bool   in_use = true;

        ~Connection() {
            if (mysql) {
                mysql_close(mysql);
                mysql = nullptr;
            }
        }
        Connection() = default;
        Connection(Connection&&) = delete;
        Connection(const Connection&) = delete;
    };

    using ConnPtr = std::shared_ptr<Connection>;

    DbPool() = default;

    ~DbPool() {
        close();
    }

    bool init(const std::string& host, int port,
              const std::string& user, const std::string& password,
              const std::string& database,
              int min_conn, int max_conn) {
        max_connections_ = max_conn;

        for (int i = 0; i < min_conn; ++i) {
            auto conn = create_connection(host, port, user, password, database);
            if (!conn) {
                LOG_ERROR("DbPool: failed to create initial connection " + std::to_string(i + 1));
                return false;
            }
            conn->in_use = false;
            pool_.push(conn);
        }
        LOG_INFO("DbPool: initialized with " + std::to_string(min_conn) + " connections");
        return true;
    }

    // 获取一个连接 (超时返回 nullptr)
    ConnPtr acquire(int timeout_sec = 5) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!cv_.wait_for(lock, std::chrono::seconds(timeout_sec),
                          [this] { return !pool_.empty() || total_conns_ < max_connections_; })) {
            LOG_WARN("DbPool: acquire timeout");
            return nullptr;
        }

        if (!pool_.empty()) {
            auto conn = pool_.front();
            pool_.pop();
            conn->in_use = true;
            // Ping 检查连接是否存活
            if (mysql_ping(conn->mysql) != 0) {
                LOG_WARN("DbPool: dead connection detected, reconnecting...");
                mysql_close(conn->mysql);
                conn->mysql = nullptr;
                // 递归重试
                total_conns_--;
                lock.unlock();
                return acquire(timeout_sec);
            }
            return conn;
        }

        // 池空, 但可创建新连接
        // 不能在这里创建 (需要连接参数), 所以等待
        return nullptr;
    }

    // 归还连接
    void release(ConnPtr conn) {
        if (!conn) return;
        conn->in_use = false;
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(conn);
        cv_.notify_one();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            pool_.pop(); // shared_ptr 析构时自动关闭
        }
        total_conns_ = 0;
    }

    size_t available() const { return pool_.size(); }

private:
    ConnPtr create_connection(const std::string& host, int port,
                               const std::string& user, const std::string& password,
                               const std::string& database) {
        auto conn = std::make_shared<Connection>();
        conn->mysql = mysql_init(nullptr);
        if (!conn->mysql) {
            LOG_ERROR("DbPool: mysql_init failed");
            return nullptr;
        }

        // 设置连接选项
        unsigned int timeout = 5;
        mysql_options(conn->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        bool reconnect = true;
        mysql_options(conn->mysql, MYSQL_OPT_RECONNECT, &reconnect);
        mysql_options(conn->mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        if (!mysql_real_connect(conn->mysql, host.c_str(), user.c_str(),
                                password.empty() ? nullptr : password.c_str(),
                                database.c_str(), port, nullptr, 0)) {
            LOG_ERROR(std::string("DbPool: mysql_real_connect failed: ") + mysql_error(conn->mysql));
            return nullptr;
        }

        total_conns_++;
        return conn;
    }

    std::queue<ConnPtr> pool_;
    std::mutex          mutex_;
    std::condition_variable cv_;
    int max_connections_ = 20;
    int total_conns_     = 0;
};
