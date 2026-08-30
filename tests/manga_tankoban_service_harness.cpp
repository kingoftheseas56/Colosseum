// Tankoban volume-mode FAÇADE contract — the single object QML talks to.
//
// Composes Tasks 1–7 end-to-end and proves the façade OWNS terminal state across
// every path, with no stubbing of the terminal step. Driven through:
//   * a FAKE IMangaNyaaSearch  → deterministic per-volume candidates;
//   * the REAL MangaVolumeTorrentDownloader over a FAKE IMangaTorrentEngine (no
//     libtorrent) → a finished transfer is simulated by materialising the REAL
//     tests/fixtures/tankoban/tiny-volume.cbz as the resolved archive;
//   * the REAL MangaVolumePacker over a FAKE MangaScraper serving file:// JPEG
//     pages (Task 7 approach) → the WeebCentral terminal path is real, not stubbed;
//   * the REAL MangaVolumeIndex / MangaVolumeArchiveIngestor / MangaSynopsisEnricher
//     over QTemporaryDirs → so statusOf == "ready" is a genuine ingest/pack→index
//     result (real extracted/downloaded pages).
//
// Beyond the pinned pipeline it locks the five approved-with-fixes behaviours:
//   #1 a RESTART-replayed torrent that finishes ingests with ledger-recovered
//      provenance even when the series was never re-prepared (no orphan);
//   #2 remove() during an in-flight ingest ARRESTS the pipeline — no resurrection;
//   #3 a second concurrent acquisition for one volume is rejected;
//   #4 cancel/remove on an idle volume is a quiet no-op (no spurious `removed`);
//   #5 the packer-finished path converges through the façade to ONE ready record.
#include "engine/MangaResult.h"
#include "engine/MangaScraper.h"
#include "engine/MangaSeriesDetail.h"
#include "engine/MangaSynopsisEnricher.h"
#include "engine/MangaTankobanLogic.h"
#include "engine/MangaTankobanService.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"
#include "engine/MangaVolumePacker.h"
#include "torrent/MangaNyaaSource.h"
#include "torrent/MangaVolumeRequestLedger.h"
#include "torrent/MangaVolumeTorrentDownloader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

// A real, decodable 1x1 baseline JPEG (embedded — no committed binary fixture),
// lifted verbatim from manga_volume_packer_harness so the fake scraper serves a
// page the packer's magic-byte sniff accepts.
const unsigned char kJpeg[] = {
    0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
    0x00,0x01,0x00,0x00,0xff,0xdb,0x00,0x43,0x00,0x08,0x06,0x06,0x07,0x06,0x05,0x08,
    0x07,0x07,0x07,0x09,0x09,0x08,0x0a,0x0c,0x14,0x0d,0x0c,0x0b,0x0b,0x0c,0x19,0x12,
    0x13,0x0f,0x14,0x1d,0x1a,0x1f,0x1e,0x1d,0x1a,0x1c,0x1c,0x20,0x24,0x2e,0x27,0x20,
    0x22,0x2c,0x23,0x1c,0x1c,0x28,0x37,0x29,0x2c,0x30,0x31,0x34,0x34,0x34,0x1f,0x27,
    0x39,0x3d,0x38,0x32,0x3c,0x2e,0x33,0x34,0x32,0xff,0xdb,0x00,0x43,0x01,0x09,0x09,
    0x09,0x0c,0x0b,0x0c,0x18,0x0d,0x0d,0x18,0x32,0x21,0x1c,0x21,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0xff,0xc0,
    0x00,0x11,0x08,0x00,0x01,0x00,0x01,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,
    0x01,0xff,0xc4,0x00,0x1f,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,
    0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,
    0x05,0x04,0x04,0x00,0x00,0x01,0x7d,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,
    0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,
    0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,
    0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,
    0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,
    0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,
    0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,
    0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,
    0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xc4,0x00,0x1f,0x01,0x00,0x03,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
    0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x11,0x00,
    0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,
    0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,
    0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,
    0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,
    0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,0x88,
    0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,
    0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,
    0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,
    0xfa,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0xf1,
    0x1a,0x28,0xa2,0xb6,0x32,0x3f,0xff,0xd9
};

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QString fixturePath(const QString& name)
{
    return QStringLiteral(TANKOBAN_FIXTURES_DIR) + QLatin1Char('/') + name;
}

bool writeJpeg(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(reinterpret_cast<const char*>(kJpeg), sizeof(kJpeg))
           == static_cast<qint64>(sizeof(kJpeg));
}

template <typename Obj, typename Signal>
bool waitFor(Obj* obj, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;
    const QMetaObject::Connection c =
        QObject::connect(obj, signal, &loop, [&]() { fired = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, [&]() { loop.quit(); });
    loop.exec();
    QObject::disconnect(c);
    return fired;
}

// ── Fake torrent engine (no libtorrent) ──────────────────────────────────────
class FakeEngine : public IMangaTorrentEngine {
    Q_OBJECT
public:
    using IMangaTorrentEngine::IMangaTorrentEngine;

    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) override
    {
        lastPaused = paused; lastMagnet = magnetUri; lastSavePath = savePath;
        ++addMagnetCount;
        return QString();
    }
    void setFilePriorities(const QString&, const QVector<int>& p) override { priorities = p; }
    void startTorrent(const QString& infoHash, const QString&) override { startedHashes << infoHash.toLower(); }
    void removeTorrent(const QString& infoHash, bool deleteFiles) override { removed << qMakePair(infoHash.toLower(), deleteFiles); }
    QJsonArray torrentFiles(const QString& infoHash) const override { return known.value(infoHash.toLower()); }

    void emitMetadata(const QString& hash, const QJsonArray& files)
    {
        known.insert(hash.toLower(), files);
        emit metadataReady(hash, QStringLiteral("torrent"), 0, files);
    }
    void emitFinished(const QString& hash) { emit torrentFinished(hash); }
    void emitProgress(const QString& hash, float fraction)
    { emit torrentProgress(hash, fraction, 0, 0, 0, 0); }

    bool lastPaused = false;
    QString lastMagnet, lastSavePath;
    int addMagnetCount = 0;
    QVector<int> priorities;
    QStringList startedHashes;
    QList<QPair<QString, bool>> removed;
    QHash<QString, QJsonArray> known;
};

// ── Fake Nyaa search — returns whatever `next` holds, keyed to the real id ─────
class FakeNyaaSearch : public IMangaNyaaSearch {
    Q_OBJECT
public:
    using IMangaNyaaSearch::IMangaNyaaSearch;
    void search(const SeriesSnapshot& series, const QString& targetVolume) override
    {
        ++calls;
        if (failNext) {
            failNext = false;
            emit searchFailed(volumeId(series.seriesId, targetVolume),
                              QStringLiteral("provider down (fake)"));
            return;
        }
        emit searchSucceeded(volumeId(series.seriesId, targetVolume), next);
    }
    // Series-level search (catalogue-independence Slice 4): the façade passes its
    // own opaque key as series.seriesId — echo it back verbatim, same as the real
    // MangaNyaaSource does.
    void searchSeries(const SeriesSnapshot& series) override
    {
        ++calls;
        emit searchSucceeded(series.seriesId, next);
    }
    QList<MangaNyaaCandidate> next;
    int calls = 0;
    bool failNext = false;
};

