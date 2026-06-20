// ============================================================
// 测试: 并发压力测试 — 模拟多客户端同时请求
// 使用多线程发送 HTTP 请求
// ============================================================
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define closesocket close
#endif

const char* SERVER_HOST = "127.0.0.1";
const int   SERVER_PORT = 8080;
const int   TOTAL_REQUESTS = 1000;
const int   CONCURRENT_THREADS = 50;

struct Result {
    int  status_code  = 0;
    long latency_us   = 0;
    bool success      = false;
    int  thread_id    = 0;
};

// 发送简单 HTTP 请求并计时
Result send_request(int thread_id, int req_id) {
    Result result;
    result.thread_id = thread_id;

    auto start = std::chrono::high_resolution_clock::now();

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return result;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_HOST);

    // 设置超时
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return result;
    }

    // 发送 HTTP 请求
    std::string request =
        "GET /api/menu HTTP/1.1\r\n"
        "Host: 127.0.0.1:8080\r\n"
        "Connection: close\r\n"
        "\r\n";
    send(sock, request.c_str(), request.size(), 0);

    // 读取响应
    char buf[4096];
    std::string response;
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
        if (response.find("\r\n\r\n") != std::string::npos && n < (int)sizeof(buf) - 1) break;
    }

    closesocket(sock);

    auto end = std::chrono::high_resolution_clock::now();
    result.latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 解析状态码
    if (response.find("HTTP/1.1 200") != std::string::npos) {
        result.status_code = 200;
        result.success = true;
    } else {
        size_t pos = response.find("HTTP/1.1 ");
        if (pos != std::string::npos) {
            result.status_code = std::stoi(response.substr(pos + 9, 3));
        }
    }

    if ((req_id + 1) % 100 == 0) {
        std::cout << "  [" << thread_id << "] completed " << (req_id + 1) << " requests" << std::endl;
    }

    return result;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    std::cout << "=== Test: Concurrency Stress Test ===" << std::endl;
    std::cout << "Target: " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
    std::cout << "Total requests: " << TOTAL_REQUESTS << std::endl;
    std::cout << "Concurrent threads: " << CONCURRENT_THREADS << std::endl;
    std::cout << "======================================" << std::endl;

    std::atomic<int> completed{0};
    std::atomic<int> success{0};
    std::vector<Result> all_results(TOTAL_REQUESTS);
    std::vector<std::thread> threads;

    auto test_start = std::chrono::high_resolution_clock::now();

    // 每个线程发送 TOTAL_REQUESTS / CONCURRENT_THREADS 个请求
    int per_thread = TOTAL_REQUESTS / CONCURRENT_THREADS;

    for (int t = 0; t < CONCURRENT_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < per_thread; ++i) {
                int idx = t * per_thread + i;
                auto result = send_request(t, i);
                all_results[idx] = result;
                completed++;
                if (result.success) success++;
            }
        });
    }

    for (auto& t : threads) t.join();

    auto test_end = std::chrono::high_resolution_clock::now();
    auto total_time_us = std::chrono::duration_cast<std::chrono::microseconds>(test_end - test_start).count();

    // ---- 统计 ----
    std::cout << "\n========== Results ==========" << std::endl;
    std::cout << "Total completed: " << completed.load() << std::endl;
    std::cout << "Successful (200): " << success.load() << std::endl;
    std::cout << "Failed: " << (completed.load() - success.load()) << std::endl;
    std::cout << "Success rate: " << (success.load() * 100.0 / completed.load()) << "%" << std::endl;
    std::cout << "Total time: " << (total_time_us / 1000.0) << " ms" << std::endl;

    double qps = completed.load() * 1e6 / total_time_us;
    std::cout << "Throughput: " << qps << " req/s (QPS)" << std::endl;

    // 延迟统计 (只看成功的)
    std::vector<long> latencies;
    for (auto& r : all_results) {
        if (r.success && r.latency_us > 0) {
            latencies.push_back(r.latency_us);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        size_t n = latencies.size();
        long p50  = latencies[n * 50 / 100];
        long p90  = latencies[n * 90 / 100];
        long p99  = latencies[n * 99 / 100];
        long avg  = 0;
        for (long l : latencies) avg += l;
        avg /= n;

        std::cout << "\n--- Latency (microseconds) ---" << std::endl;
        std::cout << "Avg: " << avg << " us (" << (avg / 1000.0) << " ms)" << std::endl;
        std::cout << "P50: " << p50 << " us (" << (p50 / 1000.0) << " ms)" << std::endl;
        std::cout << "P90: " << p90 << " us (" << (p90 / 1000.0) << " ms)" << std::endl;
        std::cout << "P99: " << p99 << " us (" << (p99 / 1000.0) << " ms)" << std::endl;
        std::cout << "Min: " << latencies[0] << " us" << std::endl;
        std::cout << "Max: " << latencies[n - 1] << " us (" << (latencies[n-1] / 1000.0) << " ms)" << std::endl;
    }

    std::cout << "\n=== Concurrency test complete! ===" << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif

    return success.load() > 0 ? 0 : 1;
}
