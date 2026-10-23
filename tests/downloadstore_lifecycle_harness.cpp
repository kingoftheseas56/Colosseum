#include "player/downloadstore.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>
#include <memory>

namespace {

QByteArray patternedBytes(int size, int salt) {
    QByteArray bytes;
    bytes.resize(size);
    for (int i = 0; i < size; ++i)
        bytes[i] = char((i * 37 + salt * 19) % 251);
    return bytes;
}

class RangeFixture final : public QObject {
public:
    enum class Mode { HoldPrefix, HonorRange, IgnoreRange };

    explicit RangeFixture(QObject *parent = nullptr) : QObject(parent) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                auto *bytes = new QByteArray;
                auto parsed = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket, bytes, parsed]() {
                    if (*parsed)
                        return;
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n"))
                        return;
                    *parsed = true;
                    m_requests.append(*bytes);
                    const QByteArray request = *bytes;
                    delete bytes;
                    answer(socket, request);
                });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::AnyIPv4, 0); }
    QString url(const QString &path = QStringLiteral("/video.mp4"),
                const QString &host = QStringLiteral("127.0.0.1")) const {
        return QStringLiteral("http://%1:%2%3").arg(host).arg(m_server.serverPort()).arg(path);
    }
    void setResponse(Mode mode, const QByteArray &body, qint64 prefix = 0) {
        m_mode = mode;
        m_body = body;
        m_prefix = prefix;
    }
    int requestCount() const { return m_requests.size(); }
    QByteArray requestAt(int index) const {
        return index >= 0 && index < m_requests.size() ? m_requests.at(index) : QByteArray();
    }
    int statusAt(int index) const {
        return index >= 0 && index < m_statuses.size() ? m_statuses.at(index) : 0;
    }
    void dropHeldSocket() {
        if (m_held)
            m_held->abort();
    }
    bool finishHeldResponse() {
        if (!m_held)
            return false;
        const qint64 sent = qBound<qint64>(0, m_prefix, m_body.size());
        m_held->write(m_body.mid(int(sent)));
        m_held->flush();
        m_held->disconnectFromHost();
        m_held.clear();
        return true;
    }

private:
    void answer(QTcpSocket *socket, const QByteArray &request) {
        const QByteArray lower = request.toLower();
        const int rangePos = lower.indexOf("\r\nrange:");
        qint64 offset = -1;
        if (rangePos >= 0) {
            const int valueStart = rangePos + int(sizeof("\r\nrange:") - 1);
            const int valueEnd = lower.indexOf("\r\n", valueStart);
            const QByteArray value = lower.mid(valueStart, valueEnd - valueStart).trimmed();
            if (value.startsWith("bytes=")) {
                bool ok = false;
                const QByteArray range = value.mid(6);
                const int dash = range.indexOf('-');
                const qint64 parsed = range.left(dash).toLongLong(&ok);
                if (ok)
                    offset = parsed;
            }
        }

        if (m_mode == Mode::HoldPrefix) {
            m_statuses.append(200);
            const qint64 count = qBound<qint64>(0, m_prefix, m_body.size());
            QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: ")
                + QByteArray::number(m_body.size())
                + QByteArrayLiteral("\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n")
                + m_body.left(int(count));
            socket->write(response);
            socket->flush();
            m_held = socket;
            return;
        }

        const bool serveRange = m_mode == Mode::HonorRange && offset >= 0
                             && offset < m_body.size();
        if (serveRange) {
            m_statuses.append(206);
            const QByteArray suffix = m_body.mid(int(offset));
            const QByteArray header = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                + QByteArrayLiteral("Content-Range: bytes ") + QByteArray::number(offset)
                + '-' + QByteArray::number(m_body.size() - 1) + '/'
                + QByteArray::number(m_body.size())
                + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(suffix.size())
                + QByteArrayLiteral("\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n");
            socket->write(header + suffix);
        } else {
            m_statuses.append(200);
            const QByteArray header = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: ")
                + QByteArray::number(m_body.size())
                + QByteArrayLiteral("\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n");
            socket->write(header + m_body);
        }
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    Mode m_mode = Mode::HonorRange;
    QByteArray m_body;
    qint64 m_prefix = 0;
    QPointer<QTcpSocket> m_held;
    QList<QByteArray> m_requests;
    QList<int> m_statuses;
};

