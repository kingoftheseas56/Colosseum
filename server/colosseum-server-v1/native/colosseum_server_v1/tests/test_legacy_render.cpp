#include "server1/media/LegacyHlsService.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().isEmpty()) return 2;
    int failed = 0;
    for (const auto& value : document.array()) {
        const auto row = value.toObject();
        const auto response = server1::media::LegacyHlsService::render(
            row["action"].toString(), row["instance"].toObject(), row["input"].toObject(),
            row["subs"].toArray(), row["hasSplitter"].toBool(true));
        QJsonObject actual{{"status", response.status}, {"headers", response.headers},
            {"body", QString::fromUtf8(response.body)}};
        if (!response.program.isEmpty()) {
            actual["program"] = response.program;
            actual["args"] = QJsonArray::fromStringList(response.arguments);
        }
        if (actual == row["expected"].toObject()) continue;
        ++failed;
        std::cerr << "FAIL " << row["name"].toString().toStdString()
                  << "\nexpected " << QJsonDocument(row["expected"].toObject()).toJson(QJsonDocument::Compact).constData()
                  << "\nactual   " << QJsonDocument(actual).toJson(QJsonDocument::Compact).constData() << '\n';
    }
    std::cout << (failed ? "FAIL " : "PASS ") << document.array().size() << " pinned legacy render cases; "
              << failed << " mismatches\n";
    return failed ? 1 : 0;
}
