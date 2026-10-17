#include "VinylLocalSource.h"
#include "engine/VaultIdentity.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
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
VinylLocalSource::VinylLocalSource(VaultIdentity *identity, QObject *parent, int resolutionDelayMs)
    : QObject(parent), m_identity(identity), m_resolutionDelayMs(std::clamp(resolutionDelayMs,0,2000)) {
    connect(this, &VinylLocalSource::profileLeaving, this, [this] { cancelAll(); ++m_revision; m_albums.clear(); m_tracks.clear(); m_artists.clear(); });
    connect(this, &VinylLocalSource::shuttingDown, this, &VinylLocalSource::cancelAll);
}
QVariantMap VinylLocalSource::importFiles(const QStringList &inputs) {
    QVariantList albums; QStringList errors, replacedManifestPaths; QSet<QString> seen;
    static const QSet<QString> extensions = {"mp3","flac","wav","ogg","opus","m4a","aac","aiff","aif","wma","ape","alac","m4b"};
    for (const auto &input : inputs) {
        QString path = localPath(input);
        if (path.isEmpty()) { errors.append(QStringLiteral("Only local audio files can be imported.")); continue; }
        const QString problem = fileError(path);
        if (!problem.isEmpty()) { errors.append(problem); continue; }
        QFileInfo file(path);
        if (file.fileName().endsWith(".vinyl.json", Qt::CaseInsensitive)) {
            QString error;
            const auto releases = importManifest(file.canonicalFilePath(), &error);
            if (!error.isEmpty()) errors.append(error);
            else {
                replacedManifestPaths.append(file.canonicalFilePath());
                for (const auto &release : releases) {
                    const QString id = release.toMap().value("id").toString();
                    if (!seen.contains(id)) { seen.insert(id); albums.append(release); }
                }
            }
            continue;
        }
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
            {"artists",QStringList{QStringLiteral("Unknown artist")}},
            {"artistIds",QStringList{QStringLiteral("vinyl:local-artist:unknown")}},
            {"artist",QVariantMap{{"id",QStringLiteral("vinyl:local-artist:unknown")},{"name",QStringLiteral("Unknown artist")}}},
            {"year",QStringLiteral("Local file")},
            {"cover",QString()},{"origin",QStringLiteral("local")},{"tracks",QVariantList{track}},{"externalIds",QVariantMap{}}});
    }
    return {{"albums",albums},{"errors",errors},{"replacedManifestPaths",replacedManifestPaths}};
}
QVariantList VinylLocalSource::importManifest(const QString &path, QString *error) {
    auto fail = [&](const QString &message) -> QVariantList {
        *error = QStringLiteral("Invalid local catalogue %1: %2").arg(QFileInfo(path).fileName(),message); return {};
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
        return fail("file is unreadable or exceeds 2 MiB");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail("expected a JSON object");
    const auto root = document.object();
    if (root.value("version").toInt(-1) != 1) return fail("version must be 1");
    const QString directory = QFileInfo(path).canonicalPath();
    auto boundedPath = [&](const QJsonValue &value, bool audio) -> QString {
        if (!value.isString() || value.toString().isEmpty()) return {};
        const QString relative = value.toString();
        if (QDir::isAbsolutePath(relative) || !QUrl(relative).scheme().isEmpty() || relative.contains('\\')) return {};
        for (const auto &part : relative.split('/')) if (part == "..") return {};
        const QFileInfo candidate(QDir(directory).filePath(relative));
        const QString canonical = candidate.canonicalFilePath();
        if (!canonical.startsWith(directory + '/') || !fileError(canonical).isEmpty()) return {};
        static const QSet<QString> audioTypes = {"mp3","flac","wav","ogg","opus","m4a","aac","aiff","aif","wma","ape","alac","m4b","mp4","webm","mkv"};
        static const QSet<QString> imageTypes = {"png","jpg","jpeg","webp","bmp"};
        if (!(audio ? audioTypes : imageTypes).contains(candidate.suffix().toLower())) return {};
        return canonical;
    };
    auto validKey = [](const QString &key) {
        if (key.isEmpty() || key.size() > 128) return false;
        for (const auto c : key) if (!c.isLetterOrNumber() && c != '-' && c != '_') return false;
        return true;
    };
    auto validDetails = [](const QJsonObject &object) {
        for (const QString key : {QStringLiteral("about"),QStringLiteral("genre"),QStringLiteral("country"),QStringLiteral("language"),QStringLiteral("year")})
            if (object.contains(key) && !object.value(key).isString()) return false;
        if (object.contains("notes")) {
            const auto notes = object.value("notes");
            if (!notes.isString() && !notes.isObject()) return false;
            if (notes.isObject()) {
                const auto entries = notes.toObject();
                for (auto it=entries.begin(); it!=entries.end(); ++it) if (!it.value().isString()) return false;
            }
        }
        if (object.contains("lyrics")) {
            const auto lyrics = object.value("lyrics");
            if (!lyrics.isString() && !lyrics.isArray()) return false;
            if (lyrics.isArray()) for (const auto &line : lyrics.toArray()) {
                const auto row = line.toObject();
                if (!line.isObject() || !row.value("text").isString()) return false;
                if (row.contains("at") && (!row.value("at").isDouble() || row.value("at").toDouble() < 0)) return false;
            }
        }
        if (object.contains("credits")) {
            if (!object.value("credits").isArray()) return false;
            for (const auto &credit : object.value("credits").toArray())
                if (!credit.isString() && (!credit.isObject() || !credit.toObject().value("name").isString()
                    || !credit.toObject().value("role").isString())) return false;
        }
        return true;
    };
    auto copyDetails = [](const QJsonObject &object, QVariantMap &record) {
        for (const QString key : {QStringLiteral("lyrics"),QStringLiteral("notes"),QStringLiteral("credits"),
                QStringLiteral("about"),QStringLiteral("genre"),QStringLiteral("country"),QStringLiteral("language"),QStringLiteral("year")})
            if (object.contains(key)) record.insert(key,object.value(key).toVariant());
    };
    const auto artistObject = root.value("artist").toObject();
    const QString artistName = artistObject.value("name").toString().trimmed();
    const QString artistKey = artistObject.value("key").toString("artist");
    if (artistName.isEmpty() || artistName.size() > 1024 || !validKey(artistKey)
        || (artistObject.contains("key") && !artistObject.value("key").isString())) return fail("artist name/key is invalid");
    QVariantMap artist{{"name",artistName}};
    if (artistObject.contains("biography")) {
        if (!artistObject.value("biography").isString()) return fail("biography must be text");
        artist.insert("biography",artistObject.value("biography").toString());
    }
    if (artistObject.contains("backdrops")) {
        const auto images = artistObject.value("backdrops");
        if (!images.isArray() || images.toArray().size() > 32) return fail("invalid backdrops");
        QStringList urls;
        for (const auto &image : images.toArray()) {
            const auto imagePath = boundedPath(image,false);
            if (imagePath.isEmpty()) return fail("artwork must be a readable local image within the catalogue folder");
            urls.append(QUrl::fromLocalFile(imagePath).toString());
        }
        artist.insert("backdrops",urls);
    }
    QJsonArray releases;
    if (root.contains("releases")) {
        if (!root.value("releases").isArray()) return fail("releases must be an array");
        releases = root.value("releases").toArray();
    } else releases.append(root);
    if (releases.isEmpty() || releases.size() > 200) return fail("expected 1–200 releases");
    QVariantList result; QSet<QString> releaseKeys; int totalTracks = 0;
    // Fully validate and materialize paths before registering any identities.
    for (const auto &releaseValue : releases) {
        if (!releaseValue.isObject()) return fail("release must be an object");
        const auto release = releaseValue.toObject();
        const QString key = release.value("key").toString(root.contains("releases") ? "" : "release");
        const QString title = release.value("title").toString().trimmed();
        const QString kind = release.value("kind").toString("album");
        if (!validKey(key) || (release.contains("key") && !release.value("key").isString())
            || (release.contains("kind") && !release.value("kind").isString())
            || releaseKeys.contains(key) || title.isEmpty() || title.size() > 1024
            || !QStringList{"album","single","live","video"}.contains(kind) || !validDetails(release)) return fail("invalid or duplicate release key/title/kind/details");
        releaseKeys.insert(key);
        QVariantMap album{{"key",key},{"title",title},{"artists",QStringList{artistName}},
            {"origin","local"},{"kind",kind},{"externalIds",QVariantMap{}}};
        copyDetails(release,album);
        if (release.contains("cover")) {
            const auto imagePath = boundedPath(release.value("cover"),false);
            if (imagePath.isEmpty()) return fail("cover must be a readable local image within the catalogue folder");
            album.insert("cover",QUrl::fromLocalFile(imagePath).toString());
        }
        const auto editionsValue = release.value("editions");
        if (!editionsValue.isArray() || editionsValue.toArray().isEmpty() || editionsValue.toArray().size() > 32) return fail("expected 1–32 editions");
        QVariantList editions; QSet<QString> editionKeys;
        for (const auto &editionValue : editionsValue.toArray()) {
            if (!editionValue.isObject()) return fail("edition must be an object");
            const auto edition = editionValue.toObject();
            const QString editionKey = edition.value("key").toString("standard");
            if (!validKey(editionKey) || (edition.contains("key") && !edition.value("key").isString()) || editionKeys.contains(editionKey)
                || (edition.contains("title") && (!edition.value("title").isString() || edition.value("title").toString().trimmed().isEmpty()))) return fail("invalid or duplicate edition key");
            editionKeys.insert(editionKey);
            const auto trackValues = edition.value("tracks");
            if (!trackValues.isArray() || trackValues.toArray().isEmpty()) return fail("edition tracks must be a nonempty array");
            QVariantList tracks; QSet<QString> positions;
            for (const auto &trackValue : trackValues.toArray()) {
                if (++totalTracks > 1000) return fail("more than 1000 tracks");
                if (!trackValue.isObject()) return fail("track must be an object");
                const auto track = trackValue.toObject();
                const auto audioPath = boundedPath(track.value("file"),true);
                const QString trackTitle = track.value("title").toString().trimmed();
                const double disc = track.value("disc").toDouble(1), number = track.value("number").toDouble(tracks.size()+1);
                const double seconds = track.value("seconds").toDouble(0);
                if (audioPath.isEmpty() || trackTitle.isEmpty() || !validDetails(track)
                    || disc < 1 || disc > 999 || std::floor(disc) != disc || number < 1 || number > 9999 || std::floor(number) != number
                    || (track.contains("disc") && !track.value("disc").isDouble()) || (track.contains("number") && !track.value("number").isDouble())
                    || (track.contains("seconds") && !track.value("seconds").isDouble()) || seconds < 0 || seconds > 86400)
                    return fail("invalid track path/title/position/duration/details");
                const QString position = QString::number(int(disc))+":"+QString::number(int(number));
                if (positions.contains(position)) return fail("duplicate track position");
                positions.insert(position);
                QVariantMap row{{"title",trackTitle},{"artists",QStringList{artistName}},{"seconds",seconds},
                    {"disc",int(disc)},{"number",int(number)},{"available",true},{"localPath",audioPath},{"externalIds",QVariantMap{}}};
                copyDetails(track,row); tracks.append(row);
            }
            std::sort(tracks.begin(),tracks.end(),[](const QVariant &a,const QVariant &b) {
                const auto x=a.toMap(), y=b.toMap();
                return x.value("disc").toInt() == y.value("disc").toInt() ? x.value("number").toInt() < y.value("number").toInt() : x.value("disc").toInt() < y.value("disc").toInt();
            });
            editions.append(QVariantMap{{"key",editionKey},{"title",edition.value("title").toString(editionKey)},{"tracks",tracks}});
        }
        album.insert("editions",editions); result.append(album);
    }
    if (!m_identity) return fail("local identity is unavailable");
    // Metadata refresh keeps the existing Collection-owned source identity. File
    // playback identities still go through VaultIdentity's change/rename rules.
    QString manifestId;
    for (const auto &album : m_albums)
        if (album.value("manifestPath").toString() == path) { manifestId=album.value("manifestId").toString(); break; }
    QHash<QString,QString> fileIds;
    QStringList identityPaths;
    if (manifestId.isEmpty()) identityPaths.append(path);
    for (const auto &album : result) for (const auto &edition : album.toMap().value("editions").toList())
        for (const auto &track : edition.toMap().value("tracks").toList()) identityPaths.append(track.toMap().value("localPath").toString());
    identityPaths.removeDuplicates();
    QList<VaultIdentity::FileFacts> facts;
    for (const auto &identityPath : identityPaths) {
        for (const auto &pending : m_identity->pendingCeremonies())
            if (pending.toMap().value("newPath").toString() == identityPath) return fail("a file needs an identity decision before import");
        const QFileInfo info(identityPath);
        facts.append({identityPath,info.size(),info.lastModified().toMSecsSinceEpoch()});
    }
    const auto observations = m_identity->observeFiles(facts);
    for (qsizetype i=0; i<facts.size(); ++i) {
        const auto observation = observations.at(i);
        if (observation.value("prompt").toBool() || observation.value("id").toString().isEmpty()) return fail("a file needs an identity decision before import");
        fileIds.insert(facts.at(i).path,observation.value("id").toString());
    }
    if (manifestId.isEmpty()) manifestId=fileIds.value(path);
    const QString artistId=QStringLiteral("vinyl:local-artist:")+manifestId+":"+artistKey;
    artist.insert("id",artistId);
    for (auto &albumValue : result) {
        auto album=albumValue.toMap();
        const QString albumId=QStringLiteral("vinyl:local-release:")+manifestId+":"+album.take("key").toString();
        album.insert("id",albumId); album.insert("browseId",albumId); album.insert("artistIds",QStringList{artistId});
        album.insert("artist",artist); album.insert("manifestPath",path); album.insert("manifestId",manifestId);
        auto editions=album.value("editions").toList();
        for (auto &editionValue : editions) {
            auto edition=editionValue.toMap(); edition.insert("id",albumId+":"+edition.take("key").toString());
            auto tracks=edition.value("tracks").toList(); QVariantList discs; int discNumber=-1; QVariantList discTracks;
            for (auto &trackValue : tracks) {
                auto track=trackValue.toMap(); track.insert("id",fileIds.value(track.value("localPath").toString())); trackValue=track;
                const int number=track.value("disc").toInt();
                if (number != discNumber && !discTracks.isEmpty()) { discs.append(QVariantMap{{"number",discNumber},{"title",QStringLiteral("Disc %1").arg(discNumber)},{"tracks",discTracks}}); discTracks.clear(); }
                discNumber=number; discTracks.append(track);
            }
            if (!discTracks.isEmpty()) discs.append(QVariantMap{{"number",discNumber},{"title",QStringLiteral("Disc %1").arg(discNumber)},{"tracks",discTracks}});
            edition.insert("tracks",tracks); edition.insert("discs",discs); editionValue=edition;
        }
        album.insert("editions",editions); album.insert("tracks",editions.first().toMap().value("tracks"));
        album.insert("discs",editions.first().toMap().value("discs")); albumValue=album;
    }
    return result;
}
void VinylLocalSource::setAlbums(const QVariantList &entries) {
    const auto previousAlbums = m_albums;
    m_albums.clear(); m_tracks.clear(); m_artists.clear();
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        auto album = entry.value("payload").toMap().value("album").toMap();
        if (album.isEmpty()) album = entry;
        if (album.value("origin").toString() != "local" || album.value("id").toString().isEmpty()) continue;
        m_albums.insert(album.value("id").toString(),album);
        QVariantList tracks = album.value("tracks").toList();
        for (const auto &edition : album.value("editions").toList()) tracks += edition.toMap().value("tracks").toList();
        for (const auto &trackValue : tracks) {
            const auto track = trackValue.toMap();
            if (!track.value("id").toString().isEmpty()) m_tracks.insert(track.value("id").toString(),track);
        }
    }
    // Derive artists from the Collection projection: no competing catalogue store.
    auto ids = m_albums.keys(); std::sort(ids.begin(), ids.end());
    for (const auto &id : ids) {
        auto album = m_albums.value(id);
        QStringList artistIds;
        for (const auto &name : album.value("artists").toStringList()) {
            QString artistId = album.value("artist").toMap().value("id").toString();
            if (artistId.isEmpty()) artistId = QStringLiteral("vinyl:local-artist:unknown");
            artistIds.append(artistId);
            auto artist = m_artists.value(artistId);
            if (artist.isEmpty()) artist = {{"id",artistId},{"name",name},{"origin","local"},
                {"externalIds",QVariantMap{}},{"albums",QVariantList{}},{"singles",QVariantList{}},
                {"videos",QVariantList{}},{"live",QVariantList{}}};
            const auto supplied = album.value("artist").toMap();
            for (const QString key : {QStringLiteral("biography"),QStringLiteral("backdrops")})
                if (supplied.contains(key)) artist.insert(key,supplied.value(key));
            QString group = album.value("kind").toString();
            group = group == "single" ? "singles" : group == "video" ? "videos" : group == "live" ? "live" : "albums";
            auto releases = artist.value(group).toList(); releases.append(album); artist.insert(group,releases);
            m_artists.insert(artistId,artist);
        }
        album.insert("artistIds",artistIds); m_albums.insert(id,album);
    }
    if (m_albums != previousAlbums) ++m_revision;
}
int VinylLocalSource::request(const QString &lane,const QString &operation,const QString &value,const QVariantMap &page) {
    if ((lane != "browse" && lane != "play" && lane != "queue") || (!QStringList{"search","album","resolve","artists","artist"}.contains(operation))) return -1;
    const int id = ++m_serial; m_pending.insert(lane,id);
    // Queued even when local, so callers assign ID before completion arrives.
    QTimer::singleShot(operation=="resolve" ? m_resolutionDelayMs : 0,this,[this,lane,operation,value,id,page] {
        if (m_pending.value(lane,-1) != id) return;
        m_pending.remove(lane);
        QString error;
        if (!page.isEmpty()) {
            auto invalid = [&] { emit completed(lane,id,QVariant(),QStringLiteral("Invalid local catalogue page request.")); };
            auto integer = [](const QVariant &v, int minimum, int maximum, int *out) {
                if (v.typeId()!=QMetaType::Int && v.typeId()!=QMetaType::UInt
                    && v.typeId()!=QMetaType::LongLong && v.typeId()!=QMetaType::ULongLong
                    && v.typeId()!=QMetaType::Double) return false;
                bool ok=false; const double n=v.toDouble(&ok);
                if (!ok || !std::isfinite(n) || std::floor(n)!=n || n<minimum || n>maximum) return false;
                *out=int(n); return true;
            };
            int offset=0,size=0;
            if (operation!="search" || lane!="browse"
                || !integer(page.value("offset"),0,1000000,&offset)
                || !integer(page.value("pageSize"),1,200,&size)) {invalid();return;}
            for (auto it=page.begin();it!=page.end();++it)
                if (it.key()!="offset" && it.key()!="pageSize" && it.key()!="revision") {invalid();return;}
            const QString revision=QString::number(m_revision);
            if ((page.contains("revision") && page.value("revision").typeId()!=QMetaType::QString)
                || (offset>0 && page.value("revision").toString().isEmpty())) {invalid();return;}
            if (!page.value("revision").toString().isEmpty() && page.value("revision").toString()!=revision) {
                emit completed(lane,id,QVariant(),QStringLiteral("The local catalogue changed. Retry the search."));return;
            }
            const auto records=read(operation,value,&error).toList();
            if (offset>records.size()) {invalid();return;}
            const auto items=records.mid(offset,size);
            const int next=offset+items.size();
            const bool complete=next>=records.size();
            emit completed(lane,id,QVariantMap{{"items",items},{"revision",revision},
                {"nextOffset",complete ? -1 : next},{"complete",complete},{"origin","local"}},error);
            return;
        }
        const auto data = read(operation,value,&error);
        emit completed(lane,id,data,error);
    });
    return id;
}
void VinylLocalSource::cancel(const QString &lane) { m_pending.remove(lane); }
void VinylLocalSource::cancelAll() { m_pending.clear(); }
QVariant VinylLocalSource::read(const QString &operation,const QString &value,QString *error) const {
    if (operation == "artists") {
        QVariantList result; auto ids = m_artists.keys(); std::sort(ids.begin(),ids.end());
        for (const auto &id : ids) {
            const auto artist = m_artists.value(id);
            if (value.trimmed().isEmpty() || artist.value("name").toString().contains(value,Qt::CaseInsensitive)) result.append(artist);
        }
        return result;
    }
    if (operation == "artist") {
        if (m_artists.contains(value)) return m_artists.value(value);
        *error = QStringLiteral("This local artist is no longer in your library."); return {};
    }
    if (operation == "search") {
        QVariantList results; auto ids = m_albums.keys(); std::sort(ids.begin(),ids.end());
        for (const auto &id : ids) {
            const auto album = m_albums.value(id);
            QStringList text{album.value("title").toString(),album.value("artists").toStringList().join(' ')};
            for (const auto &track : album.value("tracks").toList()) text.append(track.toMap().value("title").toString());
            if (text.join(' ').contains(value.trimmed(),Qt::CaseInsensitive)) results.append(album);
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