class Harness final : public QObject {
public:
    explicit Harness(QCoreApplication &app)
        : QObject(&app), m_app(app), m_server(this), m_store(this), m_poll(this), m_deadline(this) {
        m_poll.setInterval(10);
        QObject::connect(&m_poll, &QTimer::timeout, this, [this]() { tick(); });
        m_deadline.setSingleShot(true);
        QObject::connect(&m_deadline, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("timed out in phase %1").arg(int(m_phase)));
        });
        QObject::connect(&m_store, &DownloadStore::needResolve, this,
                         [this](const QString &id, const QString &, const QString &) {
            onNeedResolve(id);
        });
        QObject::connect(&m_store, &DownloadStore::libraryChanged, this,
                         [this]() { QTimer::singleShot(0, this, [this]() { tick(); }); });
    }

    bool start() {
        if (!m_temp.isValid())
            return fail(QStringLiteral("temporary directory unavailable"));
        if (!m_server.listen())
            return fail(QStringLiteral("loopback HTTP fixture failed to listen"));
        m_firstPath = m_temp.filePath(QStringLiteral("same-source.mp4"));
        m_currentPath = m_firstPath;
        m_phase = Phase::FirstPrefix;
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_firstBody, m_prefixSize);
        enqueue(m_firstId, m_firstPath, m_firstHash, m_firstFileIdx);
        m_deadline.start(12000);
        m_poll.start();
        return true;
    }

