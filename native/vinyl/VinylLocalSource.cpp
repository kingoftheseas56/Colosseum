#include "VinylLocalSource.h"
#include "engine/VaultIdentity.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <algorithm>
namespace {
QString localPath(const QString &input) {
    const QUrl url(input);
    if (url.isLocalFile()) return url.toLocalFile();
    if (!url.scheme().isEmpty() && !(input.size() > 2 && input.at(1) == ':')) return {};
    return input;
}
QString fileError(const QString &path) {
    const QFileInfo file(path);
    if (!file.exists()) return QStringLiteral("This audio file no longer exists: %1").arg(path);
    if (!file.isFile()) return QStringLiteral("Choose an audio file, not a folder: %1").arg(path);
    QFile readable(path);
    if (!readable.open(QIODevice::ReadOnly)) return QStringLiteral("This audio file cannot be read: %1").arg(path);
    return {};
}
}
VinylLocalSource::VinylLocalSource(VaultIdentity *identity, QObject *parent)
    : QObject(parent), m_identity(identity) {
    connect(this, &VinylLocalSource::profileLeaving, this, [this] { cancelAll(); m_albums.clear(); m_tracks.clear(); });
    connect(this, &VinylLocalSource::shuttingDown, this, &VinylLocalSource::cancelAll);
}
QVariantMap VinylLocalSource::importFiles(const QStringList &inputs) {
    QVariantList albums; QStringList errors; QSet<QString> seen;
    static const QSet<QString> extensions = {"mp3","flac","wav","ogg","opus","m4a","aac","aiff","aif","wma","ape","alac","m4b"};
    for (const auto &input : inputs) {
        QString path = localPath(input);
        if (path.isEmpty()) { errors.append(QStringLiteral("Only local audio files can be imported.")); continue; }
        const QString problem = fileError(path);
        if (!problem.isEmpty()) { errors.append(problem); continue; }
        QFileInfo file(path);
        if (!extensions.contains(file.suffix().toLower())) { errors.append(QStringLiteral("Unsupported audio file type: %1").arg(file.fileName())); continue; }
        path = file.canonicalFilePath();
        if (!m_identity) { errors.append(QStringLiteral("Local file identity is unavailable.")); continue; }
        bool pendingIdentity = false;
        for (const auto &pendingValue : m_identity->pendingCeremonies()) {
            if (pendingValue.toMap().value("newPath").toString() == path) {
                pendingIdentity = true; break;
            }
        }
        if (pendingIdentity) {
            errors.append(QStringLiteral("This file needs an identity decision before import: %1").arg(path)); continue;
        }
        const auto observation = m_identity->observeFile(path, file.size(), file.lastModified().toMSecsSinceEpoch());
        if (observation.value("prompt").toBool()) {
            errors.append(QStringLiteral("This file needs an identity decision before import: %1").arg(path)); continue;
        }
        const auto trackId = observation.value("id").toString();
        if (trackId.isEmpty() || seen.contains(trackId)) continue;
        seen.insert(trackId);
        const QString title = file.completeBaseName();
        // Loose audio is an explicit single-track local release, not inferred tags.
        const QString albumId = QStringLiteral("vinyl:local-release:") + trackId;
        const QVariantMap track{{"id",trackId},{"title",title},{"artists",QStringList{QStringLiteral("Unknown artist")}},
            {"seconds",0},{"available",true},{"localPath",path},{"externalIds",QVariantMap{}}};
        albums.append(QVariantMap{{"id",albumId},{"browseId",albumId},{"title",title},
            {"artists",QStringList{QStringLiteral("Unknown artist")}},{"year",QStringLiteral("Local file")},
            {"cover",QString()},{"origin",QStringLiteral("local")},{"tracks",QVariantList{track}},{"externalIds",QVariantMap{}}});
    }
    return {{"albums",albums},{"errors",errors}};
}
void VinylLocalSource::setAlbums(const QVariantList &entries) {
    m_albums.clear(); m_tracks.clear();
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        auto album = entry.value("payload").toMap().value("album").toMap();
        if (album.isEmpty()) album = entry;
        if (album.value("origin").toString() != "local" || album.value("id").toString().isEmpty()) continue;
        m_albums.insert(album.value("id").toString(),album);
        for (const auto &trackValue : album.value("tracks").toList()) {
            const auto track = trackValue.toMap();
            if (!track.value("id").toString().isEmpty()) m_tracks.insert(track.value("id").toString(),track);
        }
    }
}
int VinylLocalSource::request(const QString &lane,const QString &operation,const QString &value) {
    if ((lane != "browse" && lane != "play") || (operation != "search" && operation != "album" && operation != "resolve")) return -1;
    const int id = ++m_serial; m_pending.insert(lane,id);
    // Queued even when local, so callers assign ID before completion arrives.
    QTimer::singleShot(0,this,[this,lane,operation,value,id] {
        if (m_pending.value(lane,-1) != id) return;
        m_pending.remove(lane);
        QString error; const auto data = read(operation,value,&error);
        emit completed(lane,id,data,error);
    });
    return id;
}
void VinylLocalSource::cancel(const QString &lane) { m_pending.remove(lane); }
void VinylLocalSource::cancelAll() { m_pending.clear(); }
QVariant VinylLocalSource::read(const QString &operation,const QString &value,QString *error) const {
    if (operation == "search") {
        QVariantList results; auto ids = m_albums.keys(); std::sort(ids.begin(),ids.end());
        for (const auto &id : ids) {
            const auto album = m_albums.value(id);
            if (value.trimmed().isEmpty() || album.value("title").toString().contains(value,Qt::CaseInsensitive)
                || album.value("artists").toStringList().join(' ').contains(value,Qt::CaseInsensitive)) results.append(album);
        }
        return results;
    }
    if (operation == "album") {
        if (m_albums.contains(value)) return m_albums.value(value);
        *error = QStringLiteral("This local album is no longer in your library. Import it again."); return {};
    }
    const auto track = m_tracks.value(value);
    if (track.isEmpty()) { *error = QStringLiteral("This local track is not available. Import it again."); return {}; }
    QString path = track.value("localPath").toString();
    if (m_identity) for (const auto &aliasValue : m_identity->pathAliases()) {
        const auto alias = aliasValue.toMap();
        if (alias.value("oldPath").toString() == path) path = alias.value("newPath").toString();
    }
    *error = fileError(path); if (!error->isEmpty()) return {};
    const QFileInfo file(path);
    const auto currentId = VaultIdentity::computeId(file.canonicalFilePath(),file.size(),file.lastModified().toMSecsSinceEpoch());
    if (!m_identity || m_identity->resolve(currentId) != m_identity->resolve(value)) {
        *error = QStringLiteral("This audio file has changed. Import it again to confirm its identity."); return {};
    }
    return QVariantMap{{"trackId",value},{"url",QUrl::fromLocalFile(path).toString()},
        {"headers",QVariantMap{}},{"qualityLabel",QStringLiteral("Local audio")},{"origin",QStringLiteral("local")}};
}
