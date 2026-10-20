#include "server1/policy/Value.h"

#include <stdexcept>
#include <utility>

namespace server1::policy {

Value::Value()
    : storage_(std::monostate {})
{
}

Value::Value(Storage storage)
    : storage_(std::move(storage))
{
}

Value Value::missing()
{
    return Value(Storage {std::monostate {}});
}

Value Value::null()
{
    return Value(Storage {nullptr});
}

Value Value::boolean(bool value)
{
    return Value(Storage {value});
}

Value Value::number(double value)
{
    return Value(Storage {value});
}

Value Value::string(std::string value)
{
    return Value(Storage {std::move(value)});
}

Value Value::bytes(Bytes value)
{
    return Value(Storage {std::move(value)});
}

Value Value::array(Array value)
{
    return Value(Storage {std::move(value)});
}

Value Value::object(Object value)
{
    return Value(Storage {std::move(value)});
}

Value::Kind Value::kind() const noexcept
{
    switch (storage_.index()) {
    case 0:
        return Kind::Missing;
    case 1:
        return Kind::Null;
    case 2:
        return Kind::Boolean;
    case 3:
        return Kind::Number;
    case 4:
        return Kind::String;
    case 5:
        return Kind::Bytes;
    case 6:
        return Kind::Array;
    case 7:
        return Kind::Object;
    default:
        return Kind::Missing;
    }
}

bool Value::isMissing() const noexcept
{
    return std::holds_alternative<std::monostate>(storage_);
}

bool Value::isNull() const noexcept
{
    return std::holds_alternative<std::nullptr_t>(storage_);
}

bool Value::asBoolean() const
{
    const auto *value = std::get_if<bool>(&storage_);
    if (!value)
        throw std::logic_error("Value is not a boolean");
    return *value;
}

double Value::asNumber() const
{
    const auto *value = std::get_if<double>(&storage_);
    if (!value)
        throw std::logic_error("Value is not a number");
    return *value;
}

const std::string &Value::asString() const
{
    const auto *value = std::get_if<std::string>(&storage_);
    if (!value)
        throw std::logic_error("Value is not a string");
    return *value;
}

const Value::Bytes &Value::asBytes() const
{
    const auto *value = std::get_if<Bytes>(&storage_);
    if (!value)
        throw std::logic_error("Value is not bytes");
    return *value;
}

const Value::Array &Value::asArray() const
{
    const auto *value = std::get_if<Array>(&storage_);
    if (!value)
        throw std::logic_error("Value is not an array");
    return *value;
}

const Value::Object &Value::asObject() const
{
    const auto *value = std::get_if<Object>(&storage_);
    if (!value)
        throw std::logic_error("Value is not an object");
    return *value;
}

const Value *Value::find(std::string_view key) const noexcept
{
    const auto *object = std::get_if<Object>(&storage_);
    if (!object)
        return nullptr;
    for (const auto &entry : *object) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

Settings::Settings(std::string serverVersion, Value base)
    : serverVersion_(std::move(serverVersion))
    , serverVersionValue_(Value::string(serverVersion_))
    , value_(Value::missing())
{
    Value::Object properties;
    if (base.kind() == Value::Kind::Object)
        properties = base.asObject();
    else if (!base.isMissing())
        throw std::logic_error("Settings base must be an object");

    bool hasVersion = false;
    for (auto &entry : properties) {
        if (entry.first == "serverVersion") {
            entry.second = serverVersionValue_;
            hasVersion = true;
            break;
        }
    }
    if (!hasVersion)
        properties.insert(properties.begin(), {"serverVersion", serverVersionValue_});
    value_ = Value::object(std::move(properties));
}

void Settings::extend(const Value &extension)
{
    if (extension.isMissing() || extension.isNull())
        return;
    if (extension.kind() != Value::Kind::Object)
        throw std::logic_error("Settings extension must be an object");
    for (const auto &entry : extension.asObject()) {
        const bool accepted = set(entry.first, entry.second);
        (void)accepted;
    }
}

bool Settings::set(std::string_view key, Value value)
{
    if (key == "serverVersion")
        return false;

    Value::Object properties = value_.asObject();
    for (auto &entry : properties) {
        if (entry.first == key) {
            entry.second = std::move(value);
            value_ = Value::object(std::move(properties));
            return true;
        }
    }
    properties.emplace_back(std::string(key), std::move(value));
    value_ = Value::object(std::move(properties));
    return true;
}

const std::string &Settings::serverVersion() const noexcept
{
    return serverVersion_;
}

const Value *Settings::find(std::string_view key) const noexcept
{
    return value_.find(key);
}

Value Settings::value() const
{
    return value_;
}

} // namespace server1::policy
