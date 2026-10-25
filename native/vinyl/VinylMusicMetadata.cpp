#include "VinylMusicMetadata.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <mpv/client.h>
#include <cmath>
#include <memory>

namespace {
QString tag(const QVariantMap &tags, std::initializer_list<const char *> keys) {
    for(const auto *key:keys){const auto value=tags.value(QString::fromLatin1(key)).toString().trimmed();
        if(!value.isEmpty())return value.left(4096);}
    return {};
}
int ordinal(const QString &value, int fallback) {
    const auto match=QRegularExpression(QStringLiteral("^\\s*(\\d{1,4})(?:\\s*/\\s*\\d{1,4})?\\s*$")).match(value);
    const int number=match.hasMatch()?match.captured(1).toInt():0;
    return number>0?number:fallback;
}
QString flacCover(const QString &path) {
    QFile input(path);if(!input.open(QIODevice::ReadOnly)||input.read(4)!="fLaC")return {};
    for(int block=0;block<128;++block){
        const auto header=input.read(4);if(header.size()!=4)return {};
        const auto flags=quint8(header[0]);
        const int size=(quint8(header[1])<<16)|(quint8(header[2])<<8)|quint8(header[3]);
        if(size>16*1024*1024)return {};
        if((flags&127)==6){
            const auto bytes=input.read(size);if(bytes.size()!=size)return {};
            QDataStream data(bytes);data.setByteOrder(QDataStream::BigEndian);
            quint32 type=0,mimeSize=0,descriptionSize=0,width=0,height=0,depth=0,colours=0,length=0;
            data>>type>>mimeSize;if(mimeSize>128)return {};
            QByteArray mime(mimeSize,'\0');if(data.readRawData(mime.data(),mimeSize)!=int(mimeSize))return {};
            data>>descriptionSize;if(descriptionSize>quint32(size)||data.skipRawData(descriptionSize)!=int(descriptionSize))return {};
            data>>width>>height>>depth>>colours>>length;
            if(data.status()!=QDataStream::Ok||length>quint32(size)||length>16*1024*1024||width>12000||height>12000)return {};
            QByteArray picture(length,'\0');if(data.readRawData(picture.data(),length)!=int(length))return {};
            const QString extension=picture.startsWith("\x89PNG\r\n\x1a\n")?"png":picture.startsWith("\xff\xd8\xff")?"jpg":"";
            if((type!=3&&type!=0)||extension.isEmpty()){if(flags&128)break;continue;}
            const auto directory=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/artwork/vinyl";
            if(!QDir().mkpath(directory))return {};
            const auto file=directory+"/"+QString::fromLatin1(QCryptographicHash::hash(picture,QCryptographicHash::Sha256).toHex())+"."+extension;
            if(!QFileInfo::exists(file)){QSaveFile output(file);if(!output.open(QIODevice::WriteOnly)||output.write(picture)!=picture.size()||!output.commit())return {};}
            return QUrl::fromLocalFile(file).toString();
        }
        if(!input.seek(input.pos()+size))return {};
        if(flags&128)break;
    }
    return {};
}
QString sidecarCover(const QString &path, bool multiDisc) {
    QStringList directories{QFileInfo(path).absolutePath()};
    if(multiDisc)directories.append(QFileInfo(directories.first()).absolutePath());
    for(const auto &directory:directories){
        QStringList candidates{directory,directory+"/Cover",directory+"/Covers",directory+"/Artwork"};
        for(const auto &candidate:candidates){
            const auto files=QDir(candidate).entryInfoList({"*.jpg","*.jpeg","*.png","*.webp"},QDir::Files|QDir::Readable,QDir::Name);
            for(const auto &file:files){
                const auto name=file.completeBaseName().toLower();
                if(name=="folder"||name=="cover"||name=="front"||name=="a front"||name=="albumart")
                    return QUrl::fromLocalFile(file.canonicalFilePath()).toString();
            }
        }
    }
    return {};
}
}

