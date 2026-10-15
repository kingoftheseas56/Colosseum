#pragma once

#include <QDir>
#include <QFile>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace DownloadFileOps {

struct Result {
    bool success = false;
    QString message;
};

using Remover = std::function<bool(const QString &)>;

inline Result removeFile(
    const QString &path,
    const Remover &remove = [](const QString &p) { return QFile::remove(p); })
{
    if (path.isEmpty() || !QFile::exists(path))
        return {true, QString()};
    if (!remove(path) || QFile::exists(path))
        return {false, QStringLiteral("Colosseum could not delete the local file.")};
    return {true, QString()};
}

inline Result removeTree(
    const QString &path,
    const Remover &remove = [](const QString &p) {
        return QDir(p).removeRecursively();
    })
{
    if (path.isEmpty() || !QDir(path).exists())
        return {true, QString()};
    if (!remove(path) || QDir(path).exists())
        return {false, QStringLiteral("Colosseum could not delete the local folder.")};
    return {true, QString()};
}

inline QVariantMap toMap(const Result &result)
{
    return {{QStringLiteral("success"), result.success},
            {QStringLiteral("message"), result.message}};
}

} // namespace DownloadFileOps
