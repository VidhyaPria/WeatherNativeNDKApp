/*
 * A small hand-written JSON reader.
 *
 * CLAUDE.md calls for "hand-written JSON mapping" on the C++ side. The obvious alternative,
 * nlohmann/json, is a ~900 KB single header whose instantiated templates would land in the same
 * binary this project exists to weigh against the ArkTS one — so the comparison would be measuring
 * the dependency as much as the port. This covers exactly what the Open-Meteo responses need:
 * objects, arrays, numbers, strings, booleans, and nulls.
 *
 * Reading is total: a missing key or a wrong type yields the null Value and then the caller's
 * fallback, mirroring how the ArkTS mapper collapses `undefined` to `null`.
 */
#ifndef BREEZY_JSON_JSON_H
#define BREEZY_JSON_JSON_H

#include <map>
#include <string>
#include <vector>

namespace breezy {
namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }

    /** Object member. Absent members — and non-objects — read as null. */
    const Value& operator[](const std::string& key) const;
    /** Array element. Out-of-range indices — and non-arrays — read as null. */
    const Value& operator[](size_t index) const;

    /** Element count of an array or object; 0 for anything else. */
    size_t Size() const;

    double AsDouble(double fallback) const;
    int AsInt(int fallback) const;
    long long AsLong(long long fallback) const;
    bool AsBool(bool fallback) const;
    std::string AsString(const std::string& fallback) const;

    /** True when the value is a number — used where "absent" and "zero" must not be confused. */
    bool IsNumber() const { return type_ == Type::Number; }

    static const Value& Null();

private:
    friend class Parser;

    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string text_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

/** Returns the null Value when the text is not valid JSON. */
Value Parse(const std::string& text);

}  // namespace json
}  // namespace breezy

#endif  // BREEZY_JSON_JSON_H