QVariantMap VinylMusicMetadata::read(const QString &path, const std::atomic_bool *cancel) {
    QVariantMap result,tags;
    auto cancelled=[&]{return cancel&&cancel->load();};
    if(cancelled())return {};
    std::unique_ptr<mpv_handle,decltype(&mpv_terminate_destroy)> handle(mpv_create(),mpv_terminate_destroy);
    if(handle){
        for(const auto &option:QList<QPair<QByteArray,QByteArray>>{
            {"config","no"},{"terminal","no"},{"msg-level","all=no"},{"vo","null"},
            {"ao","null"},{"vid","no"},{"sid","no"},{"pause","yes"},{"idle","yes"},
            {"ytdl","no"},{"load-scripts","no"},{"autoload-files","no"},{"demuxer-max-bytes","4MiB"}})
            mpv_set_option_string(handle.get(),option.first.constData(),option.second.constData());
        if(mpv_initialize(handle.get())>=0){
            const auto utf8=path.toUtf8(); // libmpv paths are UTF-8 on Windows too.
            const char *command[]{"loadfile",utf8.constData(),nullptr};
            if(mpv_command(handle.get(),command)>=0){
                QElapsedTimer deadline;deadline.start();
                while(!cancelled()&&deadline.elapsed()<5000){
                    const auto *event=mpv_wait_event(handle.get(),0.025);
                    if(event->event_id==MPV_EVENT_END_FILE||event->event_id==MPV_EVENT_SHUTDOWN)break;
                    if(event->event_id!=MPV_EVENT_FILE_LOADED)continue;
                    mpv_node metadata{};
                    if(mpv_get_property(handle.get(),"metadata",MPV_FORMAT_NODE,&metadata)>=0){
                        if(metadata.format==MPV_FORMAT_NODE_MAP&&metadata.u.list){
                            const auto *list=metadata.u.list;
                            for(int i=0;i<list->num&&i<512;++i)
                                if(list->values[i].format==MPV_FORMAT_STRING)
                                    tags.insert(QString::fromUtf8(list->keys[i]).toLower().replace('_',' '),QString::fromUtf8(list->values[i].u.string).left(4096));
                        }
                        mpv_free_node_contents(&metadata);
                    }
                    double seconds=0;
                    if(mpv_get_property(handle.get(),"duration",MPV_FORMAT_DOUBLE,&seconds)>=0&&std::isfinite(seconds)&&seconds>0)
                        result.insert("seconds",seconds);
                    result.insert("metadataRead",true);break;
                }
            }
        }
    }
    if(cancelled())return {};
    const auto title=tag(tags,{"title"}),artist=tag(tags,{"artist"}),albumArtist=tag(tags,{"album artist","albumartist"}),rawAlbum=tag(tags,{"album"});
    const auto discText=tag(tags,{"disc","discnumber","disc number"});
    const int disc=ordinal(discText,1);
    int total=ordinal(tag(tags,{"disctotal","totaldiscs","disc total","total discs"}),0);
    if(!total&&discText.contains('/'))total=ordinal(discText.section('/',1),0);
    QString album=rawAlbum;
    // Only split a disc suffix when explicit tags establish its meaning.
    if(total!=1&&!discText.isEmpty()){
        const auto suffix=QRegularExpression(QStringLiteral("\\s*[-–:]?\\s*[([]?\\b(?:CD|Disc)\\s*0*%1[)\\]]?\\s*$").arg(disc),QRegularExpression::CaseInsensitiveOption);
        const auto match=suffix.match(album);if(match.hasMatch()&&match.capturedStart()>0)album=album.left(match.capturedStart()).trimmed();
    }
    result.insert("title",title.isEmpty()?QFileInfo(path).completeBaseName():title);
    result.insert("artists",QStringList{artist.isEmpty()?(albumArtist.isEmpty()?QStringLiteral("Unknown artist"):albumArtist):artist});
    result.insert("albumArtists",albumArtist.isEmpty()?QStringList{}:QStringList{albumArtist});
    result.insert("albumArtistTagged",!albumArtist.isEmpty());
    result.insert("albumTitle",album);result.insert("taggedAlbumTitle",rawAlbum);
    result.insert("disc",disc);result.insert("discTotal",total);
    result.insert("hasDiscTag",!discText.isEmpty());
    result.insert("number",ordinal(tag(tags,{"track","tracknumber","track number"}),0));
    const auto date=tag(tags,{"date","year"});
    const auto year=QRegularExpression("^(\\d{4})").match(date);
    result.insert("year",year.hasMatch()?year.captured(1):QString());
    result.insert("genre",tag(tags,{"genre"}));
    result.insert("releaseMbid",tag(tags,{"musicbrainz albumid","musicbrainz album id"}));
    QString cover=flacCover(path);if(cover.isEmpty())cover=sidecarCover(path,total>1);
    result.insert("cover",cover);
    result.insert("localPath",path);
    return result;
}
