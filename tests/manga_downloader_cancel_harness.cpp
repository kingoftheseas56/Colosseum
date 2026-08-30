#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QWaitCondition>

#include <cstdio>
#include <functional>
#include <memory>

// The harness exercises private lifecycle seams without adding a QML or network
// control path to production. The cleanup callback is the only injected behavior.
#define private public
#include "engine/MangaDownloader.h"
#undef private
// MSVC mangles member access into the symbol name, so calling a private method from
// this TU (seen public above) can never link against a separately compiled
// MangaDownloader.cpp. The implementation is compiled in here instead; its own
// header include is guard-no-op'd, so its definitions follow the access this TU
// already saw and the symbols match.
#include "engine/MangaDownloader.cpp"

namespace {

int failures = 0;

#define CHECK(condition, label) \
    do { if (!(condition)) { ++failures; std::printf("FAIL: %s\n", label); } } while (false)

class ImmediateErrorReply final : public QNetworkReply
{
public:
    ImmediateErrorReply(const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setOpenMode(QIODevice::ReadOnly);
        setError(QNetworkReply::ConnectionRefusedError, QStringLiteral("harness"));
        QTimer::singleShot(0, this, [this] { emit finished(); });
    }

    void abort() override {}

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class ImmediateErrorNam final : public QNetworkAccessManager
{
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        Q_UNUSED(operation)
        Q_UNUSED(outgoingData)
        return new ImmediateErrorReply(request, this);
    }
};

struct RemoveGate {
    QMutex mutex;
    QWaitCondition entered;
    QWaitCondition release;
    bool didEnter = false;
    bool released = false;

    bool remove(const QString &path)
    {
        {
            QMutexLocker lock(&mutex);
            didEnter = true;
            entered.wakeAll();
            while (!released)
                release.wait(&mutex);
        }
        return QDir(path).removeRecursively();
    }

    bool waitUntilEntered(QCoreApplication &app)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2000) {
            {
                QMutexLocker lock(&mutex);
                if (didEnter)
                    return true;
            }
            app.processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        QMutexLocker lock(&mutex);
        return didEnter;
    }

    void allow()
    {
        QMutexLocker lock(&mutex);
        released = true;
        release.wakeAll();
    }
};

bool waitFor(QCoreApplication &app, const std::function<bool()> &predicate,
             int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
}

MangaDownloader::Job *jobFor(MangaDownloader &downloader, const QString &id,
                             const QString &dir)
{
    auto *job = new MangaDownloader::Job;
    job->lifetime = std::make_shared<MangaDownloader::JobLifetime>();
    job->lifetime->job = job;
    job->chapterId = id;
    job->dir = dir;
    return job;
}

void createPayload(const QString &dir)
{
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/partial.bin"));
    file.open(QIODevice::WriteOnly);
    file.write("partial");
}

void testCleanupAndQueue(QCoreApplication &app, const QString &root)
{
    ImmediateErrorNam nam;
    RemoveGate gate;
    MangaDownloader downloader(&nam, &app, {},
                                [&gate](const QString &path) { return gate.remove(path); });

    const QString targetDir = root + QStringLiteral("/target");
    const QString queuedDir = root + QStringLiteral("/queued");
    createPayload(targetDir);

    auto *target = jobFor(downloader, QStringLiteral("target"), targetDir);
    downloader.m_active.insert(target->chapterId, target);
    auto *queued = jobFor(downloader, QStringLiteral("queued"), queuedDir);
    downloader.m_queue.enqueue(queued);

    int removed = 0;
    bool queuePromotedAtRemoved = false;
    QObject::connect(&downloader, &MangaDownloader::removed, &app,
                     [&](const QString &id) {
        if (id != QStringLiteral("target")) return;
        ++removed;
        queuePromotedAtRemoved = downloader.m_active.value(QStringLiteral("queued")) == queued
                                 && downloader.m_queue.isEmpty();
    });

    downloader.finalizeCancel(target);
    CHECK(gate.waitUntilEntered(app), "cancel cleanup reaches worker delete boundary");
    CHECK(removed == 0, "removed is delayed until cleanup completes");
    CHECK(downloader.m_active.contains(QStringLiteral("target")),
          "cancelled job remains active while cleanup is in flight");
    CHECK(downloader.m_queue.size() == 1, "queued job waits behind in-flight cleanup");

    gate.allow();
    CHECK(waitFor(app, [&] { return removed == 1; }),
          "cleanup emits removed exactly once");
    CHECK(queuePromotedAtRemoved, "queue promotes only after cleanup and before removed");
    CHECK(!QDir(targetDir).exists(), "cancelled chapter directory is eventually removed");

    // Let the fake queued scraper receive its deterministic error and clean up;
    // no network or external service is involved.
    waitFor(app, [&] { return !downloader.m_active.contains(QStringLiteral("queued")); });
}

void testSaveWatcherCancellation(QCoreApplication &app, const QString &root)
{
    ImmediateErrorNam nam;
    RemoveGate gate;
    MangaDownloader downloader(&nam, &app, {},
                                [&gate](const QString &path) { return gate.remove(path); });

    const QString dir = root + QStringLiteral("/save-cancel");
    createPayload(dir);
    auto *job = jobFor(downloader, QStringLiteral("save-cancel"), dir);
    job->inFlight = 1;
    downloader.m_active.insert(job->chapterId, job);

    int removed = 0;
    QObject::connect(&downloader, &MangaDownloader::removed, &app,
                     [&](const QString &id) {
        if (id == QStringLiteral("save-cancel")) ++removed;
    });

    downloader.saveImageAsync(job, 0, 0, QStringLiteral("page_000.jpg"),
                              QByteArray(2048, 'x'));
    job->cancelled = true; // cancel before the watcher completion is delivered

    CHECK(gate.waitUntilEntered(app), "save watcher cancellation reaches async cleanup");
    CHECK(removed == 0, "save watcher cancellation does not emit early removed");
    CHECK(downloader.m_active.contains(job->chapterId),
          "save watcher cancellation keeps job alive until cleanup callback");

    gate.allow();
    CHECK(waitFor(app, [&] { return removed == 1; }),
          "save watcher cancellation emits removed exactly once");
    CHECK(!QDir(dir).exists(), "save watcher cancellation removes partial directory");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir scratch;
    CHECK(scratch.isValid(), "temporary cleanup root is available");
    if (scratch.isValid()) {
        testCleanupAndQueue(app, scratch.path());
        testSaveWatcherCancellation(app, scratch.path());
    }
    if (failures == 0)
        std::printf("MANGA_DOWNLOADER_CANCEL_RUNTIME_OK\n");
    return failures == 0 ? 0 : 1;
}
