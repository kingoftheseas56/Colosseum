// Phase 2 pure-logic gate — magnet build + metadata→priorities mapping.
// Verdict on exit code. No network, no engine.
#include "torrent/BookTorrentMagnet.h"
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ std::printf("FAIL: %s\n", msg); ++failures; } } while(0)

int main()
{
    const QString m = BookTorrentMagnet::buildMagnet("08ADA5A7A6183AAE1E09D831DF6748D566095A10");
    CHECK(m.startsWith("magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10"), "magnet prefix/lowercase");
    CHECK(m.contains("tracker.opentrackr.org"), "magnet carries default tracker");

    QJsonArray files;
    { QJsonObject o; o["index"]=0; o["name"]="pack/cover.jpg"; o["size"]=1234.0; files.append(o); }
    { QJsonObject o; o["index"]=1; o["name"]="pack/1984.epub"; o["size"]=1939288.0; files.append(o); }
    const QList<ManifestFile> mfs = BookTorrentMagnet::filesToManifest(files);
    CHECK(mfs.size()==2, "manifest size");
    CHECK(mfs[1].idx==1 && mfs[1].name=="pack/1984.epub" && mfs[1].length==1939288, "manifest field map");

    const QVector<int> prio = BookTorrentMagnet::pickToPriorities(1, 2);
    CHECK(prio.size()==2 && prio[0]==0 && prio[1]==4, "priorities vector");

    if (failures) { std::printf("%d CHECK(s) failed\n", failures); return 1; }
    std::printf("PASS: magnet + manifest + priorities helpers\n");
    return 0;
}
