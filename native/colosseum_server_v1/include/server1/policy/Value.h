#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace server1::policy {

class Value final {
public:
    enum class Kind {
        Missing,
        Null,
        Boolean,
        Number,
        String,
        Bytes,
        Array,
        Object,
    };

    using Bytes = std::vector<std::uint8_t>;
    using Array = std::vector<Value>;
    using ObjectEntry = std::pair<std::string, Value>;
    using Object = std::vector<ObjectEntry>;

    Value();

    static Value missing();
    static Value null();
    static Value boolean(bool value);
    static Value number(double value);
    static Value string(std::string value);
    static Value bytes(Bytes value);
    static Value array(Array value);
    static Value object(Object value);

    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] bool isMissing() const noexcept;
    [[nodiscard]] bool isNull() const noexcept;

    [[nodiscard]] bool asBoolean() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] const std::string &asString() const;
    [[nodiscard]] const Bytes &asBytes() const;
    [[nodiscard]] const Array &asArray() const;
    [[nodiscard]] const Object &asObject() const;

    [[nodiscard]] const Value *find(std::string_view key) const noexcept;

private:
    using Storage = std::variant<std::monostate,
                                 std::nullptr_t,
                                 bool,
                                 double,
                                 std::string,
                                 Bytes,
                                 Array,
                                 Object>;

    explicit Value(Storage storage);

    Storage storage_;
};

class Settings final {
public:
    Settings(std::string serverVersion, Value base);

    void extend(const Value &extension);
    [[nodiscard]] bool set(std::string_view key, Value value);

    [[nodiscard]] const std::string &serverVersion() const noexcept;
    [[nodiscard]] const Value *find(std::string_view key) const noexcept;
    [[nodiscard]] Value value() const;

private:
    std::string serverVersion_;
    Value serverVersionValue_;
    Value value_;
};

[[nodiscard]] double jsNumber(const Value &value);
[[nodiscard]] double jsParseInt(const Value &value, int radix = 10);
[[nodiscard]] bool jsTruthy(const Value &value) noexcept;
[[nodiscard]] std::optional<double> finiteNumber(const Value &value);
[[nodiscard]] std::int32_t jsToInt32(const Value &value);
[[nodiscard]] std::uint32_t jsToUint32(const Value &value);

[[nodiscard]] std::vector<std::uint8_t> stringToBytes(std::string_view value);
[[nodiscard]] std::string bytesToString(const std::vector<std::uint8_t> &value);

[[nodiscard]] bool hasOwnProperty(const Value &object, std::string_view key) noexcept;
[[nodiscard]] Value shallowExtend(const Value &base, const Value &extension);
[[nodiscard]] std::string jsonStringify(const Value &value);

[[nodiscard]] bool isPositiveInteger(const Value &value);
[[nodiscard]] bool isSafePositiveInteger(const Value &value);
[[nodiscard]] std::optional<std::size_t> checkedSize(const Value &value, std::size_t maximum);

} // namespace server1::policy
