#pragma once

#include "PorticoTrendTypes.h"

#include <QString>

class PorticoTrendCache
{
public:
    struct Result {
        bool found = false;
        bool stale = false;
        PorticoTrend::Shelf shelf;
        QString error;
    };

    explicit PorticoTrendCache(QString directory = {});

    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    bool save(const PorticoTrend::Shelf &shelf, QString *error = nullptr) const;
    Result load(const QString &sourceId, qint64 maxAgeSeconds,
                bool allowExpired) const;
    bool remove(const QString &sourceId) const;
    bool clear() const;

private:
    QString filePath(const QString &sourceId) const;
    QString m_directory;
};
