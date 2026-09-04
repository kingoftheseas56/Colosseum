#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

class AndroidSecureStorageBackend {
public:
    virtual ~AndroidSecureStorageBackend() = default;

    virtual bool isAvailable() const = 0;
    virtual std::optional<QByteArray> read(const QString &key) const = 0;
    virtual bool write(const QString &key, const QByteArray &value) = 0;
    virtual bool remove(const QString &key) = 0;
    virtual QStringList keys(const QString &prefix) const = 0;
};
