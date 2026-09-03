#pragma once

#include "AndroidSecureStorageBackend.h"

class AndroidJniSecureStorageBackend final : public AndroidSecureStorageBackend {
public:
    bool isAvailable() const override;
    std::optional<QByteArray> read(const QString &key) const override;
    bool write(const QString &key, const QByteArray &value) override;
    bool remove(const QString &key) override;
    QStringList keys(const QString &prefix) const override;
};
