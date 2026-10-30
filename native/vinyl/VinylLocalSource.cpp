#include "VinylLocalSource.h"
#include "VinylMusicMetadata.h"
#include <QThread>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <functional>
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

namespace {
const QSet<QString> audioExtensions={"mp3","flac","wav","ogg","opus","m4a","aac","aiff","aif","wma","ape","alac","m4b"};
QVariantMap inspectMusic(const QStringList &inputs, bool folders, const std::atomic_bool *cancel,
    const std::function<void(int,int,const QString &)> &progress={}) {
    QStringList paths,errors;QSet<QString> seen,visited;
    auto cancelled=[&]{return cancel&&cancel->load();};
    QStringList pending=inputs;int entries=0;
    while(!pending.isEmpty()&&!cancelled()){
        const QString input=pending.takeLast(),path=localPath(input);QFileInfo file(path);
        if(path.isEmpty()){errors.append("Only local audio files can be imported.");continue;}
        if(++entries>50000){errors.append("The folder selection exceeds 50,000 entries. Import smaller folders.");break;}
        if(file.isDir()&&folders){
            const auto canonical=file.canonicalFilePath();
            if(file.isSymLink()||visited.contains(canonical))continue;visited.insert(canonical);
            const auto children=QDir(path).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden,QDir::Name);
            for(const auto &child:children)if(!child.isSymLink()&&(child.isDir()||audioExtensions.contains(child.suffix().toLower())||child.fileName().endsWith(".vinyl.json",Qt::CaseInsensitive)))pending.append(child.absoluteFilePath());
            continue;
        }
        const auto problem=fileError(path);if(!problem.isEmpty()){errors.append(problem);continue;}
        if(!audioExtensions.contains(file.suffix().toLower())&&!file.fileName().endsWith(".vinyl.json",Qt::CaseInsensitive)){
            errors.append(QStringLiteral("Unsupported audio file type: %1").arg(file.fileName()));continue;
        }
        const auto canonical=file.canonicalFilePath(),key=VaultIdentity::normalizePath(canonical);
        if(seen.contains(key))continue;seen.insert(key);paths.append(canonical);
        if(paths.size()>=10000){errors.append("Import limited to 10,000 files. Import smaller folders to continue.");break;}
    }
    std::sort(paths.begin(),paths.end());QVariantList records;
    for(int index=0;index<paths.size()&&!cancelled();++index){
        const auto path=paths[index];const QFileInfo before(path);
        QVariantMap record;
        if(!path.endsWith(".vinyl.json",Qt::CaseInsensitive))record=VinylMusicMetadata::read(path,cancel);
        record.insert("localPath",path);record.insert("fileSize",before.size());record.insert("modifiedMs",before.lastModified().toMSecsSinceEpoch());
        records.append(record);if(progress)progress(index+1,paths.size(),before.fileName());
    }
    return {{"records",records},{"errors",errors}};
}
QString musicHash(const QVariantList &parts) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(parts).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
QVariantMap organizeAlbum(QVariantMap album, QVariantList tracks) {
    if(!album.value("albumArtistTagged").toBool()){
        QStringList artists;for(const auto &track:tracks)for(const auto &name:track.toMap().value("artists").toStringList())
            if(!artists.contains(name))artists.append(name);
        std::sort(artists.begin(),artists.end());QStringList artistIds;
        for(const auto &name:artists)artistIds.append("vinyl:local-artist:"+musicHash({name.toCaseFolded()}));
        album["artists"]=artists;album["artistIds"]=artistIds;
        if(!artists.isEmpty())album["artist"]=QVariantMap{{"id",artistIds.first()},{"name",artists.first()}};
    }
    std::sort(tracks.begin(),tracks.end(),[](const QVariant &a,const QVariant &b){
        const auto x=a.toMap(),y=b.toMap();
        const int xd=x.value("disc",1).toInt(),yd=y.value("disc",1).toInt();if(xd!=yd)return xd<yd;
        const int xn=x.value("taggedNumber",x.value("number",0)).toInt(),yn=y.value("taggedNumber",y.value("number",0)).toInt();
        if(xn!=yn)return xn>0&&(yn==0||xn<yn);
        return x.value("localPath").toString()<y.value("localPath").toString();
    });
    QVariantList discs,discTracks;int discNumber=-1;
    for(auto &value:tracks){auto track=value.toMap();const int disc=track.value("disc",1).toInt();
        if(disc!=discNumber&&!discTracks.isEmpty()){discs.append(QVariantMap{{"number",discNumber},{"title",QString("Disc %1").arg(discNumber)},{"tracks",discTracks}});discTracks.clear();}
        discNumber=disc;if(track.value("number").toInt()<=0)track["number"]=discTracks.size()+1;
        value=track;discTracks.append(track);
    }
    if(!discTracks.isEmpty())discs.append(QVariantMap{{"number",discNumber},{"title",QString("Disc %1").arg(discNumber)},{"tracks",discTracks}});
    const auto editionId=album.value("id").toString()+":local";
    const QVariantMap edition{{"id",editionId},{"title","Local files"},{"tracks",tracks},{"discs",discs}};
    album["tracks"]=tracks;album["discs"]=discs;album["editions"]=QVariantList{edition};return album;
}
}

