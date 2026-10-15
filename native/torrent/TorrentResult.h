#pragma once

#include <QString>
#include <QList>
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>

struct TorrentResult {
    QString   title;
    QString   magnetUri;
    qint64    sizeBytes  = 0;
    int       seeders    = 0;
    int       leechers   = 0;
    QString   sourceName;
    QString   sourceKey;
    QString   categoryId;
    QString   category;
    QString   infoHash;      // canonical 40-char lowercase hex BTIH; empty if indexer couldn't extract
    QDateTime publishDate;   // invalid if indexer couldn't extract
    QString   detailsUrl;    // per-indexer details page; empty if none
};

// Normalize any raw info-hash string to 40-char lowercase hex. Returns empty
// for non-hex or wrong-length input (callers treat as "unknown" and fall
// back to magnet-regex extraction at dedup time).
inline QString canonicalizeInfoHash(QString raw)
{
    raw = raw.trimmed().toLower();
    if (raw.length() != 40)
        return {};
    static const QRegularExpression hexRe("^[a-f0-9]{40}$");
    if (!hexRe.match(raw).hasMatch())
        return {};
    return raw;
}

Q_DECLARE_METATYPE(TorrentResult)
Q_DECLARE_METATYPE(QList<TorrentResult>)

// ── Magnet URI helpers ──────────────────────────────────────────────────────

inline QStringList defaultTrackers()
{
    return {
        "udp://tracker.opentrackr.org:1337/announce",
        "udp://open.stealth.si:80/announce",
        "udp://tracker.torrent.eu.org:451/announce",
        "udp://tracker.openbittorrent.com:6969/announce",
    };
}

inline QString buildMagnet(const QString& infoHash, const QString& title,
                           const QStringList& trackers = defaultTrackers())
{
    QString ih = infoHash.trimmed().toLower();
    if (ih.length() != 40)
        return {};

    static const QRegularExpression hexRe("^[a-f0-9]{40}$");
    if (!hexRe.match(ih).hasMatch())
        return {};

    QString out = QStringLiteral("magnet:?xt=urn:btih:%1&dn=%2")
                      .arg(ih, QString::fromUtf8(QUrl::toPercentEncoding(title)));

    for (const auto& tr : trackers) {
        if (!tr.trimmed().isEmpty())
            out += QStringLiteral("&tr=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(tr.trimmed())));
    }
    return out;
}

// ── Size formatting ─────────────────────────────────────────────────────────

inline QString humanSize(qint64 bytes)
{
    if (bytes < 0) return {};
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double size = static_cast<double>(bytes);
    int i = 0;
    while (size >= 1024.0 && i < 4) {
        size /= 1024.0;
        ++i;
    }
    return QString::number(size, 'f', (i == 0) ? 0 : 1) + " " + units[i];
}