private:
    enum class Phase {
        FirstPrefix,
        FirstPause,
        FirstResume,
        FirstDone,
        MismatchPrefix,
        MismatchPause,
        MismatchResume,
        MismatchDone,
        IgnoreRangePrefix,
        IgnoreRangePause,
        IgnoreRangeResume,
        IgnoreRangeDone,
        ManualPrefix,
        ManualPaused,
        ManualResume,
        ManualDone,
        DirectPrefix,
        DirectDone,
        Finished,
    };

    bool fail(const QString &message) {
        std::fprintf(stderr, "FAIL: %s\n", message.toUtf8().constData());
        m_app.exit(1);
        return false;
    }

    void pass(const char *message) {
        ++m_passCount;
        std::printf("PASS: %s\n", message);
        std::fflush(stdout);
    }

    void enqueue(const QString &id, const QString &path,
                 const QString &infoHash, int fileIdx) {
        QVariantMap request{
            {QStringLiteral("id"), id},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), id},
            {QStringLiteral("outputPath"), path},
        };
        if (!infoHash.isEmpty()) {
            request.insert(QStringLiteral("infoHash"), infoHash);
            request.insert(QStringLiteral("fileIdx"), fileIdx);
        }
        m_store.enqueue(request);
    }

    bool feedTorrent(const QString &id, const QString &url,
                     const QString &infoHash, int fileIdx) {
        const bool invoked = QMetaObject::invokeMethod(
            &m_store, "feedTorrentUrl", Qt::DirectConnection,
            Q_ARG(QString, id), Q_ARG(QString, url),
            Q_ARG(QString, infoHash), Q_ARG(int, fileIdx));
        if (!invoked)
            m_store.feedUrl(id, url); // baseline fallback before the lifecycle API exists
        return invoked;
    }

    bool setForeground(bool foreground) {
        return QMetaObject::invokeMethod(
            &m_store, "setForeground", Qt::DirectConnection, Q_ARG(bool, foreground));
    }

    QVariantMap row(const QString &id) const {
        const QVariantList rows = m_store.jobs();
        for (const QVariant &value : rows) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == id)
                return candidate;
        }
        return {};
    }

    QString state(const QString &id) const {
        return row(id).value(QStringLiteral("state")).toString();
    }

    QByteArray readFile(const QString &path) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    void onNeedResolve(const QString &id) {
        ++m_resolveCount[id];
        if (id == m_firstId) {
            if (m_phase == Phase::FirstPrefix && m_resolveCount[id] == 1) {
                feedTorrent(id, m_server.url(QStringLiteral("/old-first.mp4")), m_firstHash, m_firstFileIdx);
            } else if (m_phase == Phase::FirstResume && m_resolveCount[id] == 2) {
                feedTorrent(id, m_server.url(QStringLiteral("/fresh-first.mp4")), m_firstHash, m_firstFileIdx);
                m_phase = Phase::FirstDone;
            }
        } else if (id == m_mismatchId) {
            if (m_phase == Phase::MismatchPrefix && m_resolveCount[id] == 1) {
                feedTorrent(id, m_server.url(QStringLiteral("/old-mismatch.mp4")), m_mismatchOldHash, m_mismatchOldFileIdx);
            } else if (m_phase == Phase::MismatchResume && m_resolveCount[id] == 2) {
                feedTorrent(id, m_server.url(QStringLiteral("/fresh-mismatch.mp4")), m_mismatchNewHash, m_mismatchNewFileIdx);
                m_phase = Phase::MismatchDone;
            }
        } else if (id == m_ignoreRangeId) {
            if (m_phase == Phase::IgnoreRangePrefix && m_resolveCount[id] == 1) {
                feedTorrent(id, m_server.url(QStringLiteral("/old-ignore-range.mp4")), m_ignoreHash, m_ignoreFileIdx);
            } else if (m_phase == Phase::IgnoreRangeResume && m_resolveCount[id] == 2) {
                feedTorrent(id, m_server.url(QStringLiteral("/fresh-ignore-range.mp4")), m_ignoreHash, m_ignoreFileIdx);
                m_phase = Phase::IgnoreRangeDone;
            }
        } else if (id == m_manualId) {
            if (m_phase == Phase::ManualPrefix && m_resolveCount[id] == 1) {
                feedTorrent(id, m_server.url(QStringLiteral("/old-manual.mp4")), m_manualHash, m_manualFileIdx);
            } else if (m_phase == Phase::ManualResume && m_resolveCount[id] == 2) {
                feedTorrent(id, m_server.url(QStringLiteral("/fresh-manual.mp4")), m_manualHash, m_manualFileIdx);
                m_phase = Phase::ManualDone;
            }
        } else if (id == m_directId && m_phase == Phase::DirectPrefix
                   && m_resolveCount[id] == 1) {
            m_store.feedSource(id, m_server.url(QStringLiteral("/external-direct.mp4"),
                                                QStringLiteral("127.0.0.2")), {});
        }
    }

    void beginLifecycleResume(Phase phase, RangeFixture::Mode responseMode,
                              const QByteArray &body) {
        m_server.setResponse(responseMode, body);
        m_phase = phase;
        if (!setForeground(true)) {
            fail(QStringLiteral("DownloadStore::setForeground(bool) is missing"));
            return;
        }
    }

    void pauseAndClose(const QString &id) {
        if (!setForeground(false)) {
            m_apiAvailable = false;
            std::printf("BASELINE: setForeground(bool) unavailable; simulating route close\n");
        } else {
            m_apiAvailable = true;
        }
        m_server.dropHeldSocket(); // simulates the old route/socket being retired
        if (m_apiAvailable) {
            m_store.feedSource(id, QStringLiteral("http://127.0.0.1:1/stale"), {});
            const QVariantMap paused = row(id);
            if (paused.value(QStringLiteral("state")).toString() != QStringLiteral("paused")) {
                fail(QStringLiteral("background transition did not auto-pause job %1 (state=%2)")
                         .arg(id, paused.value(QStringLiteral("state")).toString()));
                return;
            }
            if (!paused.value(QStringLiteral("url")).toString().isEmpty()) {
                fail(QStringLiteral("background pause retained a stale loopback URL for %1").arg(id));
                return;
            }
            if (!QFile::exists(m_paths.value(id) + QStringLiteral(".part"))) {
                fail(QStringLiteral("background pause deleted the partial file for %1").arg(id));
                return;
            }
            const QByteArray expectedPrefix = m_bodies.value(id).left(m_prefixSize);
            if (readFile(m_paths.value(id) + QStringLiteral(".part")) != expectedPrefix) {
                fail(QStringLiteral("background pause did not preserve exact partial bytes for %1").arg(id));
                return;
            }
            pass("background lifecycle pauses the real DownloadStore job and retains exact .part bytes");
        }
    }

    void tick() {
        if (m_phase == Phase::Finished)
            return;
        const qint64 partSize = QFileInfo(m_currentPath + QStringLiteral(".part")).size();
        switch (m_phase) {
        case Phase::FirstPrefix:
            if (partSize >= m_prefixSize) {
                m_currentPath = m_firstPath;
                m_bodies.insert(m_firstId, m_firstBody);
                m_paths.insert(m_firstId, m_firstPath);
                pauseAndClose(m_firstId);
                m_phase = Phase::FirstPause;
            }
            break;
        case Phase::FirstPause:
            if (m_apiAvailable) {
                if (state(m_firstId) != QStringLiteral("paused")) {
                    fail(QStringLiteral("job left paused state after route close"));
                    return;
                }
                if (m_server.requestCount() != 1)
                    return;
                beginLifecycleResume(Phase::FirstResume, RangeFixture::Mode::HonorRange, m_firstBody);
            } else if (state(m_firstId) == QStringLiteral("failed")
                       || !QFile::exists(m_firstPath + QStringLiteral(".part"))) {
                fail(QStringLiteral("baseline reproduced data loss: state=%1 partExists=%2; expected paused with %3 bytes")
                         .arg(state(m_firstId))
                         .arg(QFile::exists(m_firstPath + QStringLiteral(".part")) ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(m_prefixSize));
            }
            break;
        case Phase::FirstResume:
        case Phase::FirstDone:
            if (m_phase == Phase::FirstDone && QFile::exists(m_firstPath)
                && readFile(m_firstPath) == m_firstBody) {
                if (m_server.requestCount() < 2
                    || !m_server.requestAt(1).toLower().contains("range: bytes=" + QByteArray::number(m_prefixSize))) {
                    fail(QStringLiteral("same pinned hash/fileIdx did not Range-resume from the retained prefix"));
                    return;
                }
                if (m_server.statusAt(1) != 206) {
                    fail(QStringLiteral("same pinned source Range request did not receive HTTP 206"));
                    return;
                }
                if (!m_server.requestAt(1).startsWith("GET /fresh-first.mp4 ")) {
                    fail(QStringLiteral("resume reused the old route URL instead of the freshly resolved route"));
                    return;
                }
                pass("same pinned hash/fileIdx re-resolves to a fresh URL and completes via Range/206 byte-exactly");
                startMismatchCase();
            }
            break;
        case Phase::MismatchPrefix:
            if (partSize >= m_prefixSize) {
                m_currentPath = m_mismatchPath;
                m_bodies.insert(m_mismatchId, m_mismatchOldBody);
                m_paths.insert(m_mismatchId, m_mismatchPath);
                pauseAndClose(m_mismatchId);
                m_phase = Phase::MismatchPause;
            }
            break;
        case Phase::MismatchPause:
            if (m_apiAvailable && state(m_mismatchId) == QStringLiteral("paused")) {
                m_server.setResponse(RangeFixture::Mode::HonorRange, m_mismatchNewBody);
                m_phase = Phase::MismatchResume;
                if (!setForeground(true)) {
                    fail(QStringLiteral("setForeground(true) missing during identity mismatch case"));
                    return;
                }
            }
            break;
        case Phase::MismatchResume:
        case Phase::MismatchDone:
            if (m_phase == Phase::MismatchDone && QFile::exists(m_mismatchPath)
                && readFile(m_mismatchPath) == m_mismatchNewBody) {
                if (m_server.requestCount() < 4
                    || m_server.requestAt(3).toLower().contains("\r\nrange:")) {
                    fail(QStringLiteral("changed resolved hash/fileIdx appended unrelated partial bytes"));
                    return;
                }
                pass("changed resolved hash/fileIdx truncates old partial before a full clean download");
                startIgnoreRangeCase();
            }
            break;
        case Phase::IgnoreRangePrefix:
            if (partSize >= m_prefixSize) {
                m_currentPath = m_ignoreRangePath;
                m_bodies.insert(m_ignoreRangeId, m_ignoreBody);
                m_paths.insert(m_ignoreRangeId, m_ignoreRangePath);
                pauseAndClose(m_ignoreRangeId);
                m_phase = Phase::IgnoreRangePause;
            }
            break;
        case Phase::IgnoreRangePause:
            if (m_apiAvailable && state(m_ignoreRangeId) == QStringLiteral("paused")) {
                m_server.setResponse(RangeFixture::Mode::IgnoreRange, m_ignoreBody);
                m_phase = Phase::IgnoreRangeResume;
                if (!setForeground(true)) {
                    fail(QStringLiteral("setForeground(true) missing during 200 fallback case"));
                    return;
                }
            }
            break;
        case Phase::IgnoreRangeResume:
        case Phase::IgnoreRangeDone:
            if (m_phase == Phase::IgnoreRangeDone && QFile::exists(m_ignoreRangePath)
                && readFile(m_ignoreRangePath) == m_ignoreBody) {
                const QByteArray request = m_server.requestAt(5).toLower();
                if (!request.contains("\r\nrange: bytes=" + QByteArray::number(m_prefixSize))) {
                    fail(QStringLiteral("same-identity resume did not request Range before 200 fallback"));
                    return;
                }
                if (m_server.statusAt(5) != 200) {
                    fail(QStringLiteral("Range fallback fixture did not exercise HTTP 200"));
                    return;
                }
                pass("server 200 response to Range restarts honestly without duplicated prefix bytes");
                startManualPauseCase();
            }
            break;
        case Phase::ManualPrefix:
            if (partSize >= m_prefixSize) {
                m_store.pauseJob(m_manualId);
                m_server.dropHeldSocket();
                if (state(m_manualId) != QStringLiteral("paused")
                    || readFile(m_manualPath + QStringLiteral(".part")) != m_manualBody.left(m_prefixSize)) {
                    fail(QStringLiteral("manual pause did not preserve the live transfer prefix"));
                    return;
                }
                pass("manual pause preserves a live .part prefix");

                const int countBefore = m_resolveCount.value(m_manualId);
                if (!setForeground(false) || !setForeground(true)) {
                    fail(QStringLiteral("lifecycle API missing in manual-pause control"));
                    return;
                }
                const QVariantMap paused = row(m_manualId);
                if (paused.value(QStringLiteral("state")).toString() != QStringLiteral("paused")
                    || m_resolveCount.value(m_manualId) != countBefore
                    || !paused.value(QStringLiteral("url")).toString().isEmpty()) {
                    fail(QStringLiteral("manual pause resumed automatically or retained its retired route"));
                    return;
                }
                pass("manual pause remains paused and clears its retired route across lifecycle toggles");

                m_server.setResponse(RangeFixture::Mode::HonorRange, m_manualBody);
                m_phase = Phase::ManualResume;
                m_store.resumeJob(m_manualId);
            }
            break;
        case Phase::ManualResume:
        case Phase::ManualDone:
            if (m_phase == Phase::ManualDone && QFile::exists(m_manualPath)
                && readFile(m_manualPath) == m_manualBody) {
                const QByteArray request = m_server.requestAt(7).toLower();
                if (!request.startsWith("get /fresh-manual.mp4 ")
                    || !request.contains("\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                    || m_server.statusAt(7) != 206) {
                    fail(QStringLiteral("manual resume did not resolve fresh pinned source and Range-append"));
                    return;
                }
                pass("manual resume re-resolves the pinned source and completes Range/206 byte-exactly");
                startDirectCase();
            }
            break;
        case Phase::DirectPrefix:
            if (partSize >= m_prefixSize) {
                if (!setForeground(false)) {
                    fail(QStringLiteral("setForeground(false) missing in external direct source boundary control"));
                    return;
                }
                const QString directState = state(m_directId);
                if (directState != QStringLiteral("downloading")) {
                    fail(QStringLiteral("background lifecycle paused unrelated external direct HTTP job (state=%1)")
                             .arg(directState));
                    return;
                }
                pass("foreground loss leaves an unrelated external direct HTTP transfer active");
                if (!m_server.finishHeldResponse()) {
                    fail(QStringLiteral("external direct response socket was not held for completion"));
                    return;
                }
                m_phase = Phase::DirectDone;
            }
            break;
        case Phase::DirectDone:
            if (QFile::exists(m_directPath) && readFile(m_directPath) == m_directBody) {
                pass("external direct HTTP job completes byte-exactly while app is backgrounded");
                finish();
            }
            break;
        case Phase::Finished:
            break;
        }
    }

    void startMismatchCase() {
        m_mismatchPath = m_temp.filePath(QStringLiteral("mismatch.mp4"));
        m_currentPath = m_mismatchPath;
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_mismatchOldBody, m_prefixSize);
        m_phase = Phase::MismatchPrefix;
        enqueue(m_mismatchId, m_mismatchPath, m_mismatchOldHash, m_mismatchOldFileIdx);
    }

    void startIgnoreRangeCase() {
        m_ignoreRangePath = m_temp.filePath(QStringLiteral("ignore-range.mp4"));
        m_currentPath = m_ignoreRangePath;
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_ignoreBody, m_prefixSize);
        m_phase = Phase::IgnoreRangePrefix;
        enqueue(m_ignoreRangeId, m_ignoreRangePath, m_ignoreHash, m_ignoreFileIdx);
    }

    void startManualPauseCase() {
        m_manualPath = m_temp.filePath(QStringLiteral("manual.mp4"));
        m_currentPath = m_manualPath;
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_manualBody, m_prefixSize);
        m_phase = Phase::ManualPrefix;
        enqueue(m_manualId, m_manualPath, m_manualHash, m_manualFileIdx);
    }

    void startDirectCase() {
        m_directPath = m_temp.filePath(QStringLiteral("external-direct.mp4"));
        m_currentPath = m_directPath;
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_directBody, m_prefixSize);
        m_phase = Phase::DirectPrefix;
        enqueue(m_directId, m_directPath, QString(), -1);
    }

    void finish() {
        m_phase = Phase::Finished;
        m_poll.stop();
        m_deadline.stop();
        std::printf("RESULT: %d passed, 0 failed\n", m_passCount);
        m_app.quit();
    }

    QCoreApplication &m_app;
    QTemporaryDir m_temp;
    RangeFixture m_server;
    DownloadStore m_store;
    QTimer m_poll;
    QTimer m_deadline;
    Phase m_phase = Phase::FirstPrefix;
    bool m_apiAvailable = false;
    const qint64 m_prefixSize = 96 * 1024;
    const QString m_firstId = QStringLiteral("lifecycle-same-source-%1").arg(QCoreApplication::applicationPid());
    const QString m_firstHash = QString(40, QLatin1Char('a'));
    const int m_firstFileIdx = 2;
    const QByteArray m_firstBody = patternedBytes(512 * 1024, 1);
    const QString m_mismatchId = QStringLiteral("lifecycle-changed-source-%1").arg(QCoreApplication::applicationPid());
    const QString m_mismatchOldHash = QString(40, QLatin1Char('b'));
    const int m_mismatchOldFileIdx = 3;
    const QString m_mismatchNewHash = QString(40, QLatin1Char('c'));
    const int m_mismatchNewFileIdx = 8;
    const QByteArray m_mismatchOldBody = patternedBytes(512 * 1024, 2);
    const QByteArray m_mismatchNewBody = patternedBytes(448 * 1024, 3);
    const QString m_ignoreRangeId = QStringLiteral("lifecycle-ignore-range-%1").arg(QCoreApplication::applicationPid());
    const QString m_ignoreHash = QString(40, QLatin1Char('d'));
    const int m_ignoreFileIdx = 5;
    const QByteArray m_ignoreBody = patternedBytes(384 * 1024, 4);
    const QString m_manualId = QStringLiteral("lifecycle-manual-pause-%1").arg(QCoreApplication::applicationPid());
    const QString m_manualHash = QString(40, QLatin1Char('e'));
    const int m_manualFileIdx = 7;
    const QByteArray m_manualBody = patternedBytes(320 * 1024, 5);
    const QString m_directId = QStringLiteral("lifecycle-external-direct-%1").arg(QCoreApplication::applicationPid());
    const QByteArray m_directBody = patternedBytes(256 * 1024, 7);
    QString m_firstPath;
    QString m_mismatchPath;
    QString m_ignoreRangePath;
    QString m_manualPath;
    QString m_directPath;
    QString m_currentPath;
    QHash<QString, int> m_resolveCount;
    QHash<QString, QString> m_paths;
    QHash<QString, QByteArray> m_bodies;
    int m_passCount = 0;
};

