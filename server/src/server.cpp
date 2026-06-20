// ============================================================
// 主服务器 — 事件循环实现
// ============================================================
#include "server.h"
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close_socket closesocket
    #define SOCK_ERRNO WSAGetLastError()
    #define SOCK_EAGAIN WSAEWOULDBLOCK
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #define close_socket close
    #define SOCK_ERRNO errno
    #define SOCK_EAGAIN EAGAIN
#endif

Server::Server() = default;

Server::~Server() {
    stop();
    if (listen_fd_ >= 0) close_socket(listen_fd_);
    delete epoll_inst_;
}

bool Server::init(const Config& cfg) {
    config_ = cfg;
    static_dir_ = cfg.server.static_dir;
    connection_timeout_ = cfg.server.connection_timeout_sec;

    // 初始化 epoll
    epoll_inst_ = epoll_create1(0);
    if (!epoll_inst_) {
        LOG_ERROR("Failed to create epoll instance");
        return false;
    }

    // 创建监听 socket
    if (!create_listen_socket(cfg.server.port)) {
        LOG_ERROR("Failed to create listen socket on port " + std::to_string(cfg.server.port));
        return false;
    }

    // 初始化线程池
    int num_threads = std::max(cfg.thread_pool.min_threads, (int)std::thread::hardware_concurrency() * 2);
    num_threads = std::min(num_threads, cfg.thread_pool.max_threads);
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);
    LOG_INFO("ThreadPool initialized with " + std::to_string(num_threads) + " threads");

    // 初始化数据库连接池
    db_pool_ = std::make_unique<DbPool>();
    if (!db_pool_->init(cfg.database.host, cfg.database.port,
                        cfg.database.user, cfg.database.password,
                        cfg.database.database,
                        cfg.database.min_connections, cfg.database.max_connections)) {
        LOG_ERROR("Failed to initialize database pool");
        return false;
    }

    // 初始化业务处理器
    order_handler_ = std::make_unique<OrderHandler>(db_pool_.get());

    // 注册路由
    router_.get("/api/menu", [this](const HttpRequest& req, const auto&) {
        return order_handler_->get_menu(req);
    });

    router_.post("/api/orders", [this](const HttpRequest& req, const auto&) {
        return order_handler_->create_order(req);
    });

    router_.get("/api/orders", [this](const HttpRequest& req, const auto&) {
        return order_handler_->list_orders(req);
    });

    router_.get("/api/orders/:id", [this](const HttpRequest& req, const auto& params) {
        return order_handler_->get_order(req, params);
    });

    router_.put("/api/orders/:id/status", [this](const HttpRequest& req, const auto& params) {
        return order_handler_->update_status(req, params);
    });

    router_.get("/api/stats", [this](const HttpRequest& req, const auto&) {
        return order_handler_->get_stats(req);
    });

    // OPTIONS (CORS preflight)
    router_.add("OPTIONS", "/api/orders", [](const HttpRequest&, const auto&) {
        HttpResponse resp;
        resp.set_status(204, "No Content");
        return resp;
    });
    router_.add("OPTIONS", "/api/orders/:id/status", [](const HttpRequest&, const auto&) {
        HttpResponse resp;
        resp.set_status(204, "No Content");
        return resp;
    });

    LOG_INFO("Server initialized on port " + std::to_string(cfg.server.port));
    return true;
}

void Server::run() {
    running_ = true;
    LOG_INFO("Server started, listening on port " + std::to_string(config_.server.port));

    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epoll_inst_, events, MAX_EVENTS, 5); // 5ms 超时 (低延迟)

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd_) {
                // 新连接
                accept_connection();
            } else {
                // 处理读写事件
                uint32_t revents = events[i].events;
                if (revents & EPOLLIN) {
                    handle_read(fd);
                }
                if (revents & EPOLLOUT) {
                    handle_write(fd);
                }
                if (revents & EPOLLERR) {
                    cleanup_connection(fd);
                }
            }
        }

        // 处理 epoll 事件后, 排空待发送响应队列 (线程安全)
        drain_pending_responses();

        // 每秒定时任务
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_tick).count() >= 1) {
            last_tick = now;
            timer_tick();
        }
    }
}

void Server::stop() {
    running_ = false;
}

// ---- 私有方法 ----

bool Server::set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

bool Server::create_listen_socket(int port) {
#ifdef _WIN32
    // WSAStartup 在 EpollInstance 构造函数中已调用
#endif

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR("Failed to create socket");
        return false;
    }

    // SO_REUSEADDR
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (!set_nonblocking(listen_fd_)) {
        LOG_ERROR("Failed to set nonblocking");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to port " + std::to_string(port));
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        LOG_ERROR("Failed to listen");
        return false;
    }

    // 注册到 epoll (边缘触发)
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    epoll_ctl(epoll_inst_, EPOLL_CTL_ADD, listen_fd_, &ev);

    LOG_INFO("Listening on 0.0.0.0:" + std::to_string(port));
    return true;
}

