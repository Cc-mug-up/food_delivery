// ============================================================
// 线程安全日志系统 (分级日志 + 控制台输出)
// ============================================================
#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <fstream>

// 避免与 Windows.h 的 ERROR 宏冲突
#ifdef ERROR
    #undef ERROR
#endif

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& instance() {
        static Logger log;
        return log;
    }

    void set_level(LogLevel lv) { level_ = lv; }
    void set_log_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    void log(LogLevel lv, const std::string& file, int line, const std::string& msg) {
        if (lv < level_) return;

        const char* labels[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << labels[(int)lv] << "] "
            << file << ":" << line << "  " << msg << "\n";

        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << oss.str() << std::flush;
        if (file_.is_open()) {
            file_ << oss.str() << std::flush;
        }
    }

private:
    Logger() = default;
    LogLevel level_ = LogLevel::DEBUG;
    std::mutex mutex_;
    std::ofstream file_;
};

#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DEBUG, __FILE__, __LINE__, msg)
#define LOG_INFO(msg)  Logger::instance().log(LogLevel::INFO,  __FILE__, __LINE__, msg)
#define LOG_WARN(msg)  Logger::instance().log(LogLevel::WARN,  __FILE__, __LINE__, msg)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERROR, __FILE__, __LINE__, msg)
