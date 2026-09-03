#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>

#include "../TorrentHttpSurface.h"
#include "../StreamProgressTracker.h"

using namespace colosseum::server::torrent_http;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

QByteArray header(const TorrentHttpReply &reply, const QByteArray &name)
{
    for (auto it=reply.headers.cbegin(); it!=reply.headers.cend(); ++it)
        if (it.key().compare(name, Qt::CaseInsensitive)==0) return it.value();
    return {};
}

class FakeBackend final : public TorrentHttpBackend {
public:
    void ensureEngine(const QString &hash, const QJsonObject &options, ReadyCallback cb) override
    {
        ensuredHash=hash; ensuredOptions=options; ++ensureCount; cb(ensureError);
    }
    void createFromTorrent(const QByteArray &bytes, TorrentReadyCallback cb) override
    {
        createdBytes=bytes; ++createTorrentCount; cb(createdHash, createTorrentError);
    }
    QJsonObject defaultEngineOptions(const QString &) const override
    {
        return defaults;
    }
    QJsonValue globalStats() const override { return global; }
    QJsonObject systemStats() const override { return system; }
    QJsonValue stats(const QString &, std::optional<int>) const override { return statsValue; }
    QVector<TorrentFileView> files(const QString &) const override { return fileList; }
    std::optional<int> guessFileIndex(const QString &, const QJsonObject &hint) const override
    { lastGuessHint=hint; ++guessCount; return guessedIndex; }
    void remove(const QString &hash, std::function<void()> done) override
    { removedHash=hash; ++removeCount; if(done) done(); }
    void removeAll() override { ++removeAllCount; }
    void prewarm(const QString &hash,int index) override { prewarmedHash=hash; prewarmedIndex=index; ++prewarmCount; }
    void streamOpened(const QString &hash,int index) override { openedHash=hash; openedIndex=index; ++openCount; }
    void streamClosed(const QString &hash,int index) override { closedHash=hash; closedIndex=index; ++closeCount; }

    mutable int guessCount=0; mutable QJsonObject lastGuessHint;
    int ensureCount=0,createTorrentCount=0,removeCount=0,removeAllCount=0,prewarmCount=0,openCount=0,closeCount=0;
    QString ensuredHash,ensureError,createdHash=QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),createTorrentError,removedHash,prewarmedHash,openedHash,closedHash;
    int prewarmedIndex=-1,openedIndex=-1,closedIndex=-1;
    QJsonObject ensuredOptions;
    QByteArray createdBytes;
    QJsonObject defaults{{"peerSearch",QJsonObject{{"min",40},{"max",150},{"sources",QJsonArray{QStringLiteral("default")}}}}};
    QJsonValue global=QJsonObject{};
    QJsonObject system{{"marker",7}};
    QJsonValue statsValue=QJsonObject{{"infoHash",QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")}};
    QVector<TorrentFileView> fileList;
    std::optional<int> guessedIndex;
};

class FakeSource final : public TorrentCreateSource {
public:
    void load(const QString &source,std::function<void(QByteArray,QString)> cb) override
    { lastSource=source; ++loads; cb(bytes,error); }
    int loads=0; QString lastSource; QByteArray bytes="torrent-source"; QString error;
};

TorrentHttpReply dispatch(TorrentHttpSurface &surface, const TorrentHttpRequest &request)
{
    bool called=false; TorrentHttpReply reply;
    const bool handled=surface.dispatch(request,[&](TorrentHttpReply value){called=true; reply=std::move(value);});
    require(handled,"route is handled"); require(called,"fake backend completes response synchronously");
    return reply;
}

void testStatsOracleAndRemoval()
{
    FakeBackend backend; FakeSource source; TorrentHttpSurface surface(backend,source);
    TorrentHttpRequest req; req.method="GET"; req.path="/stats.json";
    auto reply=dispatch(surface,req);
    require(reply.status==200,"empty global stats status matches oracle");
    require(reply.body=="{}","empty global stats body matches oracle");
    require(header(reply,"content-type")=="application/json","stats content type is JSON");

    req.path="/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/stats.json"; backend.statsValue=QJsonValue(QJsonValue::Null);
    reply=dispatch(surface,req); require(reply.body=="null","missing engine stats serialize as JSON null");

    req.path="/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/remove"; reply=dispatch(surface,req);
    require(reply.status==200 && reply.body=="{}","remove returns empty JSON object");
    require(backend.removedHash==QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),"remove lowercases infoHash");

    req.path="/removeAll"; reply=dispatch(surface,req);
    require(reply.status==200 && backend.removeAllCount==1,"removeAll responds immediately and delegates");
}