void Server::accept_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &addr_len);

        if (client_fd < 0) {
            int e = SOCK_ERRNO;
            if (e == SOCK_EAGAIN) break; // 没有更多连接
            LOG_WARN("accept error: " + std::to_string(e));
            break;
        }

        set_nonblocking(client_fd);

        // 连接数限制
        if (connections_.size() >= (size_t)config_.server.max_connections) {
            LOG_WARN("Max connections reached, rejecting new connection");
            close_socket(client_fd);
            continue;
        }

        auto conn = std::make_unique<Connection>(client_fd);
        int fd = client_fd;

        // 注册到 epoll (边缘触发)
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(epoll_inst_, EPOLL_CTL_ADD, fd, &ev);

        connections_[fd] = std::move(conn);
        LOG_DEBUG("New connection: fd=" + std::to_string(fd));
    }
}

void Server::handle_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }

    auto& conn = it->second;

    if (conn->state == ConnState::SSE) {
        // SSE 连接上收到数据 = 客户端关闭或心跳
        char buf[64];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            PushManager::instance().unsubscribe(fd);
            cleanup_connection(fd);
        }
        return;
    }

    // 读数据
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn->read_buffer.append(buf, n);
            conn->touch();
        } else if (n == 0) {
            cleanup_connection(fd);
            return;
        } else {
            if (SOCK_ERRNO == SOCK_EAGAIN) break; // 读完
            cleanup_connection(fd);
            return;
        }
    }

    // 解析 HTTP 请求
    conn->parser.parse(conn->read_buffer.data(), 0);
    // 重新解析整个 buffer
    conn->parser.reset();
    conn->parser.parse(conn->read_buffer.data(), conn->read_buffer.size());

    if (conn->parser.is_done()) {
        // 请求完整, 提交到线程池处理
        conn->state = ConnState::Processing;
        HttpRequest req_copy = conn->parser.request();
        int fd_copy = fd;

        thread_pool_->enqueue([this, fd_copy, req_copy]() {
            auto it = connections_.find(fd_copy);
            if (it == connections_.end()) return;

            HttpResponse resp = process_request(it->second.get(), req_copy);

            // 将响应放入队列, 由主线程安全发送
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_responses_.push({fd_copy, resp});
        });
    } else if (conn->parser.is_error()) {
        send_response(fd, make_error(400, "Bad Request"));
    }
}

void Server::drain_pending_responses() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    while (!pending_responses_.empty()) {
        auto [fd, resp] = pending_responses_.front();
        pending_responses_.pop();

        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            send_response(fd, resp);
        }
    }
}

void Server::handle_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = it->second;

    // 先写缓冲区 (所有连接都需要——SSE 的 HTTP 响应头也要先发给浏览器)
    size_t remaining = conn->write_buffer.size() - conn->write_offset;
    if (remaining > 0) {
        ssize_t n = send(fd, conn->write_buffer.data() + conn->write_offset, remaining, 0);
        if (n > 0) {
            conn->write_offset += n;
            remaining -= n;
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            cleanup_connection(fd);
            return;
        }
    }

    bool all_sent = (conn->write_offset >= conn->write_buffer.size());

    // SSE 长连接: 初始响应头发送完后, 保持连接, 后续由 PushManager 广播事件
    if (conn->state == ConnState::SSE) {
        if (all_sent) {
            epoll_rearm(epoll_inst_, fd);
        }
        return;
    }

    // 普通连接: 全部写完则清理或重置
    if (all_sent) {
        if (conn->state == ConnState::Closing) {
            cleanup_connection(fd);
        } else {
            // 重置为读取状态, 准备接收下一个请求
            conn->state = ConnState::Reading;
            conn->read_buffer.clear();
            conn->write_buffer.clear();
            conn->write_offset = 0;
            conn->parser.reset();

            epoll_event ev{};
            ev.events  = EPOLLIN | EPOLLET;
            ev.data.fd = fd;
            epoll_ctl(epoll_inst_, EPOLL_CTL_MOD, fd, &ev);
        }
    }
}

HttpResponse Server::process_request(Connection* conn, const HttpRequest& req) {
    // 检查是否是 SSE 订阅
    if (req.path == "/api/events" && req.method == "GET") {
        HttpResponse resp;
        resp.set_status(200, "OK");
        resp.set_header("Content-Type", "text/event-stream");
        resp.set_header("Cache-Control", "no-cache");
        resp.set_header("Connection", "keep-alive");
        resp.set_header("Access-Control-Allow-Origin", "*");
        resp.set_body("");
        resp.mark_sse();  // 标记为 SSE
        return resp;
    }

    // 静态文件
    if (req.method == "GET" && (req.path == "/" || req.path.find("/static/") == 0)) {
        return serve_static(req.path, static_dir_);
    }

    // 也处理 /css/* 和 /js/* 路径
    if (req.method == "GET") {
        std::string path = req.path;
        if (path.find("/css/") == 0 || path.find("/js/") == 0 ||
            (path.size() > 4 && path.compare(path.size() - 4, 4, ".ico") == 0) ||
            path.find(".html") != std::string::npos ||
            path.find(".js") != std::string::npos ||
            path.find(".css") != std::string::npos) {
            return serve_static(req.path, static_dir_);
        }
    }

    // 路由分发
    return router_.dispatch(req);
}