class LegacyCompatHarness final : public QObject {
public:
    explicit LegacyCompatHarness(QCoreApplication &app)
        : QObject(&app), m_app(app), m_server(this), m_poll(this), m_deadline(this) {
        m_poll.setInterval(10);
        QObject::connect(&m_poll, &QTimer::timeout, this, [this]() { tick(); });
        m_deadline.setSingleShot(true);
        QObject::connect(&m_deadline, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("legacy compatibility case timed out in phase %1").arg(int(m_phase)));
        });
        createStore();
    }

    bool start() {
        if (!m_temp.isValid())
            return fail(QStringLiteral("temporary directory unavailable"));
        if (!m_server.listen())
            return fail(QStringLiteral("loopback HTTP fixture failed to listen"));
        m_path = m_temp.filePath(QStringLiteral("legacy-pinned.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_body, m_prefixSize);
        m_phase = Phase::Prefix;
        QVariantMap request{
            {QStringLiteral("id"), m_id},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("legacy compatibility")},
            {QStringLiteral("outputPath"), m_path},
            {QStringLiteral("infoHash"), m_hash},
            {QStringLiteral("fileIdx"), m_fileIdx},
        };
        m_store->enqueue(request);
        m_poll.start();
        m_deadline.start(12000);
        return true;
    }

private:
    enum class Phase { Prefix, Resume, Done, HashOnlyPrefix, HashOnlyResume, HashOnlyDone, Finished };

    bool fail(const QString &message) {
        std::fprintf(stderr, "FAIL: %s\n", message.toUtf8().constData());
        m_app.exit(1);
        return false;
    }

    void pass(const char *message) {
        ++m_passCount;
        std::printf("PASS: %s\n", message);
        std::fflush(stdout);
    }

    bool invokeFeed(const QString &url) {
        return QMetaObject::invokeMethod(
            m_store.get(), "feedTorrentUrl", Qt::DirectConnection,
            Q_ARG(QString, m_id), Q_ARG(QString, url),
            Q_ARG(QString, m_hash), Q_ARG(int, m_fileIdx));
    }

    void createStore() {
        m_store = std::make_unique<DownloadStore>();
        QObject::connect(m_store.get(), &DownloadStore::needResolve, this,
                         [this](const QString &id, const QString &, const QString &) {
            if (id == m_id) {
                ++m_resolveCount;
                const QString route = m_resolveCount == 1
                    ? m_server.url(QStringLiteral("/old-legacy.mp4"))
                    : m_server.url(QStringLiteral("/fresh-legacy.mp4"));
                if (!invokeFeed(route))
                    fail(QStringLiteral("feedTorrentUrl(QString,QString,QString,int) missing"));
                else if (m_resolveCount == 2)
                    m_phase = Phase::Done;
            } else if (id == m_hashOnlyId) {
                ++m_hashOnlyResolveCount;
                const QString route = m_hashOnlyResolveCount == 1
                    ? m_server.url(QStringLiteral("/old-hash-only.mp4"))
                    : m_server.url(QStringLiteral("/fresh-hash-only.mp4"));
                m_store->feedSource(id, route, {});
                if (m_hashOnlyResolveCount == 2)
                    m_phase = Phase::HashOnlyDone;
            }
        });
    }

    QVariantMap row(const QString &id) const {
        for (const QVariant &value : m_store->jobs()) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == id)
                return candidate;
        }
        return {};
    }

    QByteArray readFile(const QString &path) const {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    void startHashOnlyCase() {
        m_hashOnlyPath = m_temp.filePath(QStringLiteral("legacy-hash-only.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_hashOnlyBody, m_prefixSize);
        m_phase = Phase::HashOnlyPrefix;
        QVariantMap request{
            {QStringLiteral("id"), m_hashOnlyId},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("legacy hash-only compatibility")},
            {QStringLiteral("outputPath"), m_hashOnlyPath},
            {QStringLiteral("infoHash"), m_hashOnlyHash},
            {QStringLiteral("fileIdx"), -1},
        };
        m_store->enqueue(request);
    }

    bool convertQueueToLegacyShape(const QString &targetId) {
        const QString queuePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                + QStringLiteral("/videos/queue.json");
        QFile file(queuePath);
        if (!file.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("could not read persisted queue for old-schema simulation"));
        QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!document.isArray())
            return fail(QStringLiteral("persisted queue is not a JSON array"));
        QJsonArray jobs = document.array();
        bool found = false;
        for (qsizetype i = 0; i < jobs.size(); ++i) {
            QJsonObject job = jobs.at(i).toObject();
            if (job.value(QStringLiteral("request")).toObject()
                    .value(QStringLiteral("id")).toString() != targetId)
                continue;
            // Simulate the exact pre-lifecycle queue schema while preserving its
            // pinned request identity, paused state, outputPath, and .part file.
            job.remove(QStringLiteral("partialInfoHash"));
            job.remove(QStringLiteral("partialFileIdx"));
            jobs.replace(i, job);
            found = true;
            break;
        }
        if (!found)
            return fail(QStringLiteral("persisted paused job was not found in queue"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(QStringLiteral("could not write legacy queue shape"));
        const qint64 written = file.write(QJsonDocument(jobs).toJson(QJsonDocument::Compact));
        file.close();
        if (written <= 0)
            return fail(QStringLiteral("legacy queue rewrite wrote no bytes"));
        return true;
    }

    void tick() {
        if (m_phase == Phase::Finished)
            return;
        switch (m_phase) {
        case Phase::Prefix:
            if (QFileInfo(m_path + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            m_store->pauseJob(m_id);
            m_server.dropHeldSocket();
            if (row(m_id).value(QStringLiteral("state")).toString() != QStringLiteral("paused")
                || readFile(m_path + QStringLiteral(".part")) != m_body.left(m_prefixSize)) {
                fail(QStringLiteral("could not create exact pinned legacy partial before restart"));
                return;
            }
            pass("created and manually paused an exact-pinned transfer with a retained .part");
            m_store.reset();
            if (!convertQueueToLegacyShape(m_id))
                return;
            createStore();
            if (row(m_id).value(QStringLiteral("state")).toString() != QStringLiteral("paused")
                || readFile(m_path + QStringLiteral(".part")) != m_body.left(m_prefixSize)) {
                fail(QStringLiteral("legacy queue reconstruction did not preserve paused state and .part"));
                return;
            }
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_body);
            m_phase = Phase::Resume;
            m_store->resumeJob(m_id);
            break;
        case Phase::Resume:
        case Phase::Done:
            if (m_phase != Phase::Done || !QFile::exists(m_path)
                || readFile(m_path) != m_body)
                return;
            if (m_server.requestCount() < 2) {
                fail(QStringLiteral("fresh route request was not observed after old-schema reload"));
                return;
            }
            if (!m_server.requestAt(1).startsWith("GET /fresh-legacy.mp4 ")
                || !m_server.requestAt(1).toLower().contains(
                    "\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                || m_server.statusAt(1) != 206) {
                fail(QStringLiteral("legacy pinned .part lost source identity: expected fresh-route Range/206; got request=%1 status=%2")
                         .arg(QString::fromLatin1(m_server.requestAt(1).split('\r').value(0)))
                         .arg(m_server.statusAt(1)));
                return;
            }
            pass("legacy exact-pinned queue infers identity and resumes its retained prefix via fresh Range/206");
            startHashOnlyCase();
            break;
        case Phase::HashOnlyPrefix:
            if (QFileInfo(m_hashOnlyPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            m_store->pauseJob(m_hashOnlyId);
            m_server.dropHeldSocket();
            if (row(m_hashOnlyId).value(QStringLiteral("state")).toString() != QStringLiteral("paused")
                || readFile(m_hashOnlyPath + QStringLiteral(".part")) != m_hashOnlyBody.left(m_prefixSize)) {
                fail(QStringLiteral("could not create hash-only legacy partial before restart"));
                return;
            }
            pass("created and manually paused a hash-only transfer with a retained .part");
            m_store.reset();
            if (!convertQueueToLegacyShape(m_hashOnlyId))
                return;
            createStore();
            if (row(m_hashOnlyId).value(QStringLiteral("state")).toString() != QStringLiteral("paused")
                || readFile(m_hashOnlyPath + QStringLiteral(".part")) != m_hashOnlyBody.left(m_prefixSize)) {
                fail(QStringLiteral("hash-only legacy queue reconstruction lost paused state or partial"));
                return;
            }
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_hashOnlyBody);
            m_phase = Phase::HashOnlyResume;
            m_store->resumeJob(m_hashOnlyId);
            break;
        case Phase::HashOnlyResume:
        case Phase::HashOnlyDone:
            if (m_phase != Phase::HashOnlyDone || !QFile::exists(m_hashOnlyPath)
                || readFile(m_hashOnlyPath) != m_hashOnlyBody)
                return;
            if (m_server.requestCount() < 4) {
                fail(QStringLiteral("hash-only fresh route request was not observed"));
                return;
            }
            if (!m_server.requestAt(3).startsWith("GET /fresh-hash-only.mp4 ")
                || m_server.requestAt(3).toLower().contains("\r\nrange:")
                || m_server.statusAt(3) != 200) {
                fail(QStringLiteral("hash-only legacy job incorrectly appended without exact file index"));
                return;
            }
            pass("legacy hash-only job with fileIdx -1 truncates safely and downloads without Range");
            m_phase = Phase::Finished;
            m_poll.stop();
            m_deadline.stop();
            std::printf("RESULT: %d passed, 0 failed\n", m_passCount);
            std::fflush(stdout);
            m_app.quit();
            break;
        case Phase::Finished:
            break;
        }
    }

    QCoreApplication &m_app;
    QTemporaryDir m_temp;
    RangeFixture m_server;
    std::unique_ptr<DownloadStore> m_store;
    QTimer m_poll;
    QTimer m_deadline;
    Phase m_phase = Phase::Prefix;
    const qint64 m_prefixSize = 96 * 1024;
    const QString m_id = QStringLiteral("legacy-pinned-%1").arg(QCoreApplication::applicationPid());
    const QString m_hash = QString(40, QLatin1Char('f'));
    const int m_fileIdx = 9;
    const QByteArray m_body = patternedBytes(352 * 1024, 6);
    QString m_path;
    int m_resolveCount = 0;
    const QString m_hashOnlyId = QStringLiteral("legacy-hash-only-%1").arg(QCoreApplication::applicationPid());
    const QString m_hashOnlyHash = QString(40, QLatin1Char('1'));
    const QByteArray m_hashOnlyBody = patternedBytes(288 * 1024, 8);
    QString m_hashOnlyPath;
    int m_hashOnlyResolveCount = 0;
    int m_passCount = 0;
};

} // namespace

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ColosseumAudit"));
    const bool legacyCompatibility = app.arguments().contains(QStringLiteral("--legacy-compat"));
    QCoreApplication::setApplicationName(QStringLiteral("DownloadStoreLifecycle-%1-%2")
                                             .arg(legacyCompatibility ? QStringLiteral("legacy")
                                                                      : QStringLiteral("regular"))
                                             .arg(QCoreApplication::applicationPid()));

    if (legacyCompatibility) {
        LegacyCompatHarness harness(app);
        if (!harness.start())
            return 1;
        return app.exec();
    }
    Harness harness(app);
    if (!harness.start())
        return 1;
    return app.exec();
}
