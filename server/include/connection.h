// ============================================================
// 连接管理 — 每个客户端连接的状态
// ============================================================
#pragma once

#include <string>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #define close_socket closesocket
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define close_socket close
#endif

#include "http_parser.h"
#include "http_response.h"

enum class ConnState {
    Reading,      // 等待读取请求
    Processing,   // 已提交线程池处理
    Writing,      // 等待写入响应
    SSE,          // SSE 长连接, 保持打开
    Closing       // 即将关闭
};

struct Connection {
    int         fd;
    std::string read_buffer;
    std::string write_buffer;
    size_t      write_offset = 0;
    ConnState   state = ConnState::Reading;
    HttpParser  parser;
    HttpResponse response;
    std::chrono::steady_clock::time_point last_active;
    int         sse_fail_count = 0; // SSE 写入失败计数

    Connection(int sockfd) : fd(sockfd) {
        touch();
    }

    void touch() {
        last_active = std::chrono::steady_clock::now();
    }

    bool is_expired(int timeout_sec) const {
        auto elapsed = std::chrono::steady_clock::now() - last_active;
        return std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > timeout_sec;
    }

    void close_conn() {
        if (fd >= 0) {
            shutdown(fd, 2);
            close_socket(fd);
            fd = -1;
        }
    }
};