void Server::send_response(int fd, const HttpResponse& resp) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = it->second;

    // 检查是否是 SSE 订阅 (由 process_request 标记)
    bool is_sse = resp.is_sse();

    std::string serialized;
    if (is_sse) {
        // 发送 SSE 初始响应头
        serialized = "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/event-stream\r\n"
                     "Cache-Control: no-cache\r\n"
                     "Connection: keep-alive\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Server: FoodDelivery/1.0\r\n"
                     "\r\n";
        conn->state = ConnState::SSE;
        PushManager::instance().subscribe(fd);
        LOG_INFO("SSE connection established: fd=" + std::to_string(fd));
    } else {
        serialized = resp.serialize();
        conn->state = ConnState::Closing;
    }

    conn->write_buffer = serialized;
    conn->write_offset = 0;

    // 切换到写事件监听
    epoll_event ev{};
    ev.events  = EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epoll_inst_, EPOLL_CTL_MOD, fd, &ev);
}

void Server::cleanup_connection(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    // 如果是 SSE 连接, 取消订阅
    if (it->second->state == ConnState::SSE) {
        PushManager::instance().unsubscribe(fd);
    }

    it->second->close_conn();
    epoll_event ev{};
    epoll_ctl(epoll_inst_, EPOLL_CTL_DEL, fd, &ev);
    connections_.erase(it);
    LOG_DEBUG("Connection cleaned up: fd=" + std::to_string(fd));
}

HttpResponse Server::serve_static(const std::string& path, const std::string& static_dir) {
    // 映射路径到文件
    std::string file_path = path;
    if (file_path == "/" || file_path.empty()) {
        file_path = "/index.html";
    }

    // 移除 /static 前缀
    if (file_path.find("/static/") == 0) {
        file_path = file_path.substr(7); // "/static".length()
    }

    // 如果 file_path 以 / 开头, 去掉它避免双斜线
    if (!file_path.empty() && file_path[0] == '/') {
        file_path = file_path.substr(1);
    }
    std::string full_path = static_dir + "/" + file_path;

    // 安全检查: 规范化路径, 防止目录遍历
    while (full_path.find("/../") != std::string::npos) {
        size_t pos = full_path.find("/../");
        size_t prev = full_path.rfind('/', pos - 1);
        if (prev == std::string::npos) break;
        full_path.erase(prev, pos - prev + 3);
    }
    if (full_path.find("..") != std::string::npos) {
        return make_error(403, "Forbidden");
    }

    std::ifstream f(full_path, std::ios::binary);
    if (!f.is_open()) {
        return make_404();
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    HttpResponse resp;
    resp.set_status(200, "OK");

    // MIME 类型
    std::string ext;
    size_t dot = file_path.rfind('.');
    if (dot != std::string::npos) ext = file_path.substr(dot);

    if (ext == ".html") resp.set_header("Content-Type", "text/html; charset=utf-8");
    else if (ext == ".css")  resp.set_header("Content-Type", "text/css; charset=utf-8");
    else if (ext == ".js")   resp.set_header("Content-Type", "application/javascript; charset=utf-8");
    else if (ext == ".json") resp.set_header("Content-Type", "application/json; charset=utf-8");
    else if (ext == ".png")  resp.set_header("Content-Type", "image/png");
    else if (ext == ".jpg" || ext == ".jpeg") resp.set_header("Content-Type", "image/jpeg");
    else if (ext == ".ico")  resp.set_header("Content-Type", "image/x-icon");
    else resp.set_header("Content-Type", "application/octet-stream");

    resp.set_header("Cache-Control", "no-cache");
    resp.set_body(content);
    return resp;
}

void Server::timer_tick() {
    // 1. 清理过期连接
    std::vector<int> expired;
    for (auto& [fd, conn] : connections_) {
        if (conn->state != ConnState::SSE && conn->is_expired(connection_timeout_)) {
            expired.push_back(fd);
        }
    }
    for (int fd : expired) {
        LOG_DEBUG("Connection timeout: fd=" + std::to_string(fd));
        cleanup_connection(fd);
    }

    // 2. SSE 心跳
    auto hb_clients = PushManager::instance().get_heartbeat_clients();
    std::string hb = PushManager::build_heartbeat();
    for (int fd : hb_clients) {
        ssize_t n = send(fd, hb.c_str(), hb.size(), 0);
        if (n <= 0) {
            PushManager::instance().unsubscribe(fd);
            cleanup_connection(fd);
        }
    }
}
