#include "server1/http/GuessFileIndex.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using server1::policy::Value;
Value fromJson(const QJsonValue &value)
{
    if (value.isUndefined()) return Value::missing();
    if (value.isNull()) return Value::null();
    if (value.isBool()) return Value::boolean(value.toBool());
    if (value.isDouble()) return Value::number(value.toDouble());
    if (value.isString()) return Value::string(value.toString().toStdString());
    if (value.isArray()) {
        Value::Array result;
        for (const auto &item : value.toArray()) result.push_back(fromJson(item));
        return Value::array(std::move(result));
    }
    const auto object = value.toObject();
    if (object.size() == 1 && object.contains("$number")) {
        const auto text = object["$number"].toString();
        if (text == "NaN") return Value::number(std::numeric_limits<double>::quiet_NaN());
        if (text == "Infinity") return Value::number(std::numeric_limits<double>::infinity());
        if (text == "-Infinity") return Value::number(-std::numeric_limits<double>::infinity());
    }
    if (object.size() == 1 && object.value("$undefined").toBool()) return Value::missing();
    Value::Object result;
    for (auto it = object.begin(); it != object.end(); ++it)
        result.emplace_back(it.key().toStdString(), fromJson(it.value()));
    return Value::object(std::move(result));
}

std::string evaluate(const QJsonObject &input)
{
    try {
        Value result;
        if (input.value("mode").toString() == "parse")
            result = server1::http::parseVideoName(input.value("path").toString().toStdString());
        else result = server1::http::guessFileIndex(fromJson(input.value("files")), fromJson(input.value("seriesInfo")));
        return "{\"ok\":" + server1::policy::jsonStringify(result) + '}';
    } catch (const std::invalid_argument &error) {
        if (input.value("errorDetails").toBool())
            return "{\"error\":\"TypeError\",\"message\":"
                + server1::policy::jsonStringify(Value::string(error.what())) + '}';
        return "{\"error\":\"TypeError\"}";
    }
}

int selfTest()
{
    // Golden responses observed from the hash-locked M304/M663 bundle. The
    // differential runner also verifies the complete source and module hashes.
    const std::vector<std::pair<const char *, const char *>> cases = {
        {R"({})", R"({"ok":-1})"},
        {R"({"files":[],"seriesInfo":{}})", R"({"ok":-1})"},
        {R"({"files":[null],"seriesInfo":{}})", R"({"error":"TypeError"})"},
        {R"({"files":[{}],"seriesInfo":false})", R"({"ok":-1})"},
        {R"({"files":[{"path":4}],"seriesInfo":{}})", R"({"error":"TypeError"})"},
        {R"({"files":[{"path":"amkv"}],"seriesInfo":{}})", R"({"ok":0})"},
        {R"({"files":[{"path":"mkv"}],"seriesInfo":{}})", R"({"ok":-1})"},
        {R"({"files":[{"path":"movie.mkv\n"}],"seriesInfo":{}})", R"({"ok":-1})"},
        {R"({"files":[{"path":"a\nb.mkv"}],"seriesInfo":{}})", R"({"ok":0})"},
        {R"({"files":[{"path":"a.mkv","length":"2"},{"path":"b.mkv","length":"10"}],"seriesInfo":{}})", R"({"ok":0})"},
        {R"({"files":[{"path":"a.mkv","length":2},{"path":"b.mkv","length":"10"}],"seriesInfo":{}})", R"({"ok":1})"},
        {R"({"files":[{"path":"a.mkv"},{"path":"b.mkv","length":100}],"seriesInfo":{}})", R"({"ok":0})"},
        {R"({"files":[{"path":"a.mkv","length":5},{"path":"b.mkv","length":5}],"seriesInfo":{}})", R"({"ok":0})"},
        {R"({"files":[{"path":"Name.S01E02.mkv","length":1},{"path":"b.mkv","length":100}],"seriesInfo":{"season":1,"episode":2}})", R"({"ok":0})"},
        {R"({"files":[{"path":"Name.S01E02.mkv","length":1},{"path":"b.mkv","length":100}],"seriesInfo":{"season":"1","episode":2}})", R"({"ok":1})"},
        {R"({"files":[{"path":"S01E02.mkv","length":1},{"path":"b.mkv","length":100}],"seriesInfo":{"season":1,"episode":2}})", R"({"ok":1})"},
        {R"({"mode":"parse","path":"Name/S02E03/Movie.S01E02.mkv"})", R"({"ok":{"season":2,"episode":[3],"name":"movie","type":"series","tag":[]}})"},
        {R"({"mode":"parse","path":"Movie.2012.S01E02.mkv"})", R"({"ok":{"season":1,"episode":[2],"name":"movie 2012","type":"series","aired":2012,"tag":[]}})"},
        {R"({"mode":"parse","path":"Movie.2012.01.02.S01E02.mkv"})", R"({"ok":{"aired":"2012-01-02","name":"movie 2012 01 02","type":"series","tag":[]}})"},
        {R"({"mode":"parse","path":"sample.Movie.S01E02.mkv"})", R"({"ok":{"season":1,"episode":[2],"name":"sample movie","type":"series-sample","tag":["sample"]}})"},
        {R"({"mode":"parse","path":"Movie.S01E02E03.mkv"})", R"({"ok":{"season":1,"episode":[2,3],"name":"movie","type":"series","tag":[]}})"},
        {R"({"mode":"parse","path":"Name/Season 1/Episode 2.mkv"})", R"({"ok":{"season":1,"episode":[2],"name":"name","type":"series","tag":[]}})"},
        {R"({"mode":"parse","path":"Name.2012.103e.mkv"})", R"({"ok":{"episode":[3],"season":1,"name":"name","type":"series","aired":2012,"tag":[]}})"},
        {R"({"mode":"parse","path":"Name.103.2012.mkv"})", R"({"ok":{"year":2012,"name":"name","type":"movie","tag":[]}})"},
    };
    for (const auto &[input, expected] : cases) {
        const auto actual = evaluate(QJsonDocument::fromJson(input).object());
        if (QJsonDocument::fromJson(QByteArray::fromStdString(actual)) != QJsonDocument::fromJson(expected)) {
            std::cerr << "Case: " << input << "\nExpected: " << expected << "\nActual: " << actual << '\n';
            return 1;
        }
    }
    std::cout << "PASS " << cases.size() << " M304/M663 golden cases\n";
    return 0;
}
}

int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
        std::string line;
        while (std::getline(std::cin, line))
            std::cout << evaluate(QJsonDocument::fromJson(QByteArray::fromStdString(line)).object()) << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
