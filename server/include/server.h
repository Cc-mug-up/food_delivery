// ============================================================
// 主服务器 — Epoll 事件循环, 连接管理, 线程池调度
// ============================================================
#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <atomic>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "epoll_manager.h"
#include "connection.h"
#include "thread_pool.h"
#include "db_pool.h"
#include "router.h"
#include "order_handler.h"
#include "push_manager.h"
#include "config.h"
#include "logger.h"

class Server {
public:
    Server();
    ~Server();

    bool init(const Config& cfg);
    void run();
    void stop();

private:
    // 设置非阻塞 socket
    static bool set_nonblocking(int fd);

    // 创建监听 socket
    bool create_listen_socket(int port);

    // 接受新连接
    void accept_connection();

    // 处理读事件
    void handle_read(int fd);

    // 处理写事件
    void handle_write(int fd);

    // 处理 HTTP 请求 (在线程池中执行)
    HttpResponse process_request(Connection* conn, const HttpRequest& req);

    // 发送响应并注册写事件 (仅主线程调用)
    void send_response(int fd, const HttpResponse& resp);

    // 清理连接
    void cleanup_connection(int fd);

    // 处理待发送的响应队列 (主线程调用)
    void drain_pending_responses();

    // 静态文件服务
    static HttpResponse serve_static(const std::string& path, const std::string& static_dir);

    // 定时器: 清理过期连接 + SSE 心跳
    void timer_tick();

    // ---- 成员 ----
    EpollInstance* epoll_inst_ = nullptr;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<DbPool>     db_pool_;
    Router                      router_;
    std::unique_ptr<OrderHandler> order_handler_;

    // 待发送响应队列 (线程安全: 线程池写入, 主线程读取)
    std::queue<std::pair<int, HttpResponse>> pending_responses_;
    std::mutex pending_mutex_;

    Config config_;
    std::string static_dir_;
    int connection_timeout_ = 30;

    static constexpr int MAX_EVENTS = 1024;
};
