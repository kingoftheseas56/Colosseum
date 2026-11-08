// Real MangaDownloader with deterministic network replies; no live providers or user data.
#include "engine/MangaDownloader.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <QFileInfo>
static int failures = 0;
#define CHECK(c, label) do { if (!(c)) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); } } while (0)
static bool waitFor(const std::function<bool()>& predicate, int timeout = 15000) {
    QElapsedTimer clock; clock.start();
    while (!predicate() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
    }
    if (!predicate()) std::fprintf(stderr, "fixture async wait expired after %lld ms\n", static_cast<long long>(clock.elapsed()));
    return predicate();
}
static QByteArray image() { QByteArray b(2048, 'x'); b.replace(0, 4, QByteArray::fromHex("89504e47")); return b; }
class Reply final : public QNetworkReply {
public:
    QByteArray body; qint64 offset = 0; bool ended = false; std::function<void()> settled;
    Reply(const QNetworkRequest& req, QObject* owner) : QNetworkReply(owner) {
        setRequest(req); setUrl(req.url()); setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void complete(int status, const QByteArray& bytes, const QByteArray& type = "image/png") {
        if (ended) return; ended = true; body = bytes;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, type);
        if (status >= 400) setError(ContentAccessDenied, QStringLiteral("fixture HTTP error"));
        if (settled) settled();
        setFinished(true); emit readyRead(); emit finished();
    }
    void networkFailure(QNetworkReply::NetworkError error) {
        setError(error, QStringLiteral("fixture network failure"));
        complete(0, {}, "");
    }
    void abort() override {
        if (ended) return; setError(OperationCanceledError, QStringLiteral("fixture cancelled"));
        complete(0, {});
    }
    qint64 bytesAvailable() const override { return body.size() - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* out, qint64 max) override {
        const auto n = qMin(max, body.size() - offset); if (n <= 0) return -1;
        std::memcpy(out, body.constData() + offset, size_t(n)); offset += n; return n;
    }
};
class Nam final : public QNetworkAccessManager {
public:
    QList<QPointer<Reply>> pending; QStringList starts; int active = 0; int high = 0;
    bool allowUnpinned = false;
    Reply* pendingPage(const QString& path) {
        for (auto reply : pending)
            if (reply && !reply->ended && reply->url().path() == path) return reply;
        return nullptr;
    }
    void finishImages() {
        const auto copy = pending;
        for (auto reply : copy) if (reply && !reply->ended) reply->complete(200, image());
    }
    bool settled() const { return active == 0; }
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& req, QIODevice*) override {
        auto* reply = new Reply(req, this);
        if (req.url().path().endsWith("/images")) {
            CHECK(req.rawHeader("Referer") == "https://weebcentral.com", "live manifest shares scraper Referer");
            CHECK(req.rawHeader("HX-Request").isEmpty(), "live manifest does not invent HTMX headers");
            const QString id = req.url().path().section('/', 2, 2);
            QByteArray html;
            for (int i = 0; i < 18; ++i)
                html += QString("<img src=\"https://cdn.example.test/%1/page_%2.png\">\n").arg(id).arg(i, 3, 10, QChar('0')).toUtf8();
            QTimer::singleShot(0, reply, [reply, html] { reply->complete(200, html, "text/html"); });
        } else {
            CHECK(req.rawHeader("Host") == "cdn.example.test"
                      || (allowUnpinned && req.rawHeader("Host").isEmpty()
                          && req.url().host() == "cdn.example.test"),
                  "existing IPv4 pin preserves original Host, or the ordinary unpinned DNS path");
            CHECK(req.rawHeader("Referer") == "https://weebcentral.com/", "live images share transport Referer");
            CHECK(req.rawHeader("User-Agent").contains("Chrome/152.0"), "live images use existing app User-Agent");
            starts.append(req.url().path()); pending.append(reply); ++active; high = qMax(high, active);
            reply->settled = [this] { --active; };
        }
        return reply;
    }
};
static qulonglong start(MangaDownloader& d, const QString& id) {
    qulonglong token = 0;
    CHECK(QMetaObject::invokeMethod(&d, "startLive", Qt::DirectConnection,
        Q_RETURN_ARG(qulonglong, token), Q_ARG(QString, id), Q_ARG(QString, QStringLiteral("series"))),
        "startLive is exposed to QML");
    return token;
}
static void focus(MangaDownloader& d, const QString& id, int page, qulonglong token) {
    CHECK(QMetaObject::invokeMethod(&d, "setLiveFocus", Qt::DirectConnection,
        Q_ARG(QString, id), Q_ARG(int, page), Q_ARG(qulonglong, token)), "setLiveFocus reaches scheduler");
}
static void stop(MangaDownloader& d, const QString& id, qulonglong token) {
    CHECK(QMetaObject::invokeMethod(&d, "stopLive", Qt::DirectConnection,
        Q_ARG(QString, id), Q_ARG(qulonglong, token)), "stopLive reaches lifecycle owner");
}
static QVariantList pages(MangaDownloader& d, const QString& id) {
    QVariantList result;
    CHECK(QMetaObject::invokeMethod(&d, "livePages", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantList, result), Q_ARG(QString, id)), "livePages projects complete page order");
    return result;
}
struct FakeClock {
    QList<int> delays;
    QList<std::function<void()>> callbacks;
    void schedule(int delay, QObject*, std::function<void()> callback) {
        delays.append(delay); callbacks.append(std::move(callback));
    }
    void advance() {
        const auto due = std::exchange(callbacks, {});
        for (const auto& callback : due) callback();
    }
};
static void failureAndRetry()
{
    for (const QString kind : {QStringLiteral("soft"), QStringLiteral("403"),
                               QStringLiteral("429"), QStringLiteral("dns")}) {
        Nam nam; FakeClock clock;
        nam.allowUnpinned = kind == "dns";
        MangaDownloader d(&nam, nullptr, [kind](const QString&, MangaImageHostResolver::LookupDone done) {
            done(kind == "dns" ? QString() : QStringLiteral("203.0.113.10"));
        }, {}, nullptr, [&clock](int delay, QObject* context, std::function<void()> callback) {
            clock.schedule(delay, context, std::move(callback));
        });
        const QString id = "failure-" + kind;
        const auto token = start(d, id);
        CHECK(waitFor([&] { return nam.active == 3; }), "failure fixture starts a three-page wave");
        const QString path = "/" + id + "/page_000.png";
        for (int attempt = 0; attempt < 3; ++attempt) {
            Reply* reply = nam.pendingPage(path);
            CHECK(reply != nullptr, "retry reissues the failed page instead of skipping it");
            if (!reply) break;
            if (kind == "dns") reply->networkFailure(QNetworkReply::HostNotFoundError);
            else reply->complete(kind == "soft" ? 200 : kind.toInt(), QByteArray(2048, 'h'), "text/html");
            if (attempt < 2) {
                CHECK(clock.callbacks.size() == 1, "one scheduled retry holds its existing slot");
                CHECK(clock.delays.last() == (attempt == 0 ? 2000 : 4000), "existing 2/4 second retry backoff is preserved");
                clock.advance();
            }
        }
        CHECK(d.liveStatus(id).value("state") == "paused", "exhausted transport pauses the live chapter");
        CHECK(clock.callbacks.isEmpty(), "three failed attempts do not schedule a fourth automatic request");
        CHECK(nam.starts.count(path) == 3, "exactly three automatic attempts before user action");
        nam.finishImages();
        CHECK(waitFor([&] { return !pages(d,id).at(2).toMap().value("pending").toBool(); }), "already flying adjacent pages may settle while paused");
        CHECK(nam.starts.size() == 5, "paused job does not send additional speculative pages");
        CHECK(pages(d,id).at(0).toMap().value("error") == "download_failed", "failed pending page projects a retryable error");
        d.retryLivePage(id, 0, token);
        CHECK(waitFor([&] { return nam.pendingPage(path) != nullptr; }), "explicit user Retry restarts the current failed page");
        nam.finishImages();
        CHECK(waitFor([&] { return !pages(d,id).at(0).toMap().value("pending").toBool(); }), "successful retry clears the pending image");
        CHECK(nam.high <= 3, "retry never exceeds the three-slot request budget");
        stop(d,id,token);
    }
}
static void cancellationAndArbitration()
{
    Nam nam; FakeClock clock;
    MangaDownloader d(&nam, nullptr, [](const QString&, MangaImageHostResolver::LookupDone done) {
        done(QStringLiteral("203.0.113.10"));
    }, {}, nullptr, [&clock](int delay, QObject* context, std::function<void()> callback) {
        clock.schedule(delay, context, std::move(callback));
    });
    const auto old = start(d, "cancelled");
    CHECK(waitFor([&] { return nam.active == 3; }), "cancellation fixture starts live requests");
    auto* first = nam.pendingPage("/cancelled/page_000.png");
    CHECK(first != nullptr, "cancelled fixture has a current page");
    if (first) first->complete(429, QByteArray(2048,'h'), "text/html");
    stop(d,"cancelled",old);
    const auto fresh = start(d,"cancelled");
    clock.advance();
    CHECK(waitFor([&] { return nam.active == 3; }), "same chapter can reopen after cancelling a backoff");
    CHECK(nam.starts.count("/cancelled/page_000.png") == 2, "stale retry callback cannot fetch into the reopened session");
    stop(d,"cancelled",old);
    CHECK(nam.active == 3, "stale stop token cannot cancel the new session");
    stop(d,"cancelled",fresh);
    CHECK(nam.active == 0, "close aborts only the live session's remaining requests");

    d.downloadChapter("permanent-active", "series", "Fixture", "Chapter 3");
    CHECK(waitFor([&] { return nam.active == 3; }), "ordinary permanent job starts normally");
    const auto token = start(d,"priority-live");
    const int startsBefore = nam.starts.size();
    nam.finishImages();
    CHECK(waitFor([&] { return nam.starts.contains("/priority-live/page_000.png"); }), "live opens after pre-existing permanent requests drain");
    CHECK(nam.starts.size() == startsBefore + 3, "no extra permanent wave overlaps live admission");
    CHECK(!nam.starts.contains("/permanent-active/page_003.png"), "permanent downloader pauses behind live reader");
    nam.finishImages();
    CHECK(waitFor([&] { return nam.starts.contains("/priority-live/page_005.png"); }), "live refills its own read-ahead window");
    nam.finishImages();
    CHECK(waitFor([&] { return !pages(d,"priority-live").at(5).toMap().value("pending").toBool(); }), "live window settles");
    CHECK(!nam.starts.contains("/permanent-active/page_003.png"), "permanent stays paused even while live is idle at its window");
    stop(d,"priority-live",token);
    CHECK(waitFor([&] { return nam.starts.contains("/permanent-active/page_003.png"); }), "closing live resumes permanent download automatically");
    CHECK(nam.high <= 3, "single permanent/live handoff never overlaps above three requests");
    d.cancelDownload("permanent-active");
    CHECK(waitFor([&] { return d.activeChapterJobs().isEmpty(); }), "fixture permanent cleanup drains");
}
static void cacheLifecycle(const QString& cache)
{
    const QString root = cache + "/manga-live";
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    for (int i = 0; i < 5; ++i) {
        const QString dir = root + "/old-" + QString::number(i);
        QDir().mkpath(dir);
        QFile manifest(dir + "/manifest.json"); CHECK(manifest.open(QIODevice::WriteOnly), "cache manifest fixture opens");
        manifest.write(QJsonDocument(QJsonObject{{"schemaVersion",1},{"chapterId",QString::number(i)}}).toJson());
        manifest.close();
        QFile access(dir + "/access.json"); CHECK(access.open(QIODevice::WriteOnly), "cache LRU fixture opens");access.write("{}");
        access.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-100 + i), QFileDevice::FileModificationTime);
        access.close();
    }
    QDir().mkpath(root + "/interrupted-no-manifest");
    {
        Nam nam;
        MangaDownloader d(&nam, nullptr, [](const QString&, MangaImageHostResolver::LookupDone done) { done("203.0.113.10"); });
        CHECK(waitFor([&] { return QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size() == 3; }), "startup removes incomplete cache folders and retains only three LRU chapters");
        CHECK(!QDir(root + "/old-0").exists() && !QDir(root + "/old-1").exists(), "startup evicts the two oldest chapters first");
        const auto token = start(d,"cache-current");
        CHECK(waitFor([&] { return nam.active == 3; }), "new live chapter opens after cache pruning");
        CHECK(QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size() == 3, "new open retains current plus two most recent chapters");
        CHECK(!QDir(root + "/old-2").exists(), "opening a fourth chapter evicts the next least-recently used chapter");
        const QString currentDir = QFileInfo(QUrl(pages(d,"cache-current").first().toMap().value("url").toString()).toLocalFile()).absolutePath();
        // Sparse/resize fixture: no 300-MiB payload is transferred or loaded into memory.
        QFile large(root + "/old-3/large.bin");
        CHECK(large.open(QIODevice::WriteOnly) && large.resize(301LL*1024*1024), "oversized retained-cache fixture created");
        large.close();
        nam.finishImages();
        CHECK(waitFor([&] { return !pages(d,"cache-current").first().toMap().value("pending").toBool(); }), "new page publication trims over-budget old cache first");
        CHECK(!QDir(root + "/old-3").exists() && QDir(currentDir).exists(), "byte-cap eviction protects the open chapter");
        stop(d,"cache-current",token);
        CHECK(QDir(currentDir).exists(), "closing retains reusable chapter files");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("manga-live-fixture-" + QUuid::createUuid().toString(QUuid::Id128));
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    {
        Nam nam;
        MangaDownloader d(&nam, nullptr, [](const QString&, MangaImageHostResolver::LookupDone done) {
            done(QStringLiteral("203.0.113.10"));
        });
        if (d.metaObject()->indexOfMethod("startLive(QString,QString)") < 0) {
            CHECK(false, "live chapter acquisition exists independently of permanent downloads");
        } else {
            const QString id = QStringLiteral("alpha");
            int permanentFinished = 0;
            QObject::connect(&d, &MangaDownloader::finished, &d, [&](const QString&) { ++permanentFinished; });
            const auto token = start(d, id);
            CHECK(token != 0, "live session has a nonzero cancellation identity");
            CHECK(waitFor([&] { return nam.active == 3; }), "first live wave uses three slots");
            CHECK(nam.starts == QStringList({"/alpha/page_000.png", "/alpha/page_001.png", "/alpha/page_002.png"}), "focus is first, followed by the next two pages");
            CHECK(start(d, id) == token, "duplicate active open reuses its session without duplicate requests");
            CHECK(pages(d, id).size() == 18, "all pages are projected before all bytes arrive");
            CHECK(pages(d, id).at(10).toMap().value("pending").toBool(), "undelivered pages explicitly stay pending");
            CHECK(d.localPages(id).isEmpty() && !d.isDownloaded(id), "live pages never make permanent Downloads ready");
            CHECK(d.activeChapterJobs().isEmpty() && d.downloadedChapters().isEmpty(), "live sessions do not appear in Downloads views");
            CHECK(!QFile::exists(data + "/manga/index.json"), "starting live reading does not create a permanent index");
            nam.finishImages();
            CHECK(waitFor([&] { return nam.starts.size() == 6; }), "window refills only to current page plus five");
            nam.finishImages();
            CHECK(waitFor([&] { return !pages(d,id).at(5).toMap().value("pending").toBool(); }), "six-page window reaches local readiness");
            CHECK(nam.starts.size() == 6, "bounded read-ahead stops without downloading the whole chapter");
            focus(d,id,12,token);
            CHECK(waitFor([&] { return nam.starts.size() == 9; }), "jump refills around new focus");
            CHECK(nam.starts.at(6) == "/alpha/page_012.png", "jump's current page is fetched first");
            CHECK(!nam.starts.contains("/alpha/page_006.png"), "obsolete queued gap pages are abandoned");
            nam.finishImages();
            CHECK(waitFor([&] { return nam.starts.size() == 12; }), "jump prefetch reaches next three pages");
            nam.finishImages();
            CHECK(waitFor([&] { return !pages(d,id).at(17).toMap().value("pending").toBool(); }), "jumped-to window is ready");
            CHECK(nam.high <= 3, "live acquisition never exceeds three simultaneous image requests");
            CHECK(permanentFinished == 0, "live acquisition emits no permanent finished signal");
            stop(d,id,token);
            const int beforeReopen = nam.starts.size();
            const auto second = start(d,id);
            CHECK(second != token, "reopen has a new session identity");
            CHECK(waitFor([&] { return pages(d,id).size() == 18 && !pages(d,id).at(0).toMap().value("pending").toBool(); }), "reopen reuses retained first-page cache");
            CHECK(nam.starts.size() == beforeReopen, "retained read-ahead is not downloaded twice");
            focus(d,id,7,token);
            CHECK(nam.starts.size() == beforeReopen, "stale focus token cannot change the reopened session");
            stop(d,id,second);
            d.downloadChapter(id, "series", "Fixture", "Chapter 1");
            CHECK(waitFor([&] { return nam.active > 0; }), "permanent download resumes after live close");
            for (int turn=0; turn<10 && permanentFinished==0; ++turn) {
                nam.finishImages(); waitFor([&] { return nam.active>0 || permanentFinished>0; });
            }
            CHECK(permanentFinished == 1 && d.isDownloaded(id), "explicit Download completes normally");
            CHECK(nam.starts.count("/alpha/page_000.png") == 1 && nam.starts.count("/alpha/page_012.png") == 1,
                  "permanent Download copies existing live pages instead of fetching them again");
            CHECK(d.localPages(id).size() == 18, "permanent completion contains every page");
            CHECK(waitFor([&] { return QFile::exists(data + "/manga/index.json"); }), "only explicit Download publishes the permanent index");
        }
    }
    failureAndRetry();
    cancellationAndArbitration();
    QThreadPool::globalInstance()->waitForDone();
    cacheLifecycle(cache);
    QThreadPool::globalInstance()->waitForDone();
    QDir(cache).removeRecursively(); QDir(data).removeRecursively();
    if (!failures) std::puts("MANGA_LIVE_READING_OK");
    return failures ? 1 : 0;
}
