// ============================================================
// 轻量级 JSON 库 — 用于请求/响应序列化
// 支持: string, number(int/double), bool, null, object, array
// ============================================================
#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <cctype>

namespace json
{

    // 前向声明
    class Value;
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    enum class Type
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        Object,
        Array
    };

    class Value
    {
    public:
        Type type_ = Type::Null;
        bool bool_val_ = false;
        int64_t int_val_ = 0;
        double double_val_ = 0.0;
        std::string string_val_;
        Array array_val_;
        Object object_val_;

        // ---- 构造函数 ----
        Value() : type_(Type::Null) {}
        Value(std::nullptr_t) : type_(Type::Null) {}
        Value(bool v) : type_(Type::Bool), bool_val_(v) {}
        Value(int v) : type_(Type::Int), int_val_(v), double_val_(v) {}
        Value(int64_t v) : type_(Type::Int), int_val_(v), double_val_(v) {}
        Value(double v) : type_(Type::Double), double_val_(v)
        {
            if (std::floor(v) == v && std::abs(v) < 1LL << 53)
            {
                int_val_ = (int64_t)v;
            }
        }
        Value(const char *s) : type_(Type::String), string_val_(s) {}
        Value(std::string s) : type_(Type::String), string_val_(std::move(s)) {}
        Value(Array a) : type_(Type::Array), array_val_(std::move(a)) {}
        Value(Object o) : type_(Type::Object), object_val_(std::move(o)) {}

        // ---- 类型判断 ----
        bool is_null() const { return type_ == Type::Null; }
        bool is_bool() const { return type_ == Type::Bool; }
        bool is_int() const { return type_ == Type::Int; }
        bool is_double() const { return type_ == Type::Double; }
        bool is_number() const { return type_ == Type::Int || type_ == Type::Double; }
        bool is_string() const { return type_ == Type::String; }
        bool is_array() const { return type_ == Type::Array; }
        bool is_object() const { return type_ == Type::Object; }

        // ---- 取值 ----
        bool as_bool() const { return bool_val_; }
        int64_t as_int() const { return int_val_; }
        double as_double() const { return double_val_; }
        std::string as_string() const { return string_val_; }
        const Array &as_array() const { return array_val_; }
        const Object &as_object() const { return object_val_; }

        // ---- Object 操作 ----
        Value &operator[](const std::string &key)
        {
            if (type_ != Type::Object)
            {
                type_ = Type::Object;
                object_val_.clear();
            }
            return object_val_[key];
        }
        const Value &operator[](const std::string &key) const
        {
            static Value null_val;
            auto it = object_val_.find(key);
            return it != object_val_.end() ? it->second : null_val;
        }
        bool contains(const std::string &key) const
        {
            return type_ == Type::Object && object_val_.count(key) > 0;
        }
        size_t size() const
        {
            if (type_ == Type::Array)
                return array_val_.size();
            if (type_ == Type::Object)
                return object_val_.size();
            return 0;
        }

        // ---- Array 操作 ----
        Value &operator[](size_t idx)
        {
            if (idx >= array_val_.size())
                array_val_.resize(idx + 1);
            return array_val_[idx];
        }
        void push_back(const Value &v)
        {
            if (type_ != Type::Array)
            {
                type_ = Type::Array;
                array_val_.clear();
            }
            array_val_.push_back(v);
        }

        // ---- 序列化 ----
        std::string dump(int indent = -1) const
        {
            std::ostringstream oss;
            serialize(oss, indent, 0);
            return oss.str();
        }

    private:
        void serialize(std::ostringstream &oss, int indent, int depth) const
        {
            std::string pad(indent >= 0 ? depth * indent : 0, ' ');
            std::string nl(indent >= 0 ? "\n" : "");
            std::string sp(indent >= 0 ? " " : "");

            switch (type_)
            {
            case Type::Null:
                oss << "null";
                break;
            case Type::Bool:
                oss << (bool_val_ ? "true" : "false");
                break;
            case Type::Int:
                oss << int_val_;
                break;
            case Type::Double:
                oss << double_val_;
                break;
            case Type::String:
                serialize_string(oss, string_val_);
                break;
            case Type::Array:
                oss << "[" << nl;
                for (size_t i = 0; i < array_val_.size(); ++i)
                {
                    if (i > 0)
                        oss << "," << nl;
                    oss << (indent >= 0 ? std::string((depth + 1) * indent, ' ') : "");
                    array_val_[i].serialize(oss, indent, depth + 1);
                }
                oss << nl << pad << "]";
                break;
            case Type::Object:
                oss << "{" << nl;
                {
                    size_t i = 0;
                    for (auto &[k, v] : object_val_)
                    {
                        if (i++ > 0)
                            oss << "," << nl;
                        oss << (indent >= 0 ? std::string((depth + 1) * indent, ' ') : "");
                        serialize_string(oss, k);
                        oss << ":" << sp;
                        v.serialize(oss, indent, depth + 1);
                    }
                }
                oss << nl << pad << "}";
                break;
            }
        }

        static void serialize_string(std::ostringstream &oss, const std::string &s)
        {
            oss << '"';
            for (char c : s)
            {
                switch (c)
                {
                case '"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\b':
                    oss << "\\b";
                    break;
                case '\f':
                    oss << "\\f";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                default:
                    oss << c;
                }
            }
            oss << '"';
        }
    };

    // ============================================================
    // JSON 解析器
    // ============================================================
    class Parser
    {
    public:
        static Value parse(const std::string &text)
        {
            Parser p(text);
            Value v = p.parse_value();
            p.skip_ws();
            if (p.pos_ < p.text_.size())
            {
                throw std::runtime_error("JSON: unexpected trailing content at pos " + std::to_string(p.pos_));
            }
            return v;
        }

    private:
        const std::string &text_;
        size_t pos_ = 0;

        Parser(const std::string &t) : text_(t) {}

        char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
        char next() { return pos_ < text_.size() ? text_[pos_++] : '\0'; }

        void skip_ws()
        {
            while (pos_ < text_.size() && std::isspace(text_[pos_]))
                ++pos_;
        }

        void expect(char c)
        {
            skip_ws();
            if (next() != c)
                throw std::runtime_error("JSON: expected '" + std::string(1, c) + "' at pos " + std::to_string(pos_));
        }

        Value parse_value()
        {
            skip_ws();
            char c = peek();
            switch (c)
            {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return parse_string();
            case 't':
            case 'f':
                return parse_bool();
            case 'n':
                return parse_null();
            default:
                if (c == '-' || std::isdigit(c))
                    return parse_number();
                throw std::runtime_error("JSON: unexpected char '" + std::string(1, c) + "' at pos " + std::to_string(pos_));
            }
        }

        Value parse_object()
        {
            Object obj;
            expect('{');
            skip_ws();
            if (peek() == '}')
            {
                next();
                return obj;
            }
            while (true)
            {
                skip_ws();
                std::string key = parse_string_raw();
                expect(':');
                obj[key] = parse_value();
                skip_ws();
                if (peek() == '}')
                {
                    next();
                    break;
                }
                expect(',');
            }
            return obj;
        }

        Value parse_array()
        {
            Array arr;
            expect('[');
            skip_ws();
            if (peek() == ']')
            {
                next();
                return arr;
            }
            while (true)
            {
                arr.push_back(parse_value());
                skip_ws();
                if (peek() == ']')
                {
                    next();
                    break;
                }
                expect(',');
            }
            return arr;
        }

        std::string parse_string_raw()
        {
            expect('"');
            std::string s;
            while (pos_ < text_.size())
            {
                char c = next();
                if (c == '"')
                    return s;
                if (c == '\\')
                {
                    char e = next();
                    switch (e)
                    {
                    case '"':
                        s += '"';
                        break;
                    case '\\':
                        s += '\\';
                        break;
                    case '/':
                        s += '/';
                        break;
                    case 'b':
                        s += '\b';
                        break;
                    case 'f':
                        s += '\f';
                        break;
                    case 'n':
                        s += '\n';
                        break;
                    case 'r':
                        s += '\r';
                        break;
                    case 't':
                        s += '\t';
                        break;
                    case 'u':
                    {
                        // 简单处理: 跳过 \uXXXX
                        for (int i = 0; i < 4; ++i)
                            next();
                        s += '?';
                        break;
                    }
                    default:
                        s += e;
                    }
                }
                else
                {
                    s += c;
                }
            }
            throw std::runtime_error("JSON: unterminated string");
        }

        Value parse_string() { return parse_string_raw(); }

        Value parse_number()
        {
            skip_ws();
            size_t start = pos_;
            if (peek() == '-')
                next();
            while (pos_ < text_.size() && std::isdigit(peek()))
                next();
            bool is_float = false;
            if (pos_ < text_.size() && peek() == '.')
            {
                is_float = true;
                next();
            }
            while (pos_ < text_.size() && std::isdigit(peek()))
                next();
            if (pos_ < text_.size() && (peek() == 'e' || peek() == 'E'))
            {
                is_float = true;
                next();
                if (peek() == '+' || peek() == '-')
                    next();
                while (pos_ < text_.size() && std::isdigit(peek()))
                    next();
            }
            std::string num = text_.substr(start, pos_ - start);
            if (is_float)
                return std::stod(num);
            return (int64_t)std::stoll(num);
        }

        Value parse_bool()
        {
            if (text_.substr(pos_, 4) == "true")
            {
                pos_ += 4;
                return true;
            }
            if (text_.substr(pos_, 5) == "false")
            {
                pos_ += 5;
                return false;
            }
            throw std::runtime_error("JSON: expected bool at pos " + std::to_string(pos_));
        }

        Value parse_null()
        {
            if (text_.substr(pos_, 4) == "null")
            {
                pos_ += 4;
                return nullptr;
            }
            throw std::runtime_error("JSON: expected null at pos " + std::to_string(pos_));
        }
    };

    inline Value parse(const std::string &text) { return Parser::parse(text); }

} // namespace json
