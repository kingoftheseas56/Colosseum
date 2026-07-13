#pragma once

#include "BookTorrentMagnet.h"
#include "TorrentResult.h"

#include <QUrl>
#include <QUrlQuery>

namespace ComicTorrentMagnet {

inline QString infoHash(const QString& hashOrMagnet)
{
    const QString direct = canonicalizeInfoHash(hashOrMagnet);
    if (!direct.isEmpty()) return direct;
    const QUrl url(hashOrMagnet);
    if (url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) != 0) return {};
    const QUrlQuery query(url);
    for (const QString& xt : query.allQueryItemValues(QStringLiteral("xt"))) {
        static const QString prefix = QStringLiteral("urn:btih:");
        if (xt.startsWith(prefix, Qt::CaseInsensitive))
            return canonicalizeInfoHash(xt.mid(prefix.size()));
    }
    return {};
}

inline QString build(const QString& selectedInfoHash, const QString& suppliedMagnet)
{
    const QString hash = canonicalizeInfoHash(selectedInfoHash);
    if (hash.isEmpty()) return {};
    if (infoHash(suppliedMagnet) == hash) return suppliedMagnet;
    return BookTorrentMagnet::buildMagnet(hash);
}

} // namespace ComicTorrentMagnet
