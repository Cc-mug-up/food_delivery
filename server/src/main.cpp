// ============================================================
// 外卖服务器 — 入口
// ============================================================
#include "server.h"
#include "config.h"
#include "logger.h"
#include <csignal>
#include <iostream>

// 全局服务器指针 (用于信号处理)
static Server* g_server = nullptr;

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        LOG_INFO("Received shutdown signal, stopping server...");
        if (g_server) g_server->stop();
        return TRUE;
    }
    return FALSE;
}

void setup_signal_handlers() {
    SetConsoleCtrlHandler(console_handler, TRUE);
}
#else
void signal_handler(int sig) {
    LOG_INFO("Received signal " + std::to_string(sig) + ", stopping server...");
    if (g_server) g_server->stop();
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
}
#endif

int main(int argc, char* argv[]) {
    std::string config_path = "config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    // 初始化日志
    Logger::instance().set_level(LogLevel::DEBUG);
    LOG_INFO("============================================");
    LOG_INFO("  外卖配送系统 - Food Delivery Server");
    LOG_INFO("============================================");

    // 加载配置
    if (!Config::instance().load(config_path)) {
        LOG_WARN("Failed to load config from " + config_path + ", using defaults");
    } else {
        LOG_INFO("Config loaded from " + config_path);
    }

    auto& cfg = Config::instance();

    // 设置日志级别
    std::string log_level = cfg.logging.level;
    if (log_level == "debug") Logger::instance().set_level(LogLevel::DEBUG);
    else if (log_level == "info")  Logger::instance().set_level(LogLevel::INFO);
    else if (log_level == "warn")  Logger::instance().set_level(LogLevel::WARN);
    else if (log_level == "error") Logger::instance().set_level(LogLevel::ERROR);

    // 创建并初始化服务器
    Server server;
    g_server = &server;

    if (!server.init(cfg)) {
        LOG_ERROR("Server initialization failed");
        return 1;
    }

    setup_signal_handlers();

    // 启动服务器 (阻塞直到 stop)
    server.run();

    LOG_INFO("Server stopped. Goodbye!");
    return 0;
}