// ── Fake WeebCentral scraper — replays weeb-pages.json as file:// JPEG pages ───
class FakeWeebScraper : public MangaScraper {
    Q_OBJECT
public:
    FakeWeebScraper(QNetworkAccessManager* nam, const QJsonObject& chapters,
                    const QString& imagesDir, QObject* parent = nullptr)
        : MangaScraper(nam, parent), m_chapters(chapters), m_imagesDir(imagesDir) {}
    QString sourceId() const override { return QStringLiteral("weebcentral"); }
    QString sourceName() const override { return QStringLiteral("WeebCentral"); }
    void search(const QString&, int) override {}
    void fetchChapters(const QString&) override {}
    void fetchDetail(const MangaResult&) override {}
    void fetchPages(const QString& chapterId) override
    {
        QList<PageInfo> pages;
        const QJsonArray arr =
            m_chapters.value(chapterId).toObject().value(QStringLiteral("pages")).toArray();
        int idx = 0;
        for (const QJsonValue& v : arr) {
            PageInfo p;
            p.index = idx++;
            p.imageUrl = QUrl::fromLocalFile(m_imagesDir + QLatin1Char('/') + v.toString()).toString();
            pages.append(p);
        }
        emit pagesReady(pages); // synchronous replay
    }
private:
    QJsonObject m_chapters;
    QString     m_imagesDir;
};

// A 3-file volume pack: volume "2" -> "Series v02.cbz" (index 1), "3" -> index 2.
QJsonArray packFiles()
{
    const char* names[] = {"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"};
    QJsonArray arr;
    for (int i = 0; i < 3; ++i) {
        QJsonObject o;
        o[QStringLiteral("index")] = i;
        o[QStringLiteral("name")]  = QString::fromLatin1(names[i]);
        o[QStringLiteral("size")]  = static_cast<qint64>(48 * 1024 * 1024);
        arr.append(o);
    }
    return arr;
}

QJsonArray oneFile(const QString& name)
{
    QJsonObject o;
    o[QStringLiteral("index")] = 0;
    o[QStringLiteral("name")]  = name;
    o[QStringLiteral("size")]  = static_cast<qint64>(48 * 1024 * 1024);
    QJsonArray arr; arr.append(o);
    return arr;
}

MangaNyaaCandidate makeCandidate(const QString& hash)
{
    MangaNyaaCandidate c;
    c.infoHash   = hash;
    c.magnetUri  = QStringLiteral("magnet:?xt=urn:btih:") + hash;
    c.title      = QStringLiteral("Series (Digital) (v01-03)");
    c.uploader   = QStringLiteral("danke");
    c.coverageLo = QStringLiteral("1");
    c.coverageHi = QStringLiteral("3");
    c.seeders    = 40;
    return c;
}

// ── Arc 18 M5 fakes: metainfo fetcher + resolver ────────────────────────────
// The fetcher "downloads" a .torrent by emitting its URL as the bytes; the
// resolver "decodes" those bytes by looking the URL up in a registered table.
// Together they drive the REAL service→indexer→store path deterministically.
class FakeMetaFetcher : public IMangaTorrentMetainfoFetcher {
    Q_OBJECT
public:
    using IMangaTorrentMetainfoFetcher::IMangaTorrentMetainfoFetcher;
    void fetch(const QString& url, const QString& requestKey) override
    {
        ++calls;
        if (failAll) {
            emit fetchFailed(requestKey, QStringLiteral("metainfo host unreachable (fake)"));
            return;
        }
        emit fetched(requestKey, url.toUtf8());
    }
    int calls = 0;
    bool failAll = false;
};

class FakeResolver : public MangaTankoban::IMangaTorrentMetainfoResolver {
public:
    bool resolve(const QByteArray& torrentBytes, MangaTankoban::TorrentMetainfo& out) override
    {
        const QString key = QString::fromUtf8(torrentBytes);
        if (!metas.contains(key))
            return false;
        out = metas.value(key);
        return true;
    }
    QHash<QString, MangaTankoban::TorrentMetainfo> metas;
};

MangaTankoban::TorrentMetainfo makeMeta(const QString& hash, std::initializer_list<QString> paths)
{
    MangaTankoban::TorrentMetainfo m;
    m.infoHash = hash;
    m.name = QStringLiteral("pack");
    int i = 0;
    for (const QString& p : paths) {
        MangaTankoban::TorrentMetainfoFile f;
        f.index = i++;
        f.path = p;
        f.size = 48 * 1024 * 1024;
        m.totalSize += f.size;
        m.files.append(f);
    }
    return m;
}

