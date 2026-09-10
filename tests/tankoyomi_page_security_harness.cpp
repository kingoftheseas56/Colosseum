#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QQueue>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include "tankoyomi_fake_network.h"
#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiConfigurationStore.h"
#include "engine/TankoyomiIdentity.h"

// Match the established cancellation harness seam. MSVC requires the private
// implementation and caller to share the same access-qualified translation unit.
#define private public
#include "engine/MangaDownloader.h"
#undef private
#include "engine/MangaDownloader.cpp"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ColosseumTankoyomiTests");
    QCoreApplication::setApplicationName("page-security-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &label) {
        qInfo().noquote() << (ok ? "ok" : "FAIL") << label;
        if (!ok) ++failures;
    };
    struct Case {
        QString name;
        QString url;
        QString address;
        QUrl redirect;
        int expectedRequests = 0;
        bool expectedSuccess = false;
        bool loop = false;
        bool unknownProvider = false;
        bool legacy = false;
    };
    const QList<Case> cases{
        {"plain HTTP", "http://cdn.example/a.jpg", "93.184.216.34", {}, 0, false},
        {"private literal", "https://127.0.0.1/a.jpg", "93.184.216.34", {}, 0, false},
        {"private IPv6", "https://[::1]/a.jpg", "93.184.216.34", {}, 0, false},
        {"localhost", "https://localhost/a.jpg", "93.184.216.34", {}, 0, false},
        {"userinfo", "https://user@cdn.example/a.jpg", "93.184.216.34", {}, 0, false},
        {"private DNS", "https://cdn.example/a.jpg", "10.2.3.4", {}, 0, false},
        {"empty DNS", "https://cdn.example/a.jpg", "", {}, 0, false},
        {"link-local DNS", "https://cdn.example/a.jpg", "169.254.169.254", {}, 0, false},
        {"public CDN", "https://cdn.example/a.jpg", "93.184.216.34", {}, 1, true},
        {"private redirect", "https://cdn.example/a.jpg", "93.184.216.34", QUrl("https://192.168.1.2/a.jpg"), 1, false},
        {"cross-CDN redirect", "https://cdn.example/a.jpg", "93.184.216.34", QUrl("https://other.example/final.jpg"), 2, true},
        {"relative redirect", "https://cdn.example/dir/a.jpg", "93.184.216.34", QUrl("../final.jpg"), 2, true},
        {"redirect loop", "https://cdn.example/a.jpg", "93.184.216.34", QUrl("/again.jpg"), 6, false, true},
        {"unknown provider", "https://cdn.example/a.jpg", "93.184.216.34", {}, 0, false, false, true},
        {"legacy raw", "https://cdn.example/a.jpg", "93.184.216.34", {}, 1, true, false, false, true}
    };
    int serial = 0;
    for (const bool thumbnail : {true, false}) {
        for (const Case &test : cases) {
            const QString label = (thumbnail ? "thumbnail: " : "download: ") + test.name;
            const QString rawId = QString::number(++serial);
            const QString chapter = test.legacy ? "01LEGACY-" + rawId
                : TankoyomiIdentity::qualifyChapter("en", test.unknownProvider ? "unknown" : "weebcentral", {{"id", rawId}});
            TankoyomiTest::Nam nam;
            QStringList resolvedHosts;
            bool canonicalUnchanged = true;
            MangaDownloader::Job *observedJob = nullptr;
            nam.respond = [&](const QNetworkRequest &, QNetworkAccessManager::Operation, int count) {
                if (observedJob)
                    canonicalUnchanged = canonicalUnchanged && observedJob->pages.first().imageUrl == test.url;
                TankoyomiTest::ReplySpec reply;
                reply.body = QByteArray::fromHex("ffd8ff") + QByteArray(2048, 'x');
                reply.contentType = "image/jpeg";
                if (!test.redirect.isEmpty() && (count == 1 || test.loop)) {
                    reply.status = 302;
                    reply.redirect = test.redirect;
                }
                return reply;
            };
            const auto registry = TankoyomiProviderRegistry::fromResource();
            TankoyomiConfigurationStore configuration(registry, directory.filePath(rawId + ".ini"));
            TankoyomiChapterService service(&nam, &configuration);
            MangaDownloader downloader(&nam, nullptr,
                [&](const QString &host, MangaImageHostResolver::LookupDone done) {
                    resolvedHosts.append(host);
                    done(test.address);
                }, {}, &service);
            int settlements = 0;
            bool success = false;
            QEventLoop loop;
            QTimer watchdog;
            watchdog.setSingleShot(true);
            QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
            watchdog.start(3000);
            PageInfo page; page.index = 0; page.imageUrl = test.url;
            if (thumbnail) {
                downloader.m_thumbInflight.insert(chapter);
                downloader.fetchThumbImage(chapter, page, [&](const QString &url, bool ok) {
                    ++settlements; success = ok && QUrl(url).isLocalFile(); loop.quit();
                }, 2);
            } else {
                auto *job = new MangaDownloader::Job;
                job->lifetime = std::make_shared<MangaDownloader::JobLifetime>();
                job->lifetime->job = job;
                job->chapterId = chapter;
                job->seriesId = QStringLiteral("mal:21");
                job->seriesTitle = QStringLiteral("One Piece");
                job->chapterLabel = QStringLiteral("Fixture");
                job->pages = {page}; job->files = QStringList{QString()}; job->total = 1;
                job->dir = directory.filePath("chapter-" + rawId);
                QDir().mkpath(job->dir);
                downloader.m_active.insert(chapter, job);
                observedJob = job;
                QObject::connect(&downloader, &MangaDownloader::finished, &loop, [&](const QString &) {
                    observedJob = nullptr; ++settlements;
                    success = downloader.localPages(chapter).size() == 1;
                    loop.quit();
                });
                QObject::connect(&downloader, &MangaDownloader::failed, &loop,
                                 [&](const QString &, const QString &) {
                    observedJob = nullptr; ++settlements; success = false; loop.quit();
                });
                downloader.pumpImages(job);
            }
            if (!settlements) loop.exec();
            check(settlements == 1 && success == test.expectedSuccess
                      && nam.requests.size() == test.expectedRequests, label);
            check(canonicalUnchanged, label + " preserves the canonical page URL");
            if (test.expectedSuccess && !nam.requests.isEmpty()) {
                const auto &request = nam.requests.last();
                const QString host = test.redirect.isEmpty() || test.redirect.isRelative()
                    ? QUrl(test.url).host() : test.redirect.host();
                check(request.url().host() == test.address && request.rawHeader("Host") == host.toUtf8()
                          && request.peerVerifyName() == host, label + " keeps logical Host and TLS identity");
                if (!test.redirect.isEmpty())
                    check(request.url().path() == QUrl(test.url).resolved(test.redirect).path(),
                          label + " resolves Location against the hostname URL");
            }
            if (!thumbnail && downloader.m_indexWriteInFlight) {
                QEventLoop indexLoop;
                QTimer::singleShot(3000, &indexLoop, &QEventLoop::quit);
                downloader.runWhenIndexIdle([&] { indexLoop.quit(); });
                if (downloader.m_indexWriteInFlight) indexLoop.exec();
            }
            if (!settlements && !thumbnail) downloader.cancelDownload(chapter);
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (const auto location : {QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        if (path.contains(QCoreApplication::applicationName())) QDir(path).removeRecursively();
    }
    qInfo() << (failures ? "TANKOYOMI_PAGE_SECURITY_FAIL" : "TANKOYOMI_PAGE_SECURITY_OK");
    return failures ? 1 : 0;
}
