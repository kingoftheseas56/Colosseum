#include "vinyl/VinylTorrentProvider.h"
#include "player/streamserver.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/TorrentIndexer.h"
#include "torrent/PirateBayIndexer.h"
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>
#include <QSettings>
#include <QTemporaryDir>

class InspectMusicRoutes : public TankorentSearchService {
public:
    explicit InspectMusicRoutes(QNetworkAccessManager *network) : TankorentSearchService(network) {}
    QStringList ids(const QString &type, const QString &source = QStringLiteral("all")) {
        const auto indexers = buildIndexersFor(type, source);
        QStringList result;
        for (auto *indexer : indexers) result.append(indexer->id());
        qDeleteAll(indexers);
        result.sort();
        return result;
    }
};

class MusicMockIndexer : public TorrentIndexer {
    Q_OBJECT
public:
    QList<TorrentResult> rows;
    bool stall = false;
    using TorrentIndexer::TorrentIndexer;
    QString id() const override { return QStringLiteral("mock"); }
    QString displayName() const override { return id(); }
    IndexerHealth health() const override { return IndexerHealth::Ok; }
    QDateTime lastSuccess() const override { return {}; }
    QString lastError() const override { return {}; }
    qint64 lastResponseMs() const override { return 0; }
    void search(const QString &, int, const QString &) override {
        if (!stall) emit searchFinished(rows); // exercise synchronous injected indexers
    }
};

class MusicMockSearch : public TankorentSearchService {
public:
    explicit MusicMockSearch(QNetworkAccessManager *network) : TankorentSearchService(network) {}
    QStringList media;
    QList<TorrentResult> rows;
    bool stall = false;
protected:
    QList<TorrentIndexer *> buildIndexersFor(const QString &type, const QString &) override {
        media.append(type);
        auto *indexer = new MusicMockIndexer(this);
        indexer->rows = rows;
        indexer->stall = stall;
        return {indexer};
    }
};

class MusicMockStream : public StreamServer {
public:
    QStringList manifestIds, hashes, cancelled;
    QString fileId;
    int fileIndex = -1;
    void requestManifest(const QString &id, const QString &hash) override {
        manifestIds.append(id); hashes.append(hash);
    }
    void requestFile(const QString &id, const QString &hash, int index) override {
        fileId = id; fileIndex = index;
        emit torrentStreamReady(id, QStringLiteral("http://127.0.0.1:11470/%1/%2").arg(hash).arg(index), hash, index);
    }
    void cancelRequest(const QString &id) override { cancelled.append(id); }
    void manifest(int attempt, const QVariantList &files) {
        emit torrentManifestReady(manifestIds.at(attempt), hashes.at(attempt), files);
    }
};

