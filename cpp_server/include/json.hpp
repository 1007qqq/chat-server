#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace chat {

class Json {
public:
    using array = std::vector<Json>;
    using object = std::map<std::string, Json>;
    using value_type = std::variant<std::nullptr_t, bool, double, std::string, array, object>;

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool value) : value_(value) {}
    Json(int value) : value_(static_cast<double>(value)) {}
    Json(long long value) : value_(static_cast<double>(value)) {}
    Json(double value) : value_(value) {}
    Json(const char *value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(array value) : value_(std::move(value)) {}
    Json(object value) : value_(std::move(value)) {}

    static Json parse(const std::string &input) {
        Parser parser(input);
        Json value = parser.parse_value();
        parser.skip_ws();
        if (!parser.eof()) {
            throw std::runtime_error("Unexpected JSON trailing characters");
        }
        return value;
    }

    static object obj(std::initializer_list<std::pair<const std::string, Json>> values) {
        return object(values);
    }

    static array arr(std::initializer_list<Json> values) {
        return array(values);
    }

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<array>(value_); }
    bool is_object() const { return std::holds_alternative<object>(value_); }

    bool as_bool(bool fallback = false) const {
        return is_bool() ? std::get<bool>(value_) : fallback;
    }

    double as_number(double fallback = 0) const {
        return is_number() ? std::get<double>(value_) : fallback;
    }

    int as_int(int fallback = 0) const {
        return static_cast<int>(as_number(fallback));
    }

    std::string as_string(const std::string &fallback = "") const {
        return is_string() ? std::get<std::string>(value_) : fallback;
    }

    const array &as_array() const {
        if (!is_array()) {
            static const array empty;
            return empty;
        }
        return std::get<array>(value_);
    }

    const object &as_object() const {
        if (!is_object()) {
            static const object empty;
            return empty;
        }
        return std::get<object>(value_);
    }

    array &as_array_mut() {
        if (!is_array()) value_ = array{};
        return std::get<array>(value_);
    }

    object &as_object_mut() {
        if (!is_object()) value_ = object{};
        return std::get<object>(value_);
    }

    bool contains(const std::string &key) const {
        if (!is_object()) return false;
        return std::get<object>(value_).count(key) > 0;
    }

    const Json &operator[](const std::string &key) const {
        if (!is_object()) {
            static const Json empty;
            return empty;
        }
        const auto &obj_value = std::get<object>(value_);
        auto it = obj_value.find(key);
        if (it == obj_value.end()) {
            static const Json empty;
            return empty;
        }
        return it->second;
    }

    Json &operator[](const std::string &key) {
        return as_object_mut()[key];
    }

    std::string dump() const {
        std::ostringstream out;
        dump_to(out);
        return out.str();
    }

