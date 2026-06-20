// ============================================================
// 配置管理 — 读取 config.json
// ============================================================
#pragma once

#include "json.hpp"
#include <string>
#include <fstream>
#include <sstream>

struct ServerConfig {
    int    port                  = 8080;
    int    max_connections       = 10000;
    int    connection_timeout_sec = 30;
    std::string static_dir       = "../frontend";
};

struct ThreadPoolConfig {
    int min_threads = 4;
    int max_threads = 16;
};

struct DatabaseConfig {
    std::string host     = "127.0.0.1";
    int         port     = 3306;
    std::string user     = "root";
    std::string password = "";
    std::string database = "food_delivery";
    int         min_connections      = 2;
    int         max_connections      = 20;
    int         connect_timeout_sec  = 5;
};

struct LoggingConfig {
    std::string level   = "info";
    std::string log_dir = "./logs";
    int max_file_size_mb = 10;
};

class Config {
public:
    static Config& instance() {
        static Config cfg;
        return cfg;
    }

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        auto root = json::parse(ss.str());

        auto& srv = root["server"];
        server.port                   = srv["port"].as_int();
        server.max_connections        = srv["max_connections"].as_int();
        server.connection_timeout_sec = srv["connection_timeout_sec"].as_int();
        if (srv.contains("static_dir")) server.static_dir = srv["static_dir"].as_string();

        auto& tp = root["thread_pool"];
        thread_pool.min_threads = tp["min_threads"].as_int();
        thread_pool.max_threads = tp["max_threads"].as_int();

        auto& db = root["database"];
        database.host               = db["host"].as_string();
        database.port               = db["port"].as_int();
        database.user               = db["user"].as_string();
        if (db.contains("password")) database.password = db["password"].as_string();
        database.database           = db["database"].as_string();
        database.min_connections    = db["min_connections"].as_int();
        database.max_connections    = db["max_connections"].as_int();
        database.connect_timeout_sec = db["connect_timeout_sec"].as_int();

        auto& lg = root["logging"];
        logging.level            = lg["level"].as_string();
        logging.log_dir          = lg["log_dir"].as_string();
        logging.max_file_size_mb = lg["max_file_size_mb"].as_int();

        return true;
    }

    ServerConfig     server;
    ThreadPoolConfig thread_pool;
    DatabaseConfig   database;
    LoggingConfig    logging;
};
