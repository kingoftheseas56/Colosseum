#pragma once
// Pure helpers for the engine-direct book torrent transport (Phase 2). No Qt
// GUI, no engine, no network — unit-tested by book_torrent_magnet_harness.
#include "BookTorrentFilePicker.h"   // ManifestFile
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace BookTorrentMagnet {

// Bare-infoHash magnet + the default public trackers (bare-DHT metadata is slow;
// the Phase-1 harness proved these three resolve well-seeded torrents fast).
inline QString buildMagnet(const QString& infoHash)
{
    static const QStringList kTrackers = {
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("udp://open.demonii.com:1337/announce"),
        QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
    };
    QString m = QStringLiteral("magnet:?xt=urn:btih:") + infoHash.trimmed().toLower();
    for (const QString& tr : kTrackers)
        m += QStringLiteral("&tr=") + QString::fromUtf8(QUrl::toPercentEncoding(tr));
    return m;
}

// Engine metadataReady files (index/name/size) → the picker's ManifestFile list.
inline QList<ManifestFile> filesToManifest(const QJsonArray& files)
{
    QList<ManifestFile> mfs;
    for (const QJsonValue& v : files) {
        const QJsonObject o = v.toObject();
        mfs.push_back({ o.value(QStringLiteral("index")).toInt(),
                        o.value(QStringLiteral("name")).toString(),
                        static_cast<qint64>(o.value(QStringLiteral("size")).toDouble()) });
    }
    return mfs;
}

// Priority vector: picked file = 4 (libtorrent default), every other file = 0 (skip).
inline QVector<int> pickToPriorities(int pickedIdx, int fileCount)
{
    QVector<int> prio(fileCount, 0);
    if (pickedIdx >= 0 && pickedIdx < fileCount) prio[pickedIdx] = 4;
    return prio;
}

} // namespace BookTorrentMagnet
