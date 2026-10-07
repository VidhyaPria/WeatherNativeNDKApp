#include "json/Json.h"

#include <cmath>
#include <cstdlib>

namespace breezy {
namespace json {
namespace {

/** Appends `codepoint` to `out` as UTF-8. */
void AppendUtf8(uint32_t codepoint, std::string& out) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

const Value& Value::Null() {
    static const Value kNull;
    return kNull;
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object) {
        return Null();
    }
    auto it = object_.find(key);
    return it == object_.end() ? Null() : it->second;
}

const Value& Value::operator[](size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) {
        return Null();
    }
    return array_[index];
}

size_t Value::Size() const {
    if (type_ == Type::Array) {
        return array_.size();
    }
    if (type_ == Type::Object) {
        return object_.size();
    }
    return 0;
}

double Value::AsDouble(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

int Value::AsInt(int fallback) const {
    return type_ == Type::Number ? static_cast<int>(std::llround(number_)) : fallback;
}

long long Value::AsLong(long long fallback) const {
    return type_ == Type::Number ? static_cast<long long>(std::llround(number_)) : fallback;
}

bool Value::AsBool(bool fallback) const {
    if (type_ == Type::Bool) {
        return boolean_;
    }
    // Open-Meteo writes is_day as 1/0 rather than true/false.
    if (type_ == Type::Number) {
        return number_ != 0.0;
    }
    return fallback;
}

std::string Value::AsString(const std::string& fallback) const {
    return type_ == Type::String ? text_ : fallback;
}

/** Recursive-descent parser over the whole text; `ok_` latches false at the first problem. */
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Value ParseDocument() {
        SkipWhitespace();
        Value root = ParseValue(0);
        if (!ok_) {
            return Value();
        }
        SkipWhitespace();
        return root;
    }

private:
    /** Open-Meteo responses nest about four deep; the cap only guards against hostile input. */
    static constexpr int kMaxDepth = 64;

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    void SkipWhitespace() {
        while (!AtEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Expect(char c) {
        if (Peek() != c) {
            ok_ = false;
            return false;
        }
        ++pos_;
        return true;
    }

    bool Literal(const char* word) {
        for (const char* p = word; *p != '\0'; ++p) {
            if (AtEnd() || text_[pos_] != *p) {
                ok_ = false;
                return false;
            }
            ++pos_;
        }
        return true;
    }

    Value ParseValue(int depth) {
        if (!ok_ || depth > kMaxDepth) {
            ok_ = false;
            return Value();
        }
        SkipWhitespace();
        switch (Peek()) {
            case '{':
                return ParseObject(depth);
            case '[':
                return ParseArray(depth);
            case '"':
                return ParseStringValue();
            case 't': {
                Value v;
                if (Literal("true")) {
                    v.type_ = Value::Type::Bool;
                    v.boolean_ = true;
                }
                return v;
            }
            case 'f': {
                Value v;
                if (Literal("false")) {
                    v.type_ = Value::Type::Bool;
                    v.boolean_ = false;
                }
                return v;
            }
            case 'n':
                Literal("null");
                return Value();
            default:
                return ParseNumber();
        }
    }

    Value ParseObject(int depth) {
        Value value;
        value.type_ = Value::Type::Object;
        if (!Expect('{')) {
            return Value();
        }
        SkipWhitespace();
        if (Peek() == '}') {
            ++pos_;
            return value;
        }
        while (ok_) {
            SkipWhitespace();
            const Value key = ParseStringValue();
            if (!ok_) {
                break;
            }
            SkipWhitespace();
            if (!Expect(':')) {
                break;
            }
            Value member = ParseValue(depth + 1);
            if (!ok_) {
                break;
            }
            value.object_.emplace(key.text_, std::move(member));
            SkipWhitespace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (Expect('}')) {
                return value;
            }
            break;
        }
        return Value();
    }

    Value ParseArray(int depth) {
        Value value;
        value.type_ = Value::Type::Array;
        if (!Expect('[')) {
            return Value();
        }
        SkipWhitespace();
        if (Peek() == ']') {
            ++pos_;
            return value;
        }
        while (ok_) {
            Value element = ParseValue(depth + 1);
            if (!ok_) {
                break;
            }
            value.array_.push_back(std::move(element));
            SkipWhitespace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (Expect(']')) {
                return value;
            }
            break;
        }
        return Value();
    }

    Value ParseStringValue() {
        Value value;
        if (!Expect('"')) {
            return Value();
        }
        value.type_ = Value::Type::String;
        while (!AtEnd()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return value;
            }
            if (c != '\\') {
                value.text_.push_back(c);
                continue;
            }
            if (AtEnd()) {
                break;
            }
            const char escape = text_[pos_++];
            switch (escape) {
                case '"': value.text_.push_back('"'); break;
                case '\\': value.text_.push_back('\\'); break;
                case '/': value.text_.push_back('/'); break;
                case 'b': value.text_.push_back('\b'); break;
                case 'f': value.text_.push_back('\f'); break;
                case 'n': value.text_.push_back('\n'); break;
                case 'r': value.text_.push_back('\r'); break;
                case 't': value.text_.push_back('\t'); break;
                case 'u': {
                    uint32_t codepoint = 0;
                    if (!ReadHex4(codepoint)) {
                        return Value();
                    }
                    // A leading surrogate is only meaningful with its trailing half.
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        const size_t mark = pos_;
                        pos_ += 2;
                        uint32_t low = 0;
                        if (ReadHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            ok_ = true;
                            pos_ = mark;
                        }
                    }
                    AppendUtf8(codepoint, value.text_);
                    break;
                }
                default:
                    ok_ = false;
                    return Value();
            }
        }
        ok_ = false;
        return Value();
    }

    bool ReadHex4(uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            ok_ = false;
            return false;
        }
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = HexDigit(text_[pos_ + i]);
            if (digit < 0) {
                ok_ = false;
                return false;
            }
            result = (result << 4) | static_cast<uint32_t>(digit);
        }
        pos_ += 4;
        out = result;
        return true;
    }

    Value ParseNumber() {
        const size_t start = pos_;
        if (Peek() == '-' || Peek() == '+') {
            ++pos_;
        }
        while (!AtEnd()) {
            const char c = text_[pos_];
            const bool part = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                              c == '+' || c == '-';
            if (!part) {
                break;
            }
            ++pos_;
        }
        if (pos_ == start) {
            ok_ = false;
            return Value();
        }
        const std::string digits = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double parsed = std::strtod(digits.c_str(), &end);
        if (end == digits.c_str()) {
            ok_ = false;
            return Value();
        }
        Value value;
        value.type_ = Value::Type::Number;
        value.number_ = parsed;
        return value;
    }

    const std::string& text_;
    size_t pos_ = 0;
    bool ok_ = true;
};

Value Parse(const std::string& text) {
    Parser parser(text);
    return parser.ParseDocument();
}

}  // namespace json
}  // namespace breezy
