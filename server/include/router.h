// ============================================================
// URL 路由器 — 前缀匹配 + 路径参数提取
// ============================================================
#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include "http_parser.h"
#include "http_response.h"

class Router {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&, const std::map<std::string, std::string>& params)>;

    // 注册路由, 如 "/api/orders/:id"
    void add(const std::string& method, const std::string& pattern, Handler handler) {
        routes_.push_back({method, pattern, std::move(handler)});
    }

    void get(const std::string& pattern, Handler h)  { add("GET", pattern, h); }
    void post(const std::string& pattern, Handler h) { add("POST", pattern, h); }
    void put(const std::string& pattern, Handler h)  { add("PUT", pattern, h); }
    void del(const std::string& pattern, Handler h)  { add("DELETE", pattern, h); }

    // 匹配请求, 返回响应 (404 如果无匹配)
    HttpResponse dispatch(const HttpRequest& req) const {
        for (auto& route : routes_) {
            if (route.method != req.method) continue;
            auto params = match_pattern(route.pattern, req.path);
            if (params.has_value()) {
                return route.handler(req, params.value());
            }
        }
        return make_404();
    }

private:
    struct Route {
        std::string method;
        std::string pattern;
        Handler     handler;
    };

    // 匹配 /api/orders/:id 与 /api/orders/123
    // 返回 params map 或 nullopt
    static std::optional<std::map<std::string, std::string>> match_pattern(
            const std::string& pattern, const std::string& path) {
        std::map<std::string, std::string> params;
        size_t pp = 0, pi = 0;
        while (pp < pattern.size() && pi < path.size()) {
            if (pattern[pp] == ':') {
                // 提取参数名
                size_t pe = pp + 1;
                while (pe < pattern.size() && pattern[pe] != '/') ++pe;
                std::string pname = pattern.substr(pp + 1, pe - pp - 1);

                // 提取参数值 (直到 / 或结尾)
                size_t ve = pi;
                while (ve < path.size() && path[ve] != '/') ++ve;
                params[pname] = path.substr(pi, ve - pi);

                pp = pe;
                pi = ve;
            } else if (pattern[pp] == path[pi]) {
                ++pp; ++pi;
            } else {
                return std::nullopt;
            }
        }
        if (pp != pattern.size() || pi != path.size()) return std::nullopt;
        return params;
    }

    std::vector<Route> routes_;
};