// Backdate a search key's freshness clock past the identity TTL — BOTH the
// search_state success stamp AND the volume's verified mappings — so the next
// lookup sees stale identity. The STORE API stamps "now" only; tests bend time
// in SQL, production never does. Own connection so it can run beside the
// service's open store (WAL).
void backdateLastSuccess(const QString& dbPath, const QString& searchKey, qint64 atMs)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("backdate_%1").arg(atMs));
    db.setDatabaseName(dbPath);
    require(db.open(), "backdate connection opens the identity db");
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE search_state SET last_success_at = ? WHERE search_key = ?"));
    q.addBindValue(atMs);
    q.addBindValue(searchKey);
    require(q.exec() && q.numRowsAffected() == 1, "backdate touched the search_state row");
    QSqlQuery m(db);
    m.prepare(QStringLiteral("UPDATE volume_mappings SET verified_at = ? WHERE volume_id = ?"));
    m.addBindValue(atMs);
    m.addBindValue(searchKey);
    require(m.exec() && m.numRowsAffected() >= 1, "backdate touched the mapping rows");
    db.close();
    QSqlDatabase::removeDatabase(db.connectionName());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // QSettings → a disposable Ini scope so modeEnabled starts empty and persists.
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "settings scope dir created");
    QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
    QCoreApplication::setApplicationName(QStringLiteral("TankobanServiceHarness"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    // ── Real collaborators over temp dirs ─────────────────────────────────────
    QTemporaryDir indexRoot, dlRoot, imagesDir, stagingRoot;
    require(indexRoot.isValid() && dlRoot.isValid() && imagesDir.isValid() && stagingRoot.isValid(),
            "temp roots created");

    // WeebCentral fixture pages the fake scraper serves over file://.
    QFile jf(fixturePath(QStringLiteral("weeb-pages.json")));
    require(jf.open(QIODevice::ReadOnly), "open weeb-pages.json fixture");
    const QJsonObject wcChapters =
        QJsonDocument::fromJson(jf.readAll()).object().value(QStringLiteral("chapters")).toObject();
    require(!wcChapters.isEmpty(), "weeb-pages.json has chapters");
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c10-p1.jpg")), "wrote c10 p1");
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c10-p2.jpg")), "wrote c10 p2");
    require(writeJpeg(imagesDir.path() + QStringLiteral("/weeb-c11-p1.jpg")), "wrote c11 p1");

    const QString saveRoot   = dlRoot.filePath(QStringLiteral("dl"));
    const QString ledgerPath = dlRoot.filePath(QStringLiteral("ledger.json"));

    FakeEngine engine;
    MangaVolumeTorrentDownloader transport(&engine, ledgerPath, saveRoot);
    MangaVolumeIndex index(indexRoot.path());
    MangaVolumeArchiveIngestor ingestor(&index);
    MangaSynopsisEnricher enricher(nullptr, indexRoot.filePath(QStringLiteral("synopsis.json"))); // cache-only
    QNetworkAccessManager packerNam;
    FakeWeebScraper scraper(&packerNam, wcChapters, imagesDir.path());
    MangaVolumePacker packer(&scraper, &packerNam, &index, stagingRoot.path());
    FakeNyaaSearch search;

    // The façade drives a REAL packer here so #5 (packer terminal path) is proven.
    MangaTankobanService service(&search, &transport, &index, &ingestor, &enricher, &packer);
    bool recoveryReadyChanged = false;
    QObject::connect(&service, &MangaTankobanService::recoveryReadyChanged, &app,
                     [&recoveryReadyChanged]() { recoveryReadyChanged = true; });
    require(service.recoveryReady(),
            "dependency-injected service starts ready (startup recovery is production-only)");
    require(!recoveryReadyChanged,
            "dependency-injected service does not emit a spurious recovery transition");

    // Signal trackers (no fatal handler — expected failures are asserted by delta).
    QStringList failures, finishedIds, removedIds;
    QObject::connect(&service, &MangaTankobanService::failed, &app,
        [&](const QString& id, const QString& reason) { failures << (id + QLatin1Char('|') + reason); });
    QObject::connect(&service, &MangaTankobanService::finished, &app,
        [&](const QString& id) { finishedIds << id; });
    QObject::connect(&service, &MangaTankobanService::removed, &app,
        [&](const QString& id) { removedIds << id; });
    QHash<QString, QVariantList> sources;
    QObject::connect(&service, &MangaTankobanService::sourcesReady, &app,
        [&](const QString& vid, const QVariantList& results) { sources.insert(vid, results); });

    // ── Prepare the snapshot (Task 1): vol1 has WeebCentral chapters; 2 & 3 none.
    const QVariantMap descriptor{{QStringLiteral("seriesId"), QStringLiteral("s1")},
                                 {QStringLiteral("title"), QStringLiteral("Series")},
                                 {QStringLiteral("author"), QStringLiteral("A. Mangaka")}};
    const QVariantList volumes{
        QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}, {QStringLiteral("cover"), QStringLiteral("a.jpg")}},
        QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}, {QStringLiteral("cover"), QStringLiteral("b.jpg")}},
        QVariantMap{{QStringLiteral("number"), QStringLiteral("3")}, {QStringLiteral("cover"), QStringLiteral("c.jpg")}},
    };
    const QVariantList chapters{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("wc-chapter-10")}, {QStringLiteral("volume"), QStringLiteral("1")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("wc-chapter-11")}, {QStringLiteral("volume"), QStringLiteral("1")}},
    };
    service.prepareSeries(descriptor, volumes, chapters);

    const QString vol1Id = volumeId(QStringLiteral("s1"), QStringLiteral("1"));
    const QString vol2Id = volumeId(QStringLiteral("s1"), QStringLiteral("2"));
    const QString vol3Id = volumeId(QStringLiteral("s1"), QStringLiteral("3"));

    // ── Pinned: mode preference ───────────────────────────────────────────────
    require(!service.modeEnabled(QStringLiteral("s1")), "missing preference is Off");
    service.setModeEnabled(QStringLiteral("s1"), true);
    require(service.modeEnabled(QStringLiteral("s1")), "preference persists per series");
    {
        MangaTankobanService fresh(&search, &transport, &index, &ingestor, &enricher, &packer);
        require(fresh.modeEnabled(QStringLiteral("s1")),
                "mode preference persists across a fresh service over the same settings scope");
    }

    // ── Every canonical volume returned even with no source ───────────────────
    require(service.volumesForSeries(QStringLiteral("s1")).size() == 3,
            "every canonical volume is returned by volumesForSeries even with no source");

    // ── Sources: volume 2 (no chapters) → one Nyaa card + disabled WeebCentral ─
    const QString hash2(40, QLatin1Char('a'));
    search.next = {makeCandidate(hash2)};
    service.searchSources(vol2Id);
    const QVariantList v2Sources = sources.value(vol2Id);
    require(!v2Sources.isEmpty(), "sourcesReady delivered candidates for volume 2");
    require(v2Sources.last().toMap().value(QStringLiteral("kind")).toString()
                == QStringLiteral("weebcentral"),
            "fallback always last");
    require(v2Sources.size() == 2, "one Nyaa card plus the WeebCentral fallback");
    {
        const QVariantMap weeb = v2Sources.last().toMap();
        require(!weeb.value(QStringLiteral("enabled")).toBool(),
                "chapterless volume disables the WeebCentral card");
        require(!weeb.value(QStringLiteral("reason")).toString().isEmpty(),
                "the disabled WeebCentral card carries a concrete reason");
    }

    // Volume 1 (has chapters): Nyaa returns nothing → WeebCentral card still last + enabled.
    search.next = {};
    service.searchSources(vol1Id);
    const QVariantList v1Sources = sources.value(vol1Id);
    require(!v1Sources.isEmpty()
                && v1Sources.last().toMap().value(QStringLiteral("kind")).toString()
                       == QStringLiteral("weebcentral"),
            "WeebCentral card present and last even when Nyaa returns nothing");
    require(v1Sources.last().toMap().value(QStringLiteral("enabled")).toBool(),
            "the WeebCentral card is enabled when the volume has mapped chapters");

    // ── Fix 1: an INCOMPLETE chapter map DISABLES the WeebCentral fallback ─────
    // Series s2 volume 1 has range [1,3] but chapters 2 & 3 are explicitly tagged
    // to volume 2, so only chapter 1 maps to volume 1. The fallback card is still
    // offered (last) but disabled with a concrete reason — a 1-of-3 volume must not
    // be presented as buildable.
    {
        const QVariantMap desc2{{QStringLiteral("seriesId"), QStringLiteral("s2")},
                                {QStringLiteral("title"), QStringLiteral("Series Two")}};
        const QVariantList vols2{
            QVariantMap{{QStringLiteral("number"), QStringLiteral("1")},
                        {QStringLiteral("chapterStart"), QStringLiteral("1")},
                        {QStringLiteral("chapterEnd"), QStringLiteral("3")}},
            QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}},
        };
        const QVariantList chaps2{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("d1")}, {QStringLiteral("number"), QStringLiteral("1")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("d2")}, {QStringLiteral("number"), QStringLiteral("2")}, {QStringLiteral("volume"), QStringLiteral("2")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("d3")}, {QStringLiteral("number"), QStringLiteral("3")}, {QStringLiteral("volume"), QStringLiteral("2")}},
        };
        service.prepareSeries(desc2, vols2, chaps2);
        const QString s2v1 = volumeId(QStringLiteral("s2"), QStringLiteral("1"));
        search.next = {};
        service.searchSources(s2v1);
        const QVariantList s2Sources = sources.value(s2v1);
        require(!s2Sources.isEmpty(), "sourcesReady delivered for s2 volume 1");
        const QVariantMap weeb = s2Sources.last().toMap();
        require(weeb.value(QStringLiteral("kind")).toString() == QStringLiteral("weebcentral"),
                "s2 fallback card present and last");
        require(weeb.value(QStringLiteral("chapterCount")).toInt() == 1,
                "the incomplete volume still carries its partial chapterCount");
        require(!weeb.value(QStringLiteral("enabled")).toBool(),
                "an incomplete chapter map disables the WeebCentral card");
        require(!weeb.value(QStringLiteral("reason")).toString().isEmpty(),
                "the incomplete-map card carries a concrete reason");
    }

    // ── downloadNyaa rejects a hash not among the cached candidates ───────────
    {
        const int before = failures.size();
        service.downloadNyaa(vol2Id, QString(40, QLatin1Char('f')));
        require(failures.size() == before + 1 && failures.last().startsWith(vol2Id),
                "downloadNyaa rejects an unknown infoHash, never accepts an arbitrary magnet");
        require(engine.addMagnetCount == 0, "a rejected hash never reaches the transport");
    }

    // ── Real search→choose→download→ingest→ready (volume 2) ───────────────────
    require(service.statusOf(vol2Id).value(QStringLiteral("state")).toString() == QStringLiteral("none"),
            "an un-acquired volume reports state none");
    service.downloadNyaa(vol2Id, hash2);
    require(engine.addMagnetCount == 1, "the chosen candidate reaches the transport");
    require(engine.lastPaused, "the candidate is added paused for metadata inspection");
    engine.emitMetadata(hash2, packFiles());
    require(engine.startedHashes.count(hash2) == 1, "the resolved volume torrent starts");

    const QString saveDir2 = saveRoot + QLatin1Char('/') + hash2;
    require(QDir().mkpath(saveDir2), "transport save dir created");
    require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                        saveDir2 + QStringLiteral("/Series v02.cbz")),
            "the real fixture is materialised as the finished archive");
    engine.emitFinished(hash2);
    require(waitFor(&service, &MangaTankobanService::finished, 30000),
            "facade emitted finished after a real ingest→index run");
    require(service.statusOf(vol2Id).value(QStringLiteral("state")).toString() == QStringLiteral("ready"),
            "one facade owns terminal state");
    require(service.localPages(vol2Id).size() == 3, "the ready volume exposes its three extracted pages");
    require(!failures.contains(vol2Id + QLatin1Char('|')), "no spurious failure on the happy path");

    // remove() clears the ready state (and emits removed exactly once).
    {
        const int rBefore = removedIds.size();
        service.remove(vol2Id);
        require(service.statusOf(vol2Id).value(QStringLiteral("state")).toString() == QStringLiteral("none"),
                "remove clears the ready state");
        require(removedIds.size() == rBefore + 1 && removedIds.last() == vol2Id,
                "remove of a published volume emits removed once");
    }

    // ── #4: cancel/remove on an idle volume is a quiet no-op ──────────────────
    {
        const int rBefore = removedIds.size();
        service.cancel(vol3Id);   // nothing in flight
        service.remove(vol3Id);   // nothing ready
        require(removedIds.size() == rBefore,
                "cancel/remove on a never-started volume is a quiet no-op (no spurious removed)");
    }

    // ── #3 + #2: concurrent reject, then remove() arrests an in-flight ingest ─
    const QString hash3(40, QLatin1Char('c'));
    search.next = {makeCandidate(hash3)};
    service.searchSources(vol3Id);
    service.downloadNyaa(vol3Id, hash3);   // first acquisition → in flight
    {
        const int before = failures.size();
        const int addBefore = engine.addMagnetCount;
        service.downloadNyaa(vol3Id, hash3);   // second concurrent request
        require(failures.size() == before + 1 && failures.last().contains(QStringLiteral("acquiring")),
                "a second concurrent acquisition for one volume is rejected");
        require(engine.addMagnetCount == addBefore, "the rejected concurrent request never re-adds the torrent");
    }
    engine.emitMetadata(hash3, packFiles());   // volume 3 -> "Series v03.cbz" (index 2)
    const QString saveDir3 = saveRoot + QLatin1Char('/') + hash3;
    require(QDir().mkpath(saveDir3), "vol3 save dir created");
    require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                        saveDir3 + QStringLiteral("/Series v03.cbz")),
            "vol3 archive materialised");
    engine.emitFinished(hash3);   // façade begins the async ingest (state: ingesting)
    require(service.statusOf(vol3Id).value(QStringLiteral("state")).toString() == QStringLiteral("ingesting"),
            "the finished transfer hands off to the ingest step");
    const int fin3Before = finishedIds.size();
    service.remove(vol3Id);       // ARREST mid-ingest
    require(removedIds.last() == vol3Id, "remove during ingest emits removed");
    // The async ingest still completes; the façade must undo the stray publish.
    require(waitFor(&ingestor, &MangaVolumeArchiveIngestor::finished, 30000),
            "the in-flight ingest actually completed (so the arrest is real, not a race win)");
    require(finishedIds.size() == fin3Before,
            "a removed volume never resurrects as finished after a stray ingest");
    require(service.statusOf(vol3Id).value(QStringLiteral("state")).toString() == QStringLiteral("none"),
            "a removed volume stays none even though its ingest completed");

    // ── #5: WeebCentral terminal path converges to ONE ready record ───────────
    const int fin1Before = finishedIds.size();
    service.compileWeebCentral(vol1Id);
    require(waitFor(&service, &MangaTankobanService::finished, 30000),
            "packer-finished converges to the facade's finished");
    require(finishedIds.size() == fin1Before + 1 && finishedIds.last() == vol1Id,
            "the packer terminal path emits finished(volumeId) exactly once");
    require(service.statusOf(vol1Id).value(QStringLiteral("state")).toString() == QStringLiteral("ready"),
            "the WeebCentral-built volume goes ready through the facade");
    {
        const QVariantList lp = service.localPages(vol1Id);
        require(lp.size() == 3, "WeebCentral volume publishes three pages");
        require(lp[0].toMap().value(QStringLiteral("group")).toInt() == 0
                    && lp[1].toMap().value(QStringLiteral("group")).toInt() == 0
                    && lp[2].toMap().value(QStringLiteral("group")).toInt() == 1,
                "chapter groups preserved as {0,0,1}");
    }

    // ── #1: a RESTART-replayed torrent that finishes ingests with ledger-recovered
    // provenance even though prepareSeries/downloadNyaa were never called ───────
    {
        QTemporaryDir rRoot, rDl;
        require(rRoot.isValid() && rDl.isValid(), "restart temp roots created");
        const QString rSaveRoot   = rDl.filePath(QStringLiteral("dl"));
        const QString rLedgerPath = rDl.filePath(QStringLiteral("ledger.json"));
        const QString rHash(40, QLatin1Char('b'));
        const QString rVolId = volumeId(QStringLiteral("s9"), QStringLiteral("5"));

        // Seed an active ledger row, as if a prior session was mid-download.
        {
            MangaVolumeRequestLedger seed(rLedgerPath);
            VolumeRequestRow row;
            row.volumeId     = rVolId;
            row.infoHash     = rHash;
            row.magnetUri    = QStringLiteral("magnet:?xt=urn:btih:") + rHash;
            row.seriesId     = QStringLiteral("s9");
            row.volumeNumber = QStringLiteral("5");
            row.savePath     = QString();   // transport computes saveDirFor(infoHash)
            row.state        = QStringLiteral("downloading");
            seed.upsert(row);
        }

        FakeEngine rEngine;
        MangaVolumeTorrentDownloader rTransport(&rEngine, rLedgerPath, rSaveRoot); // replays → re-adds paused
        require(rEngine.addMagnetCount == 1, "restart replay re-adds the persisted torrent");
        MangaVolumeIndex rIndex(rRoot.path());
        MangaVolumeArchiveIngestor rIngestor(&rIndex);
        MangaSynopsisEnricher rEnricher(nullptr, rRoot.filePath(QStringLiteral("syn.json")));
        FakeNyaaSearch rSearch;
        // NOTE: prepareSeries / downloadNyaa are deliberately NOT called.
        MangaTankobanService rService(&rSearch, &rTransport, &rIndex, &rIngestor, &rEnricher, nullptr);

        rEngine.emitMetadata(rHash, oneFile(QStringLiteral("Series v05.cbz")));  // resolves volume 5
        const QString rSaveDir = rSaveRoot + QLatin1Char('/') + rHash;
        require(QDir().mkpath(rSaveDir), "restart save dir created");
        require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                            rSaveDir + QStringLiteral("/Series v05.cbz")),
                "restart archive materialised");
        rEngine.emitFinished(rHash);
        require(waitFor(&rService, &MangaTankobanService::finished, 30000),
                "a resumed torrent that finishes ingests with ledger-recovered provenance");
        require(rService.statusOf(rVolId).value(QStringLiteral("state")).toString() == QStringLiteral("ready"),
                "the resumed volume becomes exactly one canonical ready record (no orphan)");
        require(rIndex.localPages(rVolId).size() == 3,
                "the ledger-recovered ready volume exposes its three extracted pages");
    }

    // ── BATCH: one chosen pack acquires N volumes (design 2026-07-30) ─────────
    // Measured in tests/manga_volume_pack_probe.md: on-target single-volume
    // torrents are essentially absent, so the pack IS the torrent route. The
    // load-bearing claim is that a batch is ONE torrent with N intents, never N
    // torrents — assert addMagnetCount, not just the end state.
    {
        QTemporaryDir bIndexRoot, bDlRoot;
        require(bIndexRoot.isValid() && bDlRoot.isValid(), "batch temp roots created");
        const QString bSaveRoot = bDlRoot.filePath(QStringLiteral("dl"));

        FakeEngine bEngine;
        MangaVolumeTorrentDownloader bTransport(&bEngine, bDlRoot.filePath(QStringLiteral("l.json")),
                                                bSaveRoot);
        MangaVolumeIndex bIndex(bIndexRoot.path());
        MangaVolumeArchiveIngestor bIngestor(&bIndex);
        MangaSynopsisEnricher bEnricher(nullptr, bIndexRoot.filePath(QStringLiteral("syn.json")));
        FakeNyaaSearch bSearch;
        MangaTankobanService bService(&bSearch, &bTransport, &bIndex, &bIngestor, &bEnricher, nullptr);

        QStringList bFailures, bFinished;
        QObject::connect(&bService, &MangaTankobanService::failed, &app,
            [&](const QString& id, const QString& r) { bFailures << (id + QLatin1Char('|') + r); });
        QObject::connect(&bService, &MangaTankobanService::finished, &app,
            [&](const QString& id) { bFinished << id; });

        bService.prepareSeries(descriptor, volumes, chapters);
        const QString b1 = volumeId(QStringLiteral("s1"), QStringLiteral("1"));
        const QString b2 = volumeId(QStringLiteral("s1"), QStringLiteral("2"));
        const QString b3 = volumeId(QStringLiteral("s1"), QStringLiteral("3"));

        // The batch searches only its FIRST volume — the engine has no range search.
        const QString bHash(40, QLatin1Char('d'));
        bSearch.next = {makeCandidate(bHash)};
        bService.searchSources(b1);

        // An unvalidated magnet must be refused for the WHOLE batch, loudly.
        {
            const int before = bFailures.size();
            bService.downloadNyaaBatch({b1, b2, b3}, QString(40, QLatin1Char('e')));
            require(bFailures.size() == before + 3,
                    "an unknown infoHash fails EVERY volume of the batch, never silently");
            require(bEngine.addMagnetCount == 0,
                    "a rejected batch hash never reaches the transport");
        }

        // The real batch: three volumes, ONE torrent.
        bService.downloadNyaaBatch({b1, b2, b3}, bHash);
        require(bEngine.addMagnetCount == 1,
                "a batch of three adds exactly ONE torrent, not three");
        for (const QString& id : {b1, b2, b3})
            require(bService.statusOf(id).value(QStringLiteral("state")).toString()
                        == QStringLiteral("resolving"),
                    "every volume in the batch enters the acquisition state");

        bEngine.emitMetadata(bHash, packFiles());   // v01, v02, v03 all inside
        require(bEngine.startedHashes.count(bHash) == 1, "the one batch torrent starts once");

        const QString bSaveDir = bSaveRoot + QLatin1Char('/') + bHash;
        require(QDir().mkpath(bSaveDir), "batch save dir created");
        for (const char* n : {"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"})
            require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                                bSaveDir + QLatin1Char('/') + QString::fromLatin1(n)),
                    "batch archive materialised");
        bEngine.emitFinished(bHash);

        while (bFinished.size() < 3 && waitFor(&bService, &MangaTankobanService::finished, 30000)) {}
        require(bFinished.size() == 3,
                "all THREE volumes of the batch finish from the one chosen pack");
        for (const QString& id : {b1, b2, b3})
            require(bService.statusOf(id).value(QStringLiteral("state")).toString()
                        == QStringLiteral("ready"),
                    "every volume of the batch becomes ready");
    }

    // ── BATCH: a pack that does NOT contain a requested volume ────────────────
    // The honest failure. A pack covering v01-v02 asked for v03 must leave v03
    // UN-ACQUIRED with a reason — never wrongly marked ready, which would put a
    // tile on the shelf with nothing behind it.
    {
        QTemporaryDir cIndexRoot, cDlRoot;
        require(cIndexRoot.isValid() && cDlRoot.isValid(), "short-pack temp roots created");
        const QString cSaveRoot = cDlRoot.filePath(QStringLiteral("dl"));

        FakeEngine cEngine;
        MangaVolumeTorrentDownloader cTransport(&cEngine, cDlRoot.filePath(QStringLiteral("l.json")),
                                                cSaveRoot);
        MangaVolumeIndex cIndex(cIndexRoot.path());
        MangaVolumeArchiveIngestor cIngestor(&cIndex);
        MangaSynopsisEnricher cEnricher(nullptr, cIndexRoot.filePath(QStringLiteral("syn.json")));
        FakeNyaaSearch cSearch;
        MangaTankobanService cService(&cSearch, &cTransport, &cIndex, &cIngestor, &cEnricher, nullptr);

        QStringList cFinished;
        QObject::connect(&cService, &MangaTankobanService::finished, &app,
            [&](const QString& id) { cFinished << id; });

        cService.prepareSeries(descriptor, volumes, chapters);
        const QString c1 = volumeId(QStringLiteral("s1"), QStringLiteral("1"));
        const QString c3 = volumeId(QStringLiteral("s1"), QStringLiteral("3"));

        const QString cHash(40, QLatin1Char('9'));
        cSearch.next = {makeCandidate(cHash)};
        cService.searchSources(c1);
        cService.downloadNyaaBatch({c1, c3}, cHash);

        // metadata carries ONLY volume 1 — volume 3 is not in this torrent
        cEngine.emitMetadata(cHash, oneFile(QStringLiteral("Series v01.cbz")));
        const QString cSaveDir = cSaveRoot + QLatin1Char('/') + cHash;
        require(QDir().mkpath(cSaveDir), "short-pack save dir created");
        require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                            cSaveDir + QStringLiteral("/Series v01.cbz")),
                "short-pack archive materialised");
        cEngine.emitFinished(cHash);
        require(waitFor(&cService, &MangaTankobanService::finished, 30000),
                "the volume the pack DOES contain still finishes");

        require(cService.statusOf(c3).value(QStringLiteral("state")).toString()
                    != QStringLiteral("ready"),
                "a volume missing from the chosen pack is NEVER marked ready");
        require(!cFinished.contains(c3),
                "a volume missing from the chosen pack never emits finished");
    }

    // ── Catalogue-independence Slice 3 (2026-08-20): seeding from CHAPTERLESS
    // records ─────────────────────────────────────────────────────────────────
    // Production now seeds TankobanVolumes exclusively from TankobanCatalog.volumes()
    // (MangaSeries.qml's _prepareTankoban) — rows carry only number/cover/title, no
    // chapterStart/chapterEnd, and chapters is ALWAYS []. Prove the full façade path
    // (search -> download -> ingest -> ready) still converges end-to-end with zero
    // chapters ever supplied, not just that prepareSeries accepts the shape (that pure-
    // logic half is already pinned by manga_tankoban_logic_harness's own "WHOLE SHELF
    // with no chapters" case).
    {
        QTemporaryDir dIndexRoot, dDlRoot;
        require(dIndexRoot.isValid() && dDlRoot.isValid(), "chapterless temp roots created");
        const QString dSaveRoot = dDlRoot.filePath(QStringLiteral("dl"));

        FakeEngine dEngine;
        MangaVolumeTorrentDownloader dTransport(&dEngine, dDlRoot.filePath(QStringLiteral("l.json")),
                                                dSaveRoot);
        MangaVolumeIndex dIndex(dIndexRoot.path());
        MangaVolumeArchiveIngestor dIngestor(&dIndex);
        MangaSynopsisEnricher dEnricher(nullptr, dIndexRoot.filePath(QStringLiteral("syn.json")));
        FakeNyaaSearch dSearch;
        MangaTankobanService dService(&dSearch, &dTransport, &dIndex, &dIngestor, &dEnricher, nullptr);
        QHash<QString, QVariantList> dSources;
        QObject::connect(&dService, &MangaTankobanService::sourcesReady, &app,
            [&](const QString& vid, const QVariantList& results) { dSources.insert(vid, results); });

        // Catalogue-shaped rows: number + cover + title only — no chapterStart/chapterEnd,
        // exactly TankobanCatalog::volumes()'s synthesized/overlaid shape (name->title
        // renamed at the QML layer, see MangaSeries.qml _prepareTankoban).
        const QVariantMap dDescriptor{{QStringLiteral("seriesId"), QStringLiteral("s4")},
                                      {QStringLiteral("title"), QStringLiteral("Catalogue Series")}};
        const QVariantList dVolumes{
            QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}, {QStringLiteral("cover"), QString()}, {QStringLiteral("title"), QString()}},
            QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}, {QStringLiteral("cover"), QStringLiteral("cover2.jpg")}, {QStringLiteral("title"), QStringLiteral("Real Volume Name")}},
        };
        dService.prepareSeries(dDescriptor, dVolumes, QVariantList{});   // chapters ALWAYS empty

        require(dService.volumesForSeries(QStringLiteral("s4")).size() == 2,
                "chapterless catalogue seeding still returns every canonical volume");

        const QString d1 = volumeId(QStringLiteral("s4"), QStringLiteral("1"));
        const QString dHash(40, QLatin1Char('7'));
        dSearch.next = {makeCandidate(dHash)};
        dService.searchSources(d1);
        require(dService.statusOf(d1).value(QStringLiteral("state")).toString() == QStringLiteral("none"),
                "a chapterless volume starts un-acquired, same as any other");
        // Catalogue-independence Slice 4: a chapterless volume carries zero mapped
        // WeebCentral chapters, so the (unrouted, QML never renders it — see
        // MangaTankobanSourcesPage.qml's nyaa-only filter) compile-from-chapters
        // card the façade still emits is never OFFERED as usable.
        {
            const QVariantList d1Sources = dSources.value(d1);
            require(!d1Sources.isEmpty(), "sourcesReady delivered for the chapterless volume");
            const QVariantMap dWeeb = d1Sources.last().toMap();
            require(dWeeb.value(QStringLiteral("kind")).toString() == QStringLiteral("weebcentral"),
                    "fallback card still last for a chapterless volume");
            require(!dWeeb.value(QStringLiteral("enabled")).toBool(),
                    "no compile path is ever offered for a chapterless volume");
            require(dWeeb.value(QStringLiteral("chapterCount")).toInt() == 0,
                    "a chapterless volume carries zero mapped chapters");
        }

        // ── Series-level search (Slice 4): the shelf-less page's "Search nyaa"
        // entry. No prepareSeries call for this key at all — proves the façade
        // needs no cached series/volume snapshot to run a series-mode search.
        {
            const QString seriesKey = QStringLiteral("series:s5");
            const QString eHash(40, QLatin1Char('8'));
            dSearch.next = {makeCandidate(eHash)};
            dService.searchSeriesSources(seriesKey, QStringLiteral("Never-Prepared Series"));
            require(dSources.contains(seriesKey),
                    "searchSeriesSources fires sourcesReady under the caller's own key, "
                    "with no prior prepareSeries for it");
            require(!dService.volumesForSeries(seriesKey).size(),
                    "a series-mode search key is never promoted into the canonical volume model");
        }

        dService.downloadNyaa(d1, dHash);
        require(dEngine.addMagnetCount == 1, "chapterless volume 1 reaches the transport via Nyaa");
        dEngine.emitMetadata(dHash, oneFile(QStringLiteral("Series v01.cbz")));
        require(dEngine.startedHashes.count(dHash) == 1, "the resolved chapterless-volume torrent starts");

        const QString dSaveDir = dSaveRoot + QLatin1Char('/') + dHash;
        require(QDir().mkpath(dSaveDir), "chapterless-volume save dir created");
        require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                            dSaveDir + QStringLiteral("/Series v01.cbz")),
                "chapterless-volume archive materialised");
        dEngine.emitFinished(dHash);
        require(waitFor(&dService, &MangaTankobanService::finished, 30000),
                "search->download->ingest->ready converges with zero chapters ever supplied");
        require(dService.statusOf(d1).value(QStringLiteral("state")).toString() == QStringLiteral("ready"),
                "the chapterless-seeded volume reaches ready through the ordinary Nyaa path");
        require(dService.localPages(d1).size() == 3,
                "the ready chapterless volume exposes its three extracted pages");
    }

    // ── Arc 18 M5: index-first lookup over a REAL store (trio-wired instance) ──
    // The service above runs WITHOUT the trio and behaved exactly as before —
    // this block wires the trio and proves the Torrentio behaviors end to end
    // through the REAL indexer + REAL SQLite store.
    {
        QTemporaryDir m5IndexRoot, m5DlRoot, m5TindexRoot;
        FakeEngine m5Engine;
        MangaVolumeTorrentDownloader m5Transport(
            &m5Engine, m5DlRoot.filePath(QStringLiteral("ledger.json")),
            m5DlRoot.filePath(QStringLiteral("dl")));
        MangaVolumeIndex m5Index(m5IndexRoot.path());
        MangaVolumeArchiveIngestor m5Ingestor(&m5Index);
        MangaSynopsisEnricher m5Enricher(nullptr, m5IndexRoot.filePath(QStringLiteral("syn.json")));
        FakeNyaaSearch m5Search;
        FakeResolver m5Resolver;
        FakeMetaFetcher m5Fetcher;
        const QString m5Db = m5TindexRoot.filePath(QStringLiteral("torrent-identity.db"));
        MangaTankoban::MangaTorrentIndex m5Tindex;
        require(m5Tindex.open(m5Db), "M5 identity store opens");
        MangaTankobanService m5Service(&m5Search, &m5Transport, &m5Index, &m5Ingestor,
                                       &m5Enricher, nullptr, &m5Resolver, &m5Fetcher,
                                       &m5Tindex);
        QHash<QString, QVariantList> m5Sources;
        QStringList m5Failures, m5FinishedIds;
        QObject::connect(&m5Service, &MangaTankobanService::sourcesReady, &app,
            [&](const QString& vid, const QVariantList& results) { m5Sources.insert(vid, results); });
        QObject::connect(&m5Service, &MangaTankobanService::failed, &app,
            [&](const QString& id, const QString& reason) { m5Failures << (id + QLatin1Char('|') + reason); });
        QObject::connect(&m5Service, &MangaTankobanService::finished, &app,
            [&](const QString& id) { m5FinishedIds << id; });

        const QVariantMap m5Desc{{QStringLiteral("seriesId"), QStringLiteral("s6")},
                                 {QStringLiteral("title"), QStringLiteral("Indexed Series")}};
        const QVariantList m5Vols{
            QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}},
            QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}},
            QVariantMap{{QStringLiteral("number"), QStringLiteral("3")}},
        };
        m5Service.prepareSeries(m5Desc, m5Vols, QVariantList{});
        const QString s6v1 = volumeId(QStringLiteral("s6"), QStringLiteral("1"));
        const QString s6v2 = volumeId(QStringLiteral("s6"), QStringLiteral("2"));
        const QString s6v3 = volumeId(QStringLiteral("s6"), QStringLiteral("3"));

        // 1) Cold volume: the first lookup refreshes THROUGH the index —
        //    discovery → metainfo fetch → verified mapping → merged cards.
        const QString packHash(40, QLatin1Char('b'));
        const QString packUrl = QStringLiteral("https://nyaa.fake/download/pack.torrent");
        MangaNyaaCandidate packCand = makeCandidate(packHash);
        packCand.torrentUrl = packUrl;
        m5Search.next = {packCand};
        m5Resolver.metas.insert(packUrl,
            makeMeta(packHash, {QStringLiteral("Indexed Series v01.cbz"),
                                QStringLiteral("Indexed Series v02.cbz")}));
        m5Service.searchSources(s6v1);
        {
            const QVariantList cards = m5Sources.value(s6v1);
            require(cards.size() == 2, "merged card set: one STRONG card plus the fallback");
            const QVariantMap strong = cards.first().toMap();
            require(strong.value(QStringLiteral("kind")).toString() == QStringLiteral("nyaa"),
                    "the STRONG card is a nyaa card");
            require(strong.value(QStringLiteral("indexed")).toBool(),
                    "the cold refresh produced an indexed card");
            require(strong.value(QStringLiteral("confidence")).toString() == QStringLiteral("STRONG"),
                    "file-verified identity renders as STRONG");
            require(strong.value(QStringLiteral("fileIndex")).toInt() == 0,
                    "the STRONG card carries the verified fileIndex");
            require(strong.value(QStringLiteral("filePath")).toString()
                        == QStringLiteral("Indexed Series v01.cbz"),
                    "the STRONG card carries the verified filePath");
            require(cards.last().toMap().value(QStringLiteral("kind")).toString()
                        == QStringLiteral("weebcentral"),
                    "the fallback stays last after a merged refresh");
        }
        require(m5Search.calls == 1 && m5Fetcher.calls == 1,
                "the cold lookup ran exactly one discovery and one metainfo fetch");

        // 2) Verified identity answers with ZERO provider queries.
        m5Search.calls = 0;
        m5Fetcher.calls = 0;
        m5Search.next = {};
        m5Sources.remove(s6v1);
        m5Service.searchSources(s6v1);
        {
            const QVariantList cards = m5Sources.value(s6v1);
            require(cards.size() == 2
                        && cards.first().toMap().value(QStringLiteral("confidence")).toString()
                               == QStringLiteral("STRONG"),
                    "a fresh verified identity answers instantly from the index");
        }
        require(m5Search.calls == 0 && m5Fetcher.calls == 0,
                "fresh verified identity costs zero Nyaa queries and zero fetches");

        // 3) Stale identity: instant cards first, ONE background refresh, and a
        //    failed refresh keeps the verified cards with no failed signal.
        backdateLastSuccess(m5Db, s6v1,
                            QDateTime::currentMSecsSinceEpoch() - 8 * 24 * 60 * 60 * 1000LL);
        m5Search.failNext = true;
        m5Sources.remove(s6v1);
        m5Service.searchSources(s6v1);
        require(m5Sources.value(s6v1).first().toMap().value(QStringLiteral("confidence")).toString()
                    == QStringLiteral("STRONG"),
                "stale identity still answers instantly from the index");
        require(m5Search.calls == 1, "stale identity fired exactly one background refresh");
        require(m5Failures.isEmpty(), "a failed refresh raises NO failed signal");
        require(m5Sources.value(s6v1).first().toMap().value(QStringLiteral("confidence")).toString()
                    == QStringLiteral("STRONG"),
                "verified cards survive the failed refresh untouched");

        // 4) No identity: a provider ERROR is never negative-cached; a
        //    successful EMPTY answer is (briefly).
        m5Search.calls = 0;
        m5Search.failNext = true;                       // lookup 1: provider error
        m5Service.searchSources(s6v3);
        require(m5Sources.value(s6v3).size() == 1
                    && m5Sources.value(s6v3).first().toMap().value(QStringLiteral("kind")).toString()
                           == QStringLiteral("weebcentral"),
                "a provider error with nothing indexed yields the fallback card only");
        m5Search.failNext = false;
        m5Search.next = {};
        m5Service.searchSources(s6v3);                  // lookup 2: allowed to retry
        require(m5Search.calls == 2,
                "a provider error was NOT negative-cached — the next lookup retried");
        m5Search.calls = 0;
        m5Service.searchSources(s6v3);                  // lookup 3: inside the negative TTL
        require(m5Search.calls == 0,
                "a successful empty answer IS negative-cached — no re-query inside the TTL");

        // 5) A title claim alone is NEVER STRONG: series s7's release title says
        //    v01-03 but the torrent's only archive is v01 — v2 must render the
        //    discovery card unverified.
        {
            const QVariantMap s7Desc{{QStringLiteral("seriesId"), QStringLiteral("s7")},
                                     {QStringLiteral("title"), QStringLiteral("Series Seven")}};
            m5Service.prepareSeries(s7Desc,
                {QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}},
                 QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}}},
                QVariantList{});
            const QString s7v2 = volumeId(QStringLiteral("s7"), QStringLiteral("2"));
            const QString s7Hash(40, QLatin1Char('e'));
            const QString s7Url = QStringLiteral("https://nyaa.fake/download/s7.torrent");
            MangaNyaaCandidate s7Cand = makeCandidate(s7Hash);   // title claims v01-03
            s7Cand.torrentUrl = s7Url;
            m5Search.next = {s7Cand};
            m5Resolver.metas.insert(s7Url,
                makeMeta(s7Hash, {QStringLiteral("Series Seven v01.cbz")}));
            m5Service.searchSources(s7v2);
            const QVariantList cards = m5Sources.value(s7v2);
            require(cards.size() == 2, "v2 renders the discovery card plus the fallback");
            const QVariantMap nyaa = cards.first().toMap();
            require(nyaa.value(QStringLiteral("kind")).toString() == QStringLiteral("nyaa"),
                    "the unverified discovery card is present");
            require(!nyaa.value(QStringLiteral("indexed")).toBool(),
                    "a title-claim-only candidate is not marked indexed");
            require(!nyaa.contains(QStringLiteral("confidence"))
                        || nyaa.value(QStringLiteral("confidence")).toString()
                               != QStringLiteral("STRONG"),
                    "a title claim alone never renders as STRONG");
        }

        // 6) The arbitrary-infoHash guard still holds under the index pipeline.
        const QString rogueHash(40, QLatin1Char('c'));
        m5Service.downloadNyaa(s6v2, rogueHash);
        require(m5Failures.size() == 1 && m5Failures.first().startsWith(s6v2 + QLatin1Char('|')),
                "an infoHash outside the candidate cache is still refused");

        // 7) An INDEX-cached hash validates with zero live search and downloads
        //    through the ordinary transport (m5Search.next is EMPTY here). v2's
        //    mapping came from the pack via v1's refresh — its own lookup is the
        //    instant verified answer, no discovery of its own.
        m5FinishedIds.clear();
        m5Search.calls = 0;
        m5Service.searchSources(s6v2);
        require(m5Search.calls == 0
                    && m5Sources.value(s6v2).first().toMap().value(QStringLiteral("confidence")).toString()
                           == QStringLiteral("STRONG"),
                "a sibling volume mapped by the pack answers from the index with no search");
        m5Service.downloadNyaa(s6v2, packHash);
        require(m5Failures.size() == 1, "the indexed candidate passed the cache guard");
        m5Engine.emitMetadata(packHash,
            {QJsonObject{{QStringLiteral("index"), 0},
                         {QStringLiteral("name"), QStringLiteral("Indexed Series v01.cbz")},
                         {QStringLiteral("size"), 48 * 1024 * 1024}},
             QJsonObject{{QStringLiteral("index"), 1},
                         {QStringLiteral("name"), QStringLiteral("Indexed Series v02.cbz")},
                         {QStringLiteral("size"), 48 * 1024 * 1024}}});
        require(m5Engine.startedHashes.count(packHash) == 1,
                "the indexed torrent starts after metadata resolves");
        const QString m5SaveDir = m5DlRoot.filePath(QStringLiteral("dl")) + QLatin1Char('/')
                              + packHash;
        require(QDir().mkpath(m5SaveDir), "M5 save dir created");
        require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")),
                            m5SaveDir + QStringLiteral("/Indexed Series v02.cbz")),
                "the verified v02 archive materialises");
        m5Engine.emitFinished(packHash);
        require(waitFor(&m5Service, &MangaTankobanService::finished, 30000),
                "an index-cached source downloads to a ready volume");
        require(m5Service.statusOf(s6v2).value(QStringLiteral("state")).toString()
                    == QStringLiteral("ready"),
                "the index-sourced volume reaches ready");
        require(m5Service.localPages(s6v2).size() == 3,
                "the index-sourced volume exposes its extracted pages");

        // 8) M6 end-to-end: live metadata CONTRADICTS the indexed identity →
        //    the intent fails closed, the mapping is demoted to
        //    NeedsRevalidation, and the next lookup no longer trusts it.
        const QVariantMap s8Desc{{QStringLiteral("seriesId"), QStringLiteral("s8")},
                                 {QStringLiteral("title"), QStringLiteral("Series Eight")}};
        m5Service.prepareSeries(s8Desc,
            {QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}}}, QVariantList{});
        const QString s8v1 = volumeId(QStringLiteral("s8"), QStringLiteral("1"));
        const QString s8Hash(40, QLatin1Char('f'));
        const QString s8Url = QStringLiteral("https://nyaa.fake/download/s8.torrent");
        MangaNyaaCandidate s8Cand = makeCandidate(s8Hash);
        s8Cand.torrentUrl = s8Url;
        m5Search.next = {s8Cand};
        m5Resolver.metas.insert(s8Url,
            makeMeta(s8Hash, {QStringLiteral("Series Eight v01.cbz")}));
        m5Service.searchSources(s8v1);   // indexes + verifies v1 at fileIndex 0
        require(m5Sources.value(s8v1).first().toMap().value(QStringLiteral("confidence")).toString()
                    == QStringLiteral("STRONG"),
                "s8 v1 verifies through the index first");

        const int failuresBefore = m5Failures.size();
        m5Service.downloadNyaa(s8v1, s8Hash);
        // The live swarm's metadata does NOT contain the indexed file anymore.
        m5Engine.emitMetadata(s8Hash,
            {QJsonObject{{QStringLiteral("index"), 0},
                         {QStringLiteral("name"), QStringLiteral("Series Eight v05.cbz")},
                         {QStringLiteral("size"), 48 * 1024 * 1024}}});
        require(m5Failures.size() == failuresBefore + 1,
                "the contradicted intent fails closed at the service level");
        {
            const QList<MangaTankoban::VolumeMapping> rows = m5Tindex.mappingsForVolume(s8v1);
            require(!rows.isEmpty()
                        && rows.first().status
                               == MangaTankoban::MappingStatus::NeedsRevalidation,
                    "the contradicted mapping is demoted to needs_revalidation");
        }
        m5Search.next = {};
        m5Sources.remove(s8v1);
        m5Service.searchSources(s8v1);
        {
            const QVariantList cards = m5Sources.value(s8v1);
            bool anyStrong = false;
            for (const QVariant& v : cards)
                anyStrong = anyStrong
                    || v.toMap().value(QStringLiteral("confidence")).toString()
                           == QStringLiteral("STRONG");
            require(!anyStrong,
                    "a demoted mapping never renders as STRONG on the next lookup");
        }

        // 9) M7: batch eligibility is the indexed FILE SET. The s9 pack's title
        //    claims v01-03, but its indexed files cover only v01+v02 — a batch
        //    within the verified set proceeds; a batch asking for v03 is
        //    refused WHOLE, title claim notwithstanding.
        const QVariantMap s9Desc{{QStringLiteral("seriesId"), QStringLiteral("s9")},
                                 {QStringLiteral("title"), QStringLiteral("Series Nine")}};
        m5Service.prepareSeries(s9Desc,
            {QVariantMap{{QStringLiteral("number"), QStringLiteral("1")}},
             QVariantMap{{QStringLiteral("number"), QStringLiteral("2")}},
             QVariantMap{{QStringLiteral("number"), QStringLiteral("3")}}},
            QVariantList{});
        const QString s9v1 = volumeId(QStringLiteral("s9"), QStringLiteral("1"));
        const QString s9v2 = volumeId(QStringLiteral("s9"), QStringLiteral("2"));
        const QString s9v3 = volumeId(QStringLiteral("s9"), QStringLiteral("3"));
        const QString s9Hash(40, QLatin1Char('1'));
        const QString s9Url = QStringLiteral("https://nyaa.fake/download/s9.torrent");
        MangaNyaaCandidate s9Cand = makeCandidate(s9Hash);   // title: "(v01-03)"
        s9Cand.torrentUrl = s9Url;
        m5Search.next = {s9Cand};
        m5Resolver.metas.insert(s9Url,
            makeMeta(s9Hash, {QStringLiteral("Series Nine v01.cbz"),
                              QStringLiteral("Series Nine v02.cbz")}));
        m5Service.searchSources(s9v1);   // indexes + verifies v1 AND v2

        const int m7FailuresBefore = m5Failures.size();
        m5Service.downloadNyaaBatch({s9v1, s9v2}, s9Hash);
        require(m5Failures.size() == m7FailuresBefore,
                "a batch inside the verified file set is accepted");
        m5Engine.emitMetadata(s9Hash,
            {QJsonObject{{QStringLiteral("index"), 0},
                         {QStringLiteral("name"), QStringLiteral("Series Nine v01.cbz")},
                         {QStringLiteral("size"), 48 * 1024 * 1024}},
             QJsonObject{{QStringLiteral("index"), 1},
                         {QStringLiteral("name"), QStringLiteral("Series Nine v02.cbz")},
                         {QStringLiteral("size"), 48 * 1024 * 1024}}});
        m5Engine.emitProgress(s9Hash, 0.5f);   // first tick turns both "downloading"
        require(m5Service.statusOf(s9v1).value(QStringLiteral("state")).toString()
                    == QStringLiteral("downloading")
                    && m5Service.statusOf(s9v2).value(QStringLiteral("state")).toString()
                           == QStringLiteral("downloading"),
                "the eligible batch downloads both volumes from the one torrent");
        require(m5Engine.addMagnetCount == 3,
                "three packs added total (s6, s8, s9) — the s9 batch rides ONE add");

        m5Service.downloadNyaaBatch({s9v1, s9v2, s9v3}, s9Hash);
        require(m5Failures.size() == m7FailuresBefore + 3,
                "a batch beyond the verified file set refuses every requested volume");
        {
            int refusals = 0;
            for (const QString& f : m5Failures.mid(m7FailuresBefore))
                if (f.contains(QStringLiteral("Batch refused")))
                    ++refusals;
            require(refusals == 3,
                    "each refused volume carries the batch-refusal reason");
        }
    }

    std::cout << "MANGA_TANKOBAN_SERVICE_OK\n";
    return 0;
}

#include "manga_tankoban_service_harness.moc"
