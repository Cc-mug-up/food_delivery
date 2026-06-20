// ============================================================
// 性能基准测试 — 逐 API 测量延迟和 QPS
// 生成 JSON 格式测试报告
// ============================================================
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <fstream>

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

struct BenchResult {
    std::string name;
    long        avg_latency_us;
    long        p50_us;
    long        p99_us;
    long        min_us;
    long        max_us;
    double      qps;
    int         success;
    int         failed;
};

// 发送一次 HTTP 请求, 返回延迟 (微秒) 和是否成功
std::pair<long, bool> request_once(const std::string& method, const std::string& path,
                                     const std::string& body = "") {
    auto start = std::chrono::high_resolution_clock::now();

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return {0, false};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_HOST);

#ifdef _WIN32
    DWORD timeout = 10000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv = {10, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return {0, false};
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << SERVER_HOST << ":" << SERVER_PORT << "\r\n"
        << "Connection: close\r\n";
    if (!body.empty()) {
        req << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    if (!body.empty()) req << body;

    std::string req_str = req.str();
    send(sock, req_str.c_str(), req_str.size(), 0);

    char buf[4096];
    std::string response;
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    closesocket(sock);

    auto end = std::chrono::high_resolution_clock::now();
    long latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    bool success = response.find("HTTP/1.1 200") != std::string::npos;

    return {latency, success};
}

BenchResult benchmark(const std::string& name, const std::string& method,
                      const std::string& path, int count, const std::string& body = "") {
    std::vector<long> latencies;
    int success = 0, failed = 0;

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; ++i) {
        auto [lat, ok] = request_once(method, path, body);
        if (ok) {
            latencies.push_back(lat);
            success++;
        } else {
            failed++;
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(bench_end - bench_start).count();

    BenchResult br;
    br.name = name;
    br.success = success;
    br.failed  = failed;

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        size_t n = latencies.size();
        br.min_us = latencies[0];
        br.max_us = latencies[n - 1];
        br.p50_us = latencies[n * 50 / 100];
        br.p99_us = latencies[n * 99 / 100];
        long sum = 0;
        for (long l : latencies) sum += l;
        br.avg_latency_us = sum / n;
    }
    br.qps = count * 1e6 / total_us;

    return br;
}

void print_result(const BenchResult& br) {
    std::cout << "\n--- " << br.name << " ---" << std::endl;
    std::cout << "  Success/Failed: " << br.success << "/" << br.failed << std::endl;
    std::cout << "  QPS: " << std::fixed << std::setprecision(1) << br.qps << " req/s" << std::endl;
    std::cout << "  Latency (us):" << std::endl;
    std::cout << "    Avg: " << br.avg_latency_us << " (" << (br.avg_latency_us/1000.0) << " ms)" << std::endl;
    std::cout << "    P50: " << br.p50_us << " (" << (br.p50_us/1000.0) << " ms)" << std::endl;
    std::cout << "    P99: " << br.p99_us << " (" << (br.p99_us/1000.0) << " ms)" << std::endl;
    std::cout << "    Min: " << br.min_us << " us" << std::endl;
    std::cout << "    Max: " << br.max_us << " (" << (br.max_us/1000.0) << " ms)" << std::endl;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    std::cout << "====================================================" << std::endl;
    std::cout << "          Food Delivery - Performance Benchmark     " << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Server: " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
    std::cout << "====================================================" << std::endl;

    int N = 200; // 每个 API 测试次数

    auto results = {
        benchmark("GET /api/menu",    "GET",  "/api/menu",    N),
        benchmark("GET /api/orders",  "GET",  "/api/orders",  N),
        benchmark("GET /api/stats",   "GET",  "/api/stats",   N),
        benchmark("POST /api/orders", "POST", "/api/orders",  N / 4,  // POST 少一些
            R"({"customer_name":"基准测试","phone":"13800138000","address":"测试地址","items":[{"menu_item_id":1,"quantity":1}]})"),
    };

    // 汇总表格
    std::cout << "\n====================================================" << std::endl;
    std::cout << "              SUMMARY TABLE" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::left
              << std::setw(24) << "API"
              << std::setw(10) << "QPS"
              << std::setw(12) << "Avg(ms)"
              << std::setw(12) << "P50(ms)"
              << std::setw(12) << "P99(ms)"
              << std::setw(10) << "Success" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (auto& br : results) {
        print_result(br);
        std::cout << std::left
                  << std::setw(24) << br.name
                  << std::setw(10) << std::fixed << std::setprecision(1) << br.qps
                  << std::setw(12) << std::setprecision(2) << (br.avg_latency_us / 1000.0)
                  << std::setw(12) << (br.p50_us / 1000.0)
                  << std::setw(12) << (br.p99_us / 1000.0)
                  << std::setw(10) << br.success << std::endl;
    }

    // 输出 JSON 报告
    std::ofstream report("benchmark_report.json");
    report << "{\n  \"timestamp\": \"" << []() {
        auto t = std::time(nullptr);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }() << "\",\n  \"results\": [\n";
    int idx = 0;
    for (auto& br : results) {
        report << "    {\n"
               << "      \"api\": \"" << br.name << "\",\n"
               << "      \"qps\": " << br.qps << ",\n"
               << "      \"avg_us\": " << br.avg_latency_us << ",\n"
               << "      \"p50_us\": " << br.p50_us << ",\n"
               << "      \"p99_us\": " << br.p99_us << ",\n"
               << "      \"success\": " << br.success << ",\n"
               << "      \"failed\": " << br.failed << "\n"
               << "    }";
        if (++idx < (int)results.size()) report << ",";
        report << "\n";
    }
    report << "  ]\n}\n";
    report.close();
    std::cout << "\nReport saved to benchmark_report.json" << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