void testCreateAndGuessedFileAugmentation()
{
    FakeBackend backend; FakeSource source; TorrentHttpSurface surface(backend,source);
    backend.fileList={{0,"note.srt","Pack/note.srt",10,0},{1,"movie.mp4","Pack/movie.mp4",900,10}};
    backend.statsValue=QJsonObject{{"infoHash",QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")},{"files",QJsonArray{}}};
    TorrentHttpRequest req; req.method="POST"; req.path="/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/create";
    req.body=QJsonObject{{"fileMustInclude",QJsonArray{QStringLiteral("\\.mp4$")}},{"guessFileIdx",true}};
    auto reply=dispatch(surface,req);
    require(backend.ensuredHash==QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),"create lowercases hash");
    const auto object=QJsonDocument::fromJson(reply.body).object();
    require(object.value("guessedFileIdx").toInt()==1,"fileMustInclude sets guessedFileIdx");
    require(backend.guessCount==0,"fileMustInclude match wins before metadata guesser");

    req.body=QJsonObject{{"guessFileIdx",QJsonObject{{"season",1},{"episode",2}}}}; backend.guessedIndex=1;
    reply=dispatch(surface,req); require(backend.guessCount==1,"guessFileIdx delegates to W04-owned guesser");
    require(backend.lastGuessHint.value("season").toInt()==1,"series hint reaches W04 adapter unchanged");

    TorrentHttpRequest blob; blob.method="POST"; blob.path="/create"; blob.body=QJsonObject{{"blob",QStringLiteral("0001feff")}};
    reply=dispatch(surface,blob); require(backend.createdBytes==QByteArray::fromHex("0001feff"),"hex blob is decoded before torrent creation");

    TorrentHttpRequest from; from.method="POST"; from.path="/create"; from.body=QJsonObject{{"from",QStringLiteral("fixture.torrent")}};
    reply=dispatch(surface,from); require(source.lastSource==QStringLiteral("fixture.torrent"),"from source is delegated to source loader");
    require(backend.createdBytes==source.bytes,"loaded source bytes reach torrent creator");
}

void testMediaRangeHeadAndOddInvalidFallback()
{
    FakeBackend backend; FakeSource source; TorrentHttpSurface surface(backend,source);
    const QString hashValue=QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    backend.fileList={{0,"movie.mp4","Pack/movie.mp4",100,0}};
    TorrentHttpRequest req; req.method="GET"; req.path="/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/0";
    req.headers.insert("range","bytes=10-19"); req.headers.insert("enginefs-prio","0");
    auto reply=dispatch(surface,req);
    require(reply.status==206,"bounded range is partial content");
    require(header(reply,"accept-ranges")=="bytes","media advertises byte ranges");
    require(header(reply,"content-range")=="bytes 10-19/100","bounded content range is inclusive");
    require(header(reply,"content-length")=="10","bounded content length matches range");
    require(header(reply,"content-type")=="video/mp4","mp4 MIME matches Stremio mime table");
    require(header(reply,"cache-control")=="max-age=0, no-cache","torrent media is non-cacheable over HTTP");
    require(header(reply,"transfermode.dlna.org")=="Streaming","DLNA transfer mode is preserved");
    require(reply.readPlan && reply.readPlan->start==10 && reply.readPlan->end==19,"read plan preserves requested bytes");
    require(reply.readPlan->priority && *reply.readPlan->priority==1,"enginefs-prio zero falls back to one like parseInt(value)||1");
    require(backend.openCount==1 && reply.streamLease,"stream-open occurs for media response");
    reply.streamLease->close(); reply.streamLease->close(); require(backend.closeCount==1,"stream-close lease is idempotent");

    req.headers["range"]="bytes=0-0"; req.headers["enginefs-prio"]=QByteArray(0, 'x');
    reply=dispatch(surface,req); require(reply.readPlan && !reply.readPlan->priority,"empty enginefs-prio is falsy and must not create a priority override"); reply.streamLease->close();

    req.headers["range"]="bytes=5-"; req.headers.remove("enginefs-prio");
    reply=dispatch(surface,req); require(reply.status==206 && reply.readPlan->start==5 && reply.readPlan->end==99,"open-ended range reaches EOF");
    require(backend.prewarmCount==1,"open-ended range prewarms before streaming"); reply.streamLease->close();

    backend.defaults.insert("circularBuffer",QJsonObject{{"type",QStringLiteral("memory")}});
    req.headers["range"]="bytes=7-";
    reply=dispatch(surface,req); require(reply.status==206,"circular-buffer open-ended range still serves partial content");
    require(backend.prewarmCount==1,"circularBuffer suppresses prewarm exactly like module 172"); reply.streamLease->close();
    backend.defaults.remove("circularBuffer");

    req.headers["range"]="bytes=999999-1000000";
    reply=dispatch(surface,req); require(reply.status==200,"unsatisfiable range intentionally falls back to full 200");
    require(!header(reply,"content-range").size(),"invalid range omits Content-Range");
    require(reply.readPlan && reply.readPlan->start==0 && reply.readPlan->end==99,"invalid range reads full file like module 172"); reply.streamLease->close();

    req.method="HEAD"; req.headers.clear();
    reply=dispatch(surface,req); require(reply.status==200 && !reply.readPlan,"HEAD sends headers without opening FileStream read");
    require(header(reply,"content-length")=="100","HEAD reports whole length"); require(reply.streamLease!=nullptr,"HEAD still emits stream-open"); reply.streamLease->close();
}

void testMediaQueriesRedirectAndFiltering()
{
    FakeBackend backend; FakeSource source; TorrentHttpSurface surface(backend,source);
    backend.fileList={{0,"note.srt","Pack/note.srt",10,0},{1,"My Movie.mp4","Pack/My Movie.mp4",100,10}};
    TorrentHttpRequest req; req.method="GET"; req.path="/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0";
    req.query.insert("tr",QStringList{QStringLiteral("tracker:a"),QStringLiteral("dht:custom")});
    req.query.insert("f",QStringList{QStringLiteral("/movie\\.mp4$/i")});
    req.query.insert("download",QStringList{QStringLiteral("1")}); req.query.insert("subtitles",QStringList{QStringLiteral("track.srt")});
    auto reply=dispatch(surface,req);
    require(reply.readPlan && reply.readPlan->fileIndex==1,"query f overrides numeric index with first matching file");
    const auto peerSearch=backend.ensuredOptions.value("peerSearch").toObject();
    require(peerSearch.value("min").toInt()==40 && peerSearch.value("max").toInt()==150,"tracker override preserves default peer search bounds");
    require(peerSearch.value("sources").toArray().size()==2,"tracker query replaces peer sources");
    require(header(reply,"content-disposition")=="attachment; filename=\"My Movie.mp4\";","download query sets Stremio attachment header");
    require(header(reply,"captioninfo.sec")=="track.srt","subtitle query maps to CaptionInfo.sec"); reply.streamLease->close();

    TorrentHttpRequest external=req; external.query.insert("external",QStringList{QStringLiteral("1")});
    reply=dispatch(surface,external); require(reply.status==307,"external query redirects");
    require(header(reply,"location")=="/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/My%20Movie.mp4?download=1","external Location uses encoded file name and download flag only");
    require(!reply.streamLease,"external redirect happens before stream-open");

    TorrentHttpRequest byName; byName.method="GET"; byName.path="/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/My%20Movie.mp4";
    reply=dispatch(surface,byName); require(reply.readPlan && reply.readPlan->fileIndex==1,"percent-decoded exact file name resolves nonnumeric path"); reply.streamLease->close();
}

void testProgressAndCacheEvents()
{
    StreamProgressTracker tracker;
    StreamFileContext ctx; ctx.infoHash=QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); ctx.fileIndex=5; ctx.name="movie.mp4";
    ctx.offset=0; ctx.length=1024; ctx.pieceLength=256; ctx.realPieceLength=512; ctx.verifiedPieces={0,2}; ctx.cachedDestination=QStringLiteral("C:/cache/movie.mp4");
    auto opened=tracker.open(ctx);
    require(opened.events.size()==2,"first stream open emits created and initial progress");
    require(opened.events.at(0).kind==StreamEventKind::Created,"stream-created is first");
    require(opened.events.at(1).kind==StreamEventKind::Progress && std::abs(opened.events.at(1).progress-0.5)<1e-9,"initial progress uses missing piece accounting");
    require(opened.events.at(1).destination.isEmpty(),"progress destination remains undefined/empty before cache completion");
    require(opened.automaticSelection && opened.automaticSelection->from==0 && opened.automaticSelection->to==4,"automatic full-file selection uses verification-to-virtual ratio");

    auto events=tracker.verified(ctx.infoHash,ctx.fileIndex,9,ctx.cachedDestination); require(events.isEmpty(),"verification outside file pieces is ignored");
    events=tracker.verified(ctx.infoHash,ctx.fileIndex,1,ctx.cachedDestination); require(events.size()==1 && std::abs(events.front().progress-0.75)<1e-9,"relevant verification advances progress");
    events=tracker.verified(ctx.infoHash,ctx.fileIndex,3,ctx.cachedDestination);
    require(events.size()==3,"final verification emits progress plus scoped and global cached events");
    require(events.at(0).kind==StreamEventKind::Progress && std::abs(events.at(0).progress-1.0)<1e-9,"final progress reaches one");
    require(events.at(1).kind==StreamEventKind::Cached && !events.at(1).global,"scoped cached event precedes global cached event");
    require(events.at(2).kind==StreamEventKind::Cached && events.at(2).global,"global cached event is emitted second");
    require(events.at(1).destination==ctx.cachedDestination,"cached destination is published only at completion");
    require(tracker.open(ctx).events.isEmpty(),"repeat opens do not reinitialize __cacheEvents behavior");
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    testStatsOracleAndRemoval();
    testCreateAndGuessedFileAugmentation();
    testMediaRangeHeadAndOddInvalidFallback();
    testMediaQueriesRedirectAndFiltering();
    testProgressAndCacheEvents();
    std::cout << "TORRENT_HTTP_SURFACE_OK\n";
    return 0;
}
