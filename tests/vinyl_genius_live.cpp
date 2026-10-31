#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>
#include "vinyl/VinylGeniusService.h"
#include "vinyl/VinylLyricsService.h"
#include "vinyl/VinylProvider.h"

// Opt-in live verification against the production parsers/service. Never run in CTest.
static QJsonDocument get(QNetworkAccessManager &network,const QUrl &url,QString *error){
    QNetworkRequest request(url);request.setRawHeader("User-Agent","Colosseum/1.0 (annotation verification)");request.setTransferTimeout(15000);
    auto *reply=network.get(request);QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);deadline.start(20000);loop.exec();
    QJsonDocument data;
    if(!reply->isFinished()){reply->abort();*error="Lyric lookup timed out.";}
    else if(reply->error()!=QNetworkReply::NoError||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)*error="Lyric lookup failed: "+reply->errorString();
    else {const auto bytes=reply->readAll();if(bytes.size()>2*1024*1024)*error="Lyric lookup exceeded its bound.";else data=QJsonDocument::fromJson(bytes);}
    reply->deleteLater();return data;
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);app.setApplicationName("vinyl-genius-live-verification");
    const auto args=app.arguments();if(args.size()!=4){qCritical("Usage: vinyl_genius_live cases.json results.json cache-directory");return 2;}
    QFile input(args[1]);if(!input.open(QIODevice::ReadOnly))return 2;
    const auto cases=QJsonDocument::fromJson(input.readAll()).array();if(cases.isEmpty())return 2;
    QNetworkAccessManager network;VinylProvider cache("unused",nullptr,128LL*1024*1024,{},args[3]);QVariantList results;int failures=0;
    for(const auto &entry:cases){
        const auto spec=entry.toObject();const auto title=spec.value("title").toString(),artist=spec.value("artist").toString(),album=spec.value("album").toString();
        QVariantMap result{{"title",title},{"artist",artist},{"kind",spec.value("kind").toString()},{"observedAtUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source",spec.contains("videoId")?"native production service; selected YouTube Music identity and duration":"native production service; LRCLIB document inspection"},{"bodyTextRecorded",false}};
        const bool catalogue=spec.contains("videoId")&&spec.value("duration").toDouble()>0;
        QString error;QUrl url(catalogue?"https://lrclib.net/api/get":"https://lrclib.net/api/search");QUrlQuery query;
        query.addQueryItem("track_name",title);query.addQueryItem("artist_name",artist);if(!album.isEmpty())query.addQueryItem("album_name",album);
        if(catalogue)query.addQueryItem("duration",QString::number(spec.value("duration").toDouble()));url.setQuery(query);
        const auto matches=get(network,url,&error);QVariantMap record;
        const QJsonArray candidates=matches.isObject()&&matches.object().contains("id")?QJsonArray{matches.object()}:matches.array();
        for(const auto &item:candidates){
            const auto candidate=item.toObject().toVariantMap();
            if(VinylGeniusService::normalizedText(candidate.value("trackName").toString())!=VinylGeniusService::normalizedText(title)
                ||VinylGeniusService::normalizedText(candidate.value("artistName").toString())!=VinylGeniusService::normalizedText(artist))continue;
            if(!album.isEmpty()&&VinylGeniusService::normalizedText(candidate.value("albumName").toString())!=VinylGeniusService::normalizedText(album))continue;
            if(record.isEmpty()||record.value("syncedLyrics").toString().isEmpty())record=candidate;
            if(!record.value("syncedLyrics").toString().isEmpty())break;
        }
        if(record.isEmpty()){result["status"]="lyrics_lookup_no_exact_match";result["error"]=error;results.append(result);++failures;continue;}
        const auto lrclibRef=vinyl::RecommendationEntityRef::fromProvider(vinyl::CanonicalEntityKind::Recording,
            vinyl::ProviderItemRef(*vinyl::ProviderId::parse("lrclib").value,*vinyl::ProviderItemId::parse(QString::number(record.value("id").toLongLong())).value));
        auto recording=lrclibRef.toJson().toVariantMap();
        if(catalogue){recording=VinylRecommendationIdentity::fromCatalogue({{"kind","song"},{"provider","youtube-music"},{"providerId",spec.value("videoId").toString()}}).value("ref").toMap();
            result["videoId"]=spec.value("videoId").toString();result["catalogueDuration"]=spec.value("duration").toDouble();}
        const QVariantMap context{{"recording",recording},{"profileScope","profile:live-verification"},{"occurrenceId","queue:live-"+QString::number(results.size())},
            {"trackName",record.value("trackName")},{"artistName",record.value("artistName")},{"artistNames",QStringList{record.value("artistName").toString()}},
            {"albumName",record.value("albumName")},{"duration",catalogue?spec.value("duration").toVariant():record.value("duration")}};
        const auto parsed=VinylLyricsService::fromLrclib(context,record);
        result["lrclibId"]=record.value("id");result["album"]=record.value("albumName");result["duration"]=record.value("duration");
        if(!parsed.ok()){result["status"]="lyrics_invalid";result["error"]=parsed.error.detail;results.append(result);++failures;continue;}
        const auto &document=*parsed.value;QVariantList lines;
        if(document.kind()==vinyl::TimedTextKind::Plain){int n=0;for(const auto &text:document.plainText().split('\n'))lines.append(QVariantMap{{"text",text},{"cueId",QString("plain:%1").arg(n++)}});}
        else for(const auto &cue:document.cues())lines.append(QVariantMap{{"text",cue.text()},{"cueId",cue.id().value()}});
        result["documentId"]=document.documentId().value();result["lineCount"]=lines.size();
        result["lyricKind"]=document.kind()==vinyl::TimedTextKind::Plain?"plain":document.kind()==vinyl::TimedTextKind::LineSynced?"line_synced":"unavailable";
        if(lines.isEmpty()){result["status"]="unavailable";result["annotationCount"]=0;result["passed"]=spec.value("allowUnavailable").toBool();if(!spec.value("allowUnavailable").toBool())++failures;results.append(result);continue;}
        VinylGeniusService service;service.setProvider(&cache);QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);bool started=false;
        QObject::connect(&service,&VinylGeniusService::dataChanged,&loop,[&]{if(service.status()=="loading")started=true;else if(started)loop.quit();});
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        service.setContext(context);service.setDocumentId(document.documentId().value());service.setLyrics(lines);deadline.start(120000);loop.exec();
        result["status"]=service.status();result["error"]=service.error();result["geniusSongId"]=service.geniusSongId();
        result["annotationCount"]=service.annotationCount();result["matchedReferentCount"]=service.matchedReferentCount();result["rejectedReferentCount"]=service.rejectedReferentCount();
        QVariantList evidence;QSet<QString> ids;
        for(const auto &group:service.notes())for(const auto &value:group.toList()){
            const auto note=value.toMap();if(ids.contains(note.value("id").toString()))continue;ids.insert(note.value("id").toString());
            evidence.append(QVariantMap{{"id",note.value("id")},{"referentId",note.value("referentId")},{"sourceUrl",note.value("sourceUrl")},
                {"startLine",note.value("startLine")},{"endLine",note.value("endLine")},{"startCueId",note.value("startCueId")},
                {"bodyCharacters",note.value("text").toString().size()},{"authorCount",note.value("authors").toList().size()},
                {"votes",note.value("votes")},{"verified",note.value("verified")}});
        }
        result["annotations"]=evidence;
        const bool passed=service.annotationCount()>0||(spec.value("allowUnavailable").toBool()&&service.status()=="unavailable");
        result["passed"]=passed;if(!passed)++failures;results.append(result);
        QSaveFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument::fromVariant(results).toJson());if(!output.commit())return 2;
        qInfo().noquote()<<title<<artist<<service.status()<<service.annotationCount();
    }
    QSaveFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument::fromVariant(results).toJson());if(!output.commit())return 2;
    return failures?1:0;
}
