// ============================================================
// HTTP 响应构建器
// ============================================================
#pragma once

#include <string>
#include <map>
#include <sstream>
#include "json.hpp"

class HttpResponse {
public:
    HttpResponse() = default;

    void set_status(int code, const std::string& msg) {
        code_ = code;
        msg_  = msg;
    }

    void set_header(const std::string& key, const std::string& val) {
        headers_[key] = val;
    }

    void set_body(const std::string& body) {
        body_ = body;
    }

    void set_json(const json::Value& v) {
        body_ = v.dump();
        set_header("Content-Type", "application/json; charset=utf-8");
    }

    // 序列化为完整的 HTTP 响应字符串
    std::string serialize() const {
        std::ostringstream oss;
        // 状态行
        oss << "HTTP/1.1 " << code_ << " " << msg_ << "\r\n";

        // Headers
        auto hdrs = headers_;
        hdrs["Content-Length"] = std::to_string(body_.size());
        hdrs["Connection"] = "keep-alive";
        hdrs["Server"] = "FoodDelivery/1.0";
        hdrs["Access-Control-Allow-Origin"] = "*";
        hdrs["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
        hdrs["Access-Control-Allow-Headers"] = "Content-Type";

        for (auto& [k, v] : hdrs) {
            oss << k << ": " << v << "\r\n";
        }
        oss << "\r\n";
        oss << body_;
        return oss.str();
    }

    // SSE 事件序列化 (不分 Content-Length, 用于流式传输)
    std::string serialize_sse_event(const std::string& event, const std::string& data) const {
        std::ostringstream oss;
        oss << "event: " << event << "\n";
        oss << "data: " << data << "\n\n";
        return oss.str();
    }

    int code() const { return code_; }

    // 标记为 SSE 响应 (用于 send_response 识别)
    void mark_sse(bool v = true) { is_sse_ = v; }
    bool is_sse() const { return is_sse_; }

private:
    int code_ = 200;
    std::string msg_ = "OK";
    bool is_sse_ = false;
    std::map<std::string, std::string> headers_;
    std::string body_;
};

// 快捷响应函数
inline HttpResponse make_ok(const json::Value& data) {
    HttpResponse resp;
    resp.set_status(200, "OK");
    resp.set_json(data);
    return resp;
}

inline HttpResponse make_error(int code, const std::string& msg) {
    HttpResponse resp;
    resp.set_status(code, msg);
    json::Value err;
    err["error"] = msg;
    resp.set_json(err);
    return resp;
}

inline HttpResponse make_404() {
    return make_error(404, "Not Found");
}

inline HttpResponse make_400(const std::string& msg) {
    return make_error(400, msg);
}