private:
    class Parser {
    public:
        explicit Parser(const std::string &input) : input_(input) {}

        bool eof() const { return pos_ >= input_.size(); }

        void skip_ws() {
            while (!eof() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }

        Json parse_value() {
            skip_ws();
            if (eof()) throw std::runtime_error("Unexpected JSON end");
            char ch = input_[pos_];
            if (ch == 'n') return parse_literal("null", Json(nullptr));
            if (ch == 't') return parse_literal("true", Json(true));
            if (ch == 'f') return parse_literal("false", Json(false));
            if (ch == '"') return Json(parse_string());
            if (ch == '[') return Json(parse_array());
            if (ch == '{') return Json(parse_object());
            return Json(parse_number());
        }

    private:
        Json parse_literal(const std::string &literal, Json value) {
            if (input_.compare(pos_, literal.size(), literal) != 0) {
                throw std::runtime_error("Invalid JSON literal");
            }
            pos_ += literal.size();
            return value;
        }

        std::string parse_string() {
            if (input_[pos_] != '"') throw std::runtime_error("Expected string");
            ++pos_;
            std::string out;
            while (!eof()) {
                char ch = input_[pos_++];
                if (ch == '"') return out;
                if (ch != '\\') {
                    out.push_back(ch);
                    continue;
                }
                if (eof()) throw std::runtime_error("Invalid JSON escape");
                char esc = input_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        out += parse_unicode_escape();
                        break;
                    default:
                        throw std::runtime_error("Unsupported JSON escape");
                }
            }
            throw std::runtime_error("Unterminated JSON string");
        }

        std::string parse_unicode_escape() {
            if (pos_ + 4 > input_.size()) throw std::runtime_error("Invalid unicode escape");
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
                char ch = input_[pos_++];
                code <<= 4;
                if (ch >= '0' && ch <= '9') code += ch - '0';
                else if (ch >= 'a' && ch <= 'f') code += 10 + ch - 'a';
                else if (ch >= 'A' && ch <= 'F') code += 10 + ch - 'A';
                else throw std::runtime_error("Invalid unicode escape");
            }
            std::string out;
            if (code <= 0x7F) {
                out.push_back(static_cast<char>(code));
            } else if (code <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            return out;
        }

        double parse_number() {
            size_t start = pos_;
            if (input_[pos_] == '-') ++pos_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (!eof() && input_[pos_] == '.') {
                ++pos_;
                while (!eof() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            if (!eof() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
                ++pos_;
                if (!eof() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
                while (!eof() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            char *end = nullptr;
            double value = std::strtod(input_.c_str() + start, &end);
            if (end != input_.c_str() + pos_) throw std::runtime_error("Invalid JSON number");
            return value;
        }

        array parse_array() {
            ++pos_;
            array values;
            skip_ws();
            if (!eof() && input_[pos_] == ']') {
                ++pos_;
                return values;
            }
            while (true) {
                values.push_back(parse_value());
                skip_ws();
                if (eof()) throw std::runtime_error("Unterminated JSON array");
                if (input_[pos_] == ']') {
                    ++pos_;
                    return values;
                }
                if (input_[pos_] != ',') throw std::runtime_error("Expected comma in array");
                ++pos_;
            }
        }

        object parse_object() {
            ++pos_;
            object values;
            skip_ws();
            if (!eof() && input_[pos_] == '}') {
                ++pos_;
                return values;
            }
            while (true) {
                skip_ws();
                std::string key = parse_string();
                skip_ws();
                if (eof() || input_[pos_] != ':') throw std::runtime_error("Expected object colon");
                ++pos_;
                values[key] = parse_value();
                skip_ws();
                if (eof()) throw std::runtime_error("Unterminated JSON object");
                if (input_[pos_] == '}') {
                    ++pos_;
                    return values;
                }
                if (input_[pos_] != ',') throw std::runtime_error("Expected comma in object");
                ++pos_;
            }
        }

        const std::string &input_;
        size_t pos_ = 0;
    };

    static std::string escape(const std::string &input) {
        std::ostringstream out;
        for (unsigned char ch : input) {
            switch (ch) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (ch < 0x20) {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                    } else {
                        out << static_cast<char>(ch);
                    }
            }
        }
        return out.str();
    }

    void dump_to(std::ostringstream &out) const {
        if (is_null()) {
            out << "null";
        } else if (is_bool()) {
            out << (std::get<bool>(value_) ? "true" : "false");
        } else if (is_number()) {
            double value = std::get<double>(value_);
            if (value == static_cast<long long>(value)) {
                out << static_cast<long long>(value);
            } else {
                out << value;
            }
        } else if (is_string()) {
            out << '"' << escape(std::get<std::string>(value_)) << '"';
        } else if (is_array()) {
            out << '[';
            bool first = true;
            for (const auto &item : std::get<array>(value_)) {
                if (!first) out << ',';
                first = false;
                item.dump_to(out);
            }
            out << ']';
        } else {
            out << '{';
            bool first = true;
            for (const auto &[key, value] : std::get<object>(value_)) {
                if (!first) out << ',';
                first = false;
                out << '"' << escape(key) << "\":";
                value.dump_to(out);
            }
            out << '}';
        }
    }

    value_type value_;
};

} // namespace chat