class TorrentProviderContracts : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
    QSettings::Format m_oldFormat = QSettings::defaultFormat();
    QString m_oldOrganization, m_oldApplication;
    static QVariantMap track() {
        return {{"artist", "Artist"}, {"album", "Album"}, {"title", "Song"}, {"trackNumber", 1}};
    }
    static QVariantMap file(int index = 7) {
        return {{"fileIdx", index}, {"path", "Artist/Album/01 Song.flac"}, {"length", 100000}};
    }
    static TorrentResult candidate(int number) {
        TorrentResult row;
        row.infoHash = QStringLiteral("%1").arg(number, 40, 16, QChar('0'));
        row.title = QStringLiteral("Artist Album"); row.seeders = 100 - number;
        return row;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        m_oldOrganization = QCoreApplication::organizationName();
        m_oldApplication = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumIsolatedTests"));
        QCoreApplication::setApplicationName(QStringLiteral("MusicTorrentRoutes"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settings.path());
    }
    void cleanupTestCase() {
        QSettings::setDefaultFormat(m_oldFormat);
        QCoreApplication::setOrganizationName(m_oldOrganization);
        QCoreApplication::setApplicationName(m_oldApplication);
    }
    void existingMediaRoutesAndCategoriesRemainStable() {
        QNetworkAccessManager network; InspectMusicRoutes search(&network);
        const QStringList three{QStringLiteral("exttorrents"), QStringLiteral("piratebay"), QStringLiteral("torrentscsv")};
        for (const auto &type : {QStringLiteral("books"), QStringLiteral("audiobooks"),
                                 QStringLiteral("comics"), QStringLiteral("music")})
            QCOMPARE(search.ids(type), three);
        // An unknown media type historically has no allowlist, so ALL four
        // general indexers are eligible (including Knaben). Preserve that path.
        const QStringList four{QStringLiteral("exttorrents"), QStringLiteral("knaben"),
                               QStringLiteral("piratebay"), QStringLiteral("torrentscsv")};
        QCOMPARE(search.ids(QStringLiteral("videos")), four);
        QCOMPARE(search.ids(QStringLiteral("unknown")), four);
        QCOMPARE(search.ids(QString()), four);
        QCOMPARE(search.ids(QStringLiteral("music"), QStringLiteral("knaben")), QStringList{QStringLiteral("knaben")});
        PirateBayIndexer pirateBay(&network);
        QCOMPARE(pirateBay.categoryFor(QStringLiteral("books")), QStringLiteral("601"));
        QCOMPARE(pirateBay.categoryFor(QStringLiteral("audiobooks")), QStringLiteral("102"));
        QCOMPARE(pirateBay.categoryFor(QStringLiteral("music")), QStringLiteral("100"));
        QVERIFY(pirateBay.categoryFor(QStringLiteral("videos")).isEmpty());
    }
    void exactFileUsesScopedStream() {
        QNetworkAccessManager network; MusicMockSearch search(&network); MusicMockStream stream;
        search.rows = {candidate(1)};
        VinylTorrentProvider provider(&search, &stream, nullptr, 1000);
        QSignalSpy completed(&provider, &VinylTorrentProvider::completed);
        QSignalSpy video(&stream, &StreamServer::streamReady);
        QSignalSpy downloads(&stream, &StreamServer::fetchReady);
        const int id = provider.resolve(track());
        QCOMPARE(completed.count(), 0);
        QTRY_COMPARE(stream.manifestIds.size(), 1);
        QCOMPARE(search.media, QStringList{QStringLiteral("music")});
        stream.manifest(0, {file()});
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toInt(), id);
        QVERIFY(completed.first().at(2).toString().isEmpty());
        const auto data = completed.first().at(1).toMap();
        QCOMPARE(data.value("fileIdx").toInt(), 7);
        QCOMPARE(data.value("provider").toString(), QStringLiteral("torrent"));
        QVERIFY(data.value("url").toString().endsWith(QStringLiteral("/7")));
        QCOMPARE(stream.fileIndex, 7);
        QCOMPARE(video.count(), 0); QCOMPARE(downloads.count(), 0);
    }
    void supersessionAndCancellationIgnoreLateReplies() {
        QNetworkAccessManager network; MusicMockSearch search(&network); MusicMockStream stream;
        search.rows = {candidate(1)};
        VinylTorrentProvider provider(&search, &stream, nullptr, 1000);
        QSignalSpy completed(&provider, &VinylTorrentProvider::completed);
        provider.resolve(track()); QTRY_COMPARE(stream.manifestIds.size(), 1);
        const int current = provider.resolve(track());
        stream.manifest(0, {file()});
        QCOMPARE(completed.count(), 0); QCOMPARE(stream.fileIndex, -1);
        QTRY_COMPARE(stream.manifestIds.size(), 2);
        stream.manifest(1, {file()});
        QCOMPARE(completed.count(), 1); QCOMPARE(completed.first().at(0).toInt(), current);
        provider.resolve(track()); QTRY_COMPARE(stream.manifestIds.size(), 3);
        provider.cancel(); // profile leaving uses this same retirement seam
        stream.manifest(2, {file()});
        QCOMPARE(completed.count(), 1);
        QVERIFY(stream.cancelled.contains(stream.manifestIds.at(2)));
    }
    void ambiguousCandidatesNeverPlayAndAttemptsAreBounded() {
        QNetworkAccessManager network; MusicMockSearch search(&network); MusicMockStream stream;
        for (int i = 1; i <= 8; ++i) search.rows.append(candidate(i));
        VinylTorrentProvider provider(&search, &stream, nullptr, 2000);
        QSignalSpy completed(&provider, &VinylTorrentProvider::completed);
        provider.resolve(track());
        for (int i = 0; i < 3; ++i) {
            QTRY_COMPARE(stream.manifestIds.size(), i + 1);
            stream.manifest(i, {file(7), file(8)});
        }
        QCOMPARE(stream.manifestIds.size(), 3); QCOMPARE(stream.fileIndex, -1);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.first().at(2).toString().isEmpty());
        QVERIFY(completed.first().at(1).toMap().isEmpty());
    }
    void stalledSearchHasOneTerminalDeadline() {
        QNetworkAccessManager network; MusicMockSearch search(&network); MusicMockStream stream;
        search.stall = true;
        VinylTorrentProvider provider(&search, &stream, nullptr, 40);
        QSignalSpy completed(&provider, &VinylTorrentProvider::completed);
        provider.resolve(track());
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.first().at(2).toString().isEmpty());
        QCOMPARE(stream.manifestIds.size(), 0);
        QTest::qWait(60); QCOMPARE(completed.count(), 1);
    }
    void stalledManifestDeadlineCancelsScopedTransport() {
        QNetworkAccessManager network; MusicMockSearch search(&network); MusicMockStream stream;
        search.rows = {candidate(1)};
        VinylTorrentProvider provider(&search, &stream, nullptr, 100);
        QSignalSpy completed(&provider, &VinylTorrentProvider::completed);
        provider.resolve(track());
        QTRY_COMPARE(stream.manifestIds.size(), 1);
        const QString token = stream.manifestIds.first();
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(stream.cancelled.contains(token));
        stream.manifest(0, {file()});
        QCOMPARE(completed.count(), 1); QCOMPARE(stream.fileIndex, -1);
    }
};

QTEST_GUILESS_MAIN(TorrentProviderContracts)
#include "torrent_provider_contracts.moc"
