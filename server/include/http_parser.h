// ============================================================
// HTTP/1.1 请求解析器
// 解析: 请求行 (method, path, query, version), 请求头, 请求体
// ============================================================
#pragma once

#include <string>
#include <map>
#include <sstream>
#include <cstring>

// strcasecmp 兼容 Windows
#ifdef _WIN32
    #define strcasecmp _stricmp
#endif

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query_string;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;

    // 获取 query 参数值
    std::string query(const std::string& key) const {
        size_t pos = 0;
        while (pos < query_string.size()) {
            size_t eq = query_string.find('=', pos);
            size_t amp = query_string.find('&', pos);
            if (amp == std::string::npos) amp = query_string.size();
            if (eq != std::string::npos && eq < amp) {
                std::string k = query_string.substr(pos, eq - pos);
                std::string v = query_string.substr(eq + 1, amp - eq - 1);
                if (k == key) return url_decode(v);
            }
            pos = amp + 1;
        }
        return "";
    }

    // 获取 header 值 (大小写不敏感)
    std::string header(const std::string& key) const {
        for (auto& [k, v] : headers) {
            if (strcasecmp(k.c_str(), key.c_str()) == 0) return v;
        }
        return "";
    }

    static std::string url_decode(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                int hi = hex_val(s[i + 1]);
                int lo = hex_val(s[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    result += (char)((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            } else if (s[i] == '+') {
                result += ' ';
                continue;
            }
            result += s[i];
        }
        return result;
    }

private:
    static int hex_val(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }
};

class HttpParser {
public:
    enum class State { RequestLine, Headers, Body, Done, Error };

    HttpParser() = default;

    // 喂入数据, 返回已消费的字节数
    size_t parse(const char* data, size_t len) {
        buffer_.append(data, len);
        size_t consumed = 0;

        while (state_ != State::Done && state_ != State::Error && !buffer_.empty()) {
            if (state_ == State::RequestLine) {
                size_t crlf = buffer_.find("\r\n");
                if (crlf == std::string::npos) break;
                bool ok = parse_request_line(buffer_.substr(0, crlf));
                buffer_.erase(0, crlf + 2);
                consumed += crlf + 2;
                if (!ok) { state_ = State::Error; break; }
                state_ = State::Headers;
            }
            else if (state_ == State::Headers) {
                size_t crlf = buffer_.find("\r\n");
                if (crlf == std::string::npos) break;
                if (crlf == 0) {
                    // 空行 = header 结束
                    buffer_.erase(0, 2);
                    consumed += 2;
                    // 检查 Content-Length
                    std::string cl = request_.header("Content-Length");
                    if (!cl.empty()) {
                        expected_body_len_ = std::stoul(cl);
                        state_ = State::Body;
                    } else {
                        state_ = State::Done;
                    }
                } else {
                    parse_header_line(buffer_.substr(0, crlf));
                    buffer_.erase(0, crlf + 2);
                    consumed += crlf + 2;
                }
            }
            else if (state_ == State::Body) {
                if (buffer_.size() < expected_body_len_) break;
                request_.body = buffer_.substr(0, expected_body_len_);
                buffer_.erase(0, expected_body_len_);
                consumed += expected_body_len_;
                state_ = State::Done;
            }
        }
        return consumed;
    }

    bool is_done()  const { return state_ == State::Done; }
    bool is_error() const { return state_ == State::Error; }

    HttpRequest& request() { return request_; }
    const HttpRequest& request() const { return request_; }

    void reset() {
        request_ = HttpRequest{};
        buffer_.clear();
        expected_body_len_ = 0;
        state_ = State::RequestLine;
    }

private:
    bool parse_request_line(const std::string& line) {
        std::istringstream iss(line);
        if (!(iss >> request_.method)) return false;
        std::string full_path;
        if (!(iss >> full_path)) return false;
        if (!(iss >> request_.version)) return false;

        // 分离 path 和 query string
        size_t q = full_path.find('?');
        if (q != std::string::npos) {
            request_.path         = full_path.substr(0, q);
            request_.query_string = full_path.substr(q + 1);
        } else {
            request_.path = full_path;
        }
        return true;
    }

    void parse_header_line(const std::string& line) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) return;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // trim val
        size_t start = val.find_first_not_of(" \t");
        size_t end   = val.find_last_not_of(" \t");
        if (start == std::string::npos) val = "";
        else val = val.substr(start, end - start + 1);
        request_.headers[key] = val;
    }

    HttpRequest request_;
    std::string buffer_;
    size_t      expected_body_len_ = 0;
    State       state_ = State::RequestLine;
};