VinylLocalSource::~VinylLocalSource() {
    cancelImport();for(auto *worker:m_importWorkers){worker->wait();delete worker;}
}
int VinylLocalSource::startImport(const QStringList &inputs) {
    cancelImport();const int request=++m_serial;m_importRequest=request;
    const auto cancelled=std::make_shared<std::atomic_bool>(false);m_importCancelled=cancelled;
    auto *worker=QThread::create([this,inputs,request,cancelled]{
        auto inspection=inspectMusic(inputs,true,cancelled.get(),[this,request,cancelled](int processed,int total,const QString &name){
            QMetaObject::invokeMethod(this,[this,request,cancelled,processed,total,name]{
                if(!cancelled->load()&&m_importRequest==request)emit importProgress(request,processed,total,name);
            },Qt::QueuedConnection);
        });
        if(cancelled->load())return;
        QMetaObject::invokeMethod(this,[this,request,cancelled,inspection]{
            if(cancelled->load()||m_importRequest!=request)return;
            m_importRequest=-1;const auto result=finishImport(inspection);emit importCompleted(request,result);
        },Qt::QueuedConnection);
    });
    m_importWorkers.append(worker);
    connect(worker,&QThread::finished,this,[this,worker]{m_importWorkers.removeOne(worker);worker->deleteLater();});
    worker->start();return request;
}
void VinylLocalSource::cancelImport() {
    m_importRequest=-1;if(m_importCancelled)m_importCancelled->store(true);
}
QVariantMap VinylLocalSource::importFiles(const QStringList &inputs) {
    // Compatibility seam for bounded native callers; the product uses startImport.
    return finishImport(inspectMusic(inputs,false,nullptr));
}
QVariantMap VinylLocalSource::finishImport(const QVariantMap &inspection) {
    QVariantList albums,ceremonies;QStringList errors=inspection.value("errors").toStringList(),replacedManifests,replacedIds;
    QHash<QString,QVariantMap> changed;QSet<QString> seen;int imported=0;
    QSet<QString> manifestFiles,manifestTrackIds;
    QHash<QString,QVariantMap> inspectedFiles;
    for(const auto &value:inspection.value("records").toList())inspectedFiles.insert(value.toMap().value("localPath").toString(),value.toMap());
    // Explicit catalogues win over automatic tags when a folder contains both.
    // Validate them first; a malformed catalogue must not hide loose audio.
    for(const auto &value:inspection.value("records").toList()){
        const auto path=value.toMap().value("localPath").toString();
        if(!path.endsWith(".vinyl.json",Qt::CaseInsensitive))continue;
        const auto record=value.toMap();const QFileInfo manifest(path);
        if(manifest.size()!=record.value("fileSize").toLongLong()||manifest.lastModified().toMSecsSinceEpoch()!=record.value("modifiedMs").toLongLong()){
            errors.append("This catalogue changed during import: "+path);continue;
        }
        bool changedReference=false;
        QFile json(path);
        if(json.open(QIODevice::ReadOnly)&&json.size()<=2*1024*1024){
            const auto root=QJsonDocument::fromJson(json.readAll()).object();
            const auto releases=root.contains("releases")?root.value("releases").toArray():QJsonArray{root};
            for(const auto &release:releases)for(const auto &edition:release.toObject().value("editions").toArray())for(const auto &item:edition.toObject().value("tracks").toArray()){
                const QFileInfo audio(QDir(manifest.canonicalPath()).filePath(item.toObject().value("file").toString()));
                const auto prior=inspectedFiles.value(audio.canonicalFilePath());
                if(!prior.isEmpty()&&(audio.size()!=prior.value("fileSize").toLongLong()||audio.lastModified().toMSecsSinceEpoch()!=prior.value("modifiedMs").toLongLong()))changedReference=true;
            }
        }
        if(changedReference){errors.append("Audio referenced by this catalogue changed during import: "+path);continue;}
        QString error;const auto releases=importManifest(path,&error,&ceremonies);
        // importManifest observes identities on the owner thread. Recheck its
        // materialized paths before admitting them to Collection as well; a
        // modification between validation and observation must not silently
        // substitute a newer file. Playback separately revalidates Vault facts.
        if(error.isEmpty())for(const auto &release:releases)for(const auto &edition:release.toMap().value("editions").toList())for(const auto &item:edition.toMap().value("tracks").toList()){
            const auto audio=item.toMap().value("localPath").toString();const auto prior=inspectedFiles.value(audio);const QFileInfo current(audio);
            if(!prior.isEmpty()&&(current.size()!=prior.value("fileSize").toLongLong()||current.lastModified().toMSecsSinceEpoch()!=prior.value("modifiedMs").toLongLong()))
                error="Audio referenced by this catalogue changed during import: "+path;
        }
        if(!error.isEmpty())errors.append(error);else{
            replacedManifests.append(path);
            for(const auto &release:releases){albums.append(release);
                for(const auto &edition:release.toMap().value("editions").toList())for(const auto &track:edition.toMap().value("tracks").toList()){
                    const auto audio=track.toMap().value("localPath").toString();
                    manifestTrackIds.insert(track.toMap().value("id").toString());
                    if(!manifestFiles.contains(audio)){manifestFiles.insert(audio);++imported;}
                }
            }
        }
    }
    for(auto it=m_albums.cbegin();it!=m_albums.cend();++it){
        if(!it.value().value("manifestPath").toString().isEmpty())continue;
        auto oldTracks=it.value().value("tracks").toList();bool removed=false;
        for(int index=oldTracks.size()-1;index>=0;--index)if(manifestTrackIds.contains(oldTracks[index].toMap().value("id").toString())){oldTracks.removeAt(index);removed=true;}
        if(removed){auto old=it.value();old["tracks"]=oldTracks;changed.insert(it.key(),old);replacedIds.append(it.key());}
    }
    // Keep the shared registry on its owner thread, with one durable write for
    // the batch rather than serializing its growing JSON once per audio file.
    QHash<QString,QVariantMap> observations;
    if(m_identity){
        QList<VaultIdentity::FileFacts> facts;
        for(const auto &value:inspection.value("records").toList()){
            const auto record=value.toMap();const auto path=record.value("localPath").toString();const QFileInfo file(path);
            if(path.endsWith(".vinyl.json",Qt::CaseInsensitive)||manifestFiles.contains(path)||!fileError(path).isEmpty()
                ||file.size()!=record.value("fileSize").toLongLong()||file.lastModified().toMSecsSinceEpoch()!=record.value("modifiedMs").toLongLong())continue;
            facts.append({path,file.size(),file.lastModified().toMSecsSinceEpoch()});
        }
        const auto results=m_identity->observeFiles(facts);
        for(int index=0;index<facts.size();++index)observations.insert(facts[index].path,results[index]);
    }
    for(const auto &value:inspection.value("records").toList()){
        auto track=value.toMap();const auto path=track.value("localPath").toString();const QFileInfo file(path);
        if(path.endsWith(".vinyl.json",Qt::CaseInsensitive)||manifestFiles.contains(path))continue;
        if(file.size()!=track.take("fileSize").toLongLong()||file.lastModified().toMSecsSinceEpoch()!=track.take("modifiedMs").toLongLong()||!fileError(path).isEmpty()){
            errors.append("This file changed or became unavailable during import: "+path);continue;
        }
        if(!m_identity){errors.append("Local file identity is unavailable.");continue;}
        bool pending=false;for(const auto &ceremony:m_identity->pendingCeremonies())if(ceremony.toMap().value("newPath").toString()==path){ceremonies.append(ceremony);pending=true;break;}
        if(pending){errors.append("This file needs an identity decision before import: "+path);continue;}
        const auto observation=observations.value(path);
        if(observation.value("prompt").toBool()){ceremonies.append(observation);errors.append("This file needs an identity decision before import: "+path);continue;}
        const auto id=observation.value("id").toString();
        if(id.isEmpty()){errors.append(observation.value("error","Local identity could not be saved.").toString());continue;}
        if(seen.contains(id))continue;seen.insert(id);++imported;
        const auto title=track.take("albumTitle").toString();
        const auto taggedArtists=track.take("albumArtists").toStringList();
        const auto artistNames=taggedArtists.isEmpty()?track.value("artists").toStringList():taggedArtists;
        const auto releaseId=track.take("releaseMbid").toString();
        QString directory=file.absolutePath();const int disc=track.value("disc",1).toInt();
        if(track.value("hasDiscTag").toBool()&&QRegularExpression(QString("^(?:cd|disc|disk)[ _-]*0*%1$").arg(disc),QRegularExpression::CaseInsensitiveOption)
            .match(QFileInfo(directory).fileName()).hasMatch())directory=QFileInfo(directory).absolutePath();
        const auto group=musicHash({title.toCaseFolded(),taggedArtists.join("\n").toCaseFolded(),track.value("year"),releaseId,VaultIdentity::normalizePath(directory)});
        const QString albumId=title.isEmpty()?QString("vinyl:local-release:")+id:QString("vinyl:local-album:")+group;
        QVariantMap album=changed.value(albumId,m_albums.value(albumId));
        if(album.isEmpty()){
            const auto artistId=QString("vinyl:local-artist:")+musicHash({artistNames.join("\n").toCaseFolded()});
            album={{"id",albumId},{"browseId",albumId},{"title",title.isEmpty()?track.value("title"):title},{"artists",artistNames},
                {"artistIds",QStringList{artistId}},{"artist",QVariantMap{{"id",artistId},{"name",artistNames.join(" · ")}}},
                {"year",track.value("year")},{"genre",track.value("genre")},{"cover",track.value("cover")},
                {"origin","local"},{"localGroupKey",group},{"albumArtistTagged",track.value("albumArtistTagged")},{"externalIds",QVariantMap{}}};
        }
        if(album.value("cover").toString().isEmpty())album["cover"]=track.value("cover");
        album["discTotal"]=std::max(album.value("discTotal").toInt(),track.value("discTotal").toInt());
        track["taggedNumber"]=track.value("number",0);track["id"]=id;track["available"]=true;track["externalIds"]=QVariantMap{};
        auto tracks=album.value("tracks").toList();bool replaced=false;
        for(auto &old:tracks)if(old.toMap().value("id")==id){old=track;replaced=true;break;}
        if(!replaced)tracks.append(track);
        album["tracks"]=tracks;changed.insert(albumId,album);
        // Upgrade old filename-only records and move changed grouping atomically
        // through Collection. Queue snapshots keep their existing Vault IDs.
        for(auto it=m_albums.cbegin();it!=m_albums.cend();++it){
            if(it.key()==albumId||!it.value().value("manifestPath").toString().isEmpty())continue;
            auto old=changed.value(it.key(),it.value());auto oldTracks=old.value("tracks").toList();bool removed=false;
            for(int index=oldTracks.size()-1;index>=0;--index)if(oldTracks[index].toMap().value("id")==id){oldTracks.removeAt(index);removed=true;}
            if(removed){replacedIds.append(it.key());old["tracks"]=oldTracks;changed.insert(it.key(),old);}
        }
    }
    auto ids=changed.keys();std::sort(ids.begin(),ids.end());
    for(const auto &id:ids)if(!changed.value(id).value("tracks").toList().isEmpty())
        albums.append(organizeAlbum(changed.value(id),changed.value(id).value("tracks").toList()));
    replacedIds.removeDuplicates();
    return {{"albums",albums},{"errors",errors},{"replacedManifestPaths",replacedManifests},
        {"replacedAlbumIds",replacedIds},{"importedTracks",imported},{"identityCeremonies",ceremonies}};
}
bool VinylLocalSource::decideImportIdentity(const QString &relationship, const QString &choice) {
    return m_identity && m_identity->decideFileCeremony(relationship, choice);
}
QVariantList VinylLocalSource::importManifest(const QString &path, QString *error, QVariantList *ceremonies) {
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
            if (pending.toMap().value("newPath").toString() == identityPath) {
                ceremonies->append(pending); return fail("a file needs an identity decision before import");
            }
        const QFileInfo info(identityPath);
        facts.append({identityPath,info.size(),info.lastModified().toMSecsSinceEpoch()});
    }
    const auto observations = m_identity->observeFiles(facts);
    for (qsizetype i=0; i<facts.size(); ++i) {
        const auto observation = observations.at(i);
        if (observation.value("prompt").toBool() || observation.value("id").toString().isEmpty()) {
            if (observation.value("prompt").toBool()) ceremonies->append(observation);
            return fail(observation.value("error").toString().isEmpty()
                ? QStringLiteral("a file needs an identity decision before import") : observation.value("error").toString());
        }
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
        const auto suppliedIds=album.value("artistIds").toStringList();int artistIndex=0;
        for (const auto &name : album.value("artists").toStringList()) {
            QString artistId = suppliedIds.value(artistIndex++);
            if(artistId.isEmpty())artistId=album.value("artist").toMap().value("id").toString();
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
    if ((lane != "browse" && lane != "play" && lane != "queue" && lane != "named-collections") || (!QStringList{"search","album","resolve","artists","artist"}.contains(operation))) return -1;
    if (lane == "named-collections" && operation != "album") return -1;
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
void VinylLocalSource::cancelAll() { m_pending.clear();cancelImport(); }
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
