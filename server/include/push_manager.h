// ============================================================
// SSE 实时推送管理器 — 维护 SSE 客户端列表, 广播事件
// ============================================================
#pragma once

#include <vector>
#include <mutex>
#include <algorithm>
#include <string>
#include <chrono>
#include "json.hpp"
#include "logger.h"

constexpr int SSE_HEARTBEAT_SEC = 30;

class PushManager {
public:
    static PushManager& instance() {
        static PushManager mgr;
        return mgr;
    }

    // 注册 SSE 客户端
    void subscribe(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        clients_.push_back({fd, now, now});
        LOG_INFO("SSE client subscribed, fd=" + std::to_string(fd) + " total=" + std::to_string(clients_.size()));
    }

    // 取消注册
    void unsubscribe(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                           [fd](const SSEClient& c) { return c.fd == fd; }),
            clients_.end());
        LOG_INFO("SSE client unsubscribed, fd=" + std::to_string(fd) + " total=" + std::to_string(clients_.size()));
    }

    // 获取需要发送心跳的客户端列表
    std::vector<int> get_heartbeat_clients() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        std::vector<int> result;
        for (auto& c : clients_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - c.last_heartbeat).count();
            if (elapsed >= SSE_HEARTBEAT_SEC) {
                c.last_heartbeat = now;
                result.push_back(c.fd);
            }
        }
        return result;
    }

    // 广播订单创建事件
    void broadcast_new_order(const json::Value& order_data) {
        broadcast("new_order", order_data);
    }

    // 广播订单状态变更
    void broadcast_status_update(const json::Value& update_data) {
        broadcast("order_status", update_data);
    }

    // 获取所有 SSE 客户端 fd
    std::vector<int> get_all_clients() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<int> result;
        for (auto& c : clients_) result.push_back(c.fd);
        return result;
    }

    size_t client_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return clients_.size();
    }

    // 构建 SSE 事件字符串 (静态方法, 供外部使用)
    static std::string build_event(const std::string& event, const std::string& data) {
        return "event: " + event + "\ndata: " + data + "\n\n";
    }

    static std::string build_heartbeat() {
        return ": heartbeat\n\n"; // SSE comment line
    }

private:
    struct SSEClient {
        int fd;
        std::chrono::steady_clock::time_point connected_at;
        std::chrono::steady_clock::time_point last_heartbeat;
    };

    void broadcast(const std::string& event, const json::Value& data) {
        std::string payload = build_event(event, data.dump());
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<int> dead_fds;
        for (auto& c : clients_) {
            ssize_t n = send(c.fd, payload.c_str(), payload.size(), 0);
            if (n <= 0) {
                dead_fds.push_back(c.fd);
            }
        }
        // 清理断开的连接
        for (int fd : dead_fds) {
            clients_.erase(
                std::remove_if(clients_.begin(), clients_.end(),
                               [fd](const SSEClient& c) { return c.fd == fd; }),
                clients_.end());
            LOG_WARN("SSE broadcast: removed dead client fd=" + std::to_string(fd));
        }
    }

    std::vector<SSEClient> clients_;
    mutable std::mutex mutex_;
};
