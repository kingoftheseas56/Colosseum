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
    enum class Mode { HoldPrefix, HoldRangePrefix, HonorRange, IgnoreRange,
                      WrongRangeOffset, MissingRangeOffset };

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
        const qint64 sent = qBound<qint64>(0, m_heldSentThrough, m_body.size());
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
            m_heldSentThrough = count;
            m_held = socket;
            return;
        }

        if (m_mode == Mode::HoldRangePrefix && offset >= 0 && offset < m_body.size()) {
            m_statuses.append(206);
            const QByteArray suffix = m_body.mid(int(offset));
            const qint64 count = qBound<qint64>(0, m_prefix, suffix.size());
            QByteArray response = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                + QByteArrayLiteral("Content-Range: bytes ") + QByteArray::number(offset)
                + '-' + QByteArray::number(m_body.size() - 1) + '/'
                + QByteArray::number(m_body.size())
                + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(suffix.size())
                + QByteArrayLiteral("\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n")
                + suffix.left(int(count));
            socket->write(response);
            socket->flush();
            m_heldSentThrough = offset + count;
            m_held = socket;
            return;
        }

        const bool serveRange = (m_mode == Mode::HonorRange
                                 || m_mode == Mode::WrongRangeOffset
                                 || m_mode == Mode::MissingRangeOffset)
                             && offset >= 0 && offset < m_body.size();
        if (serveRange) {
            m_statuses.append(206);
            const QByteArray suffix = m_body.mid(int(offset));
            QByteArray header = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n");
            if (m_mode != Mode::MissingRangeOffset) {
                const qint64 declaredOffset = m_mode == Mode::WrongRangeOffset ? offset + 1 : offset;
                header += QByteArrayLiteral("Content-Range: bytes ")
                    + QByteArray::number(declaredOffset) + '-'
                    + QByteArray::number(m_body.size() - 1) + '/'
                    + QByteArray::number(m_body.size()) + QByteArrayLiteral("\r\n");
            }
            header += QByteArrayLiteral("Content-Length: ") + QByteArray::number(suffix.size())
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
    qint64 m_heldSentThrough = 0;
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
                if (!QMetaObject::invokeMethod(
                        &m_store, "setBackgroundExecutionAllowed", Qt::DirectConnection,
                        Q_ARG(bool, true))) {
                    fail(QStringLiteral("service lease API missing in external direct boundary case"));
                    return;
                }
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

class BackgroundExecutionHarness final : public QObject {
public:
    explicit BackgroundExecutionHarness(QCoreApplication &app)
        : QObject(&app), m_app(app), m_server(this), m_store(this), m_poll(this), m_deadline(this) {
        m_poll.setInterval(10);
        QObject::connect(&m_poll, &QTimer::timeout, this, [this]() { tick(); });
        m_deadline.setSingleShot(true);
        QObject::connect(&m_deadline, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("background execution case timed out in phase %1")
                     .arg(int(m_phase)));
        });
        QObject::connect(&m_store, &DownloadStore::needResolve, this,
                         [this](const QString &id, const QString &, const QString &) {
            onNeedResolve(id);
        });
        QObject::connect(&m_store, &DownloadStore::resolutionReset,
                         this, [this]() { ++m_resolutionResetCount; });
    }

    bool start() {
        if (!m_temp.isValid())
            return fail(QStringLiteral("temporary directory unavailable"));
        if (!m_server.listen())
            return fail(QStringLiteral("loopback HTTP fixture failed to listen"));
        if (!setForeground(false))
            return fail(QStringLiteral("DownloadStore::setForeground(bool) is missing"));

        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_firstBody, m_prefixSize);
        enqueue(m_firstId, m_firstPath, QString(), -1);
        enqueue(m_secondId, m_secondPath, QString(), -1);
        if (state(m_firstId) != QStringLiteral("queued")
            || state(m_secondId) != QStringLiteral("queued")
            || m_resolveCount.value(m_firstId) != 0) {
            return fail(QStringLiteral("hidden jobs without a service lease were promoted"));
        }
        pass("hidden queued jobs wait without a service lease");

        m_phase = Phase::FirstPrefix;
        m_leaseApiAvailable = setBackgroundExecutionAllowed(true);
        m_deadline.start(25000);
        m_poll.start();
        return true;
    }

private:
    enum class Phase {
        FirstPrefix,
        FirstDone,
        SecondPrefix,
        SecondDone,
        TorrentPrefix,
        TorrentPaused,
        TorrentDone,
        DirectPrefix,
        DirectPaused,
        DirectDone,
        ManualPrefix,
        ManualDone,
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

    bool setForeground(bool foreground) {
        return QMetaObject::invokeMethod(
            &m_store, "setForeground", Qt::DirectConnection, Q_ARG(bool, foreground));
    }

    bool setBackgroundExecutionAllowed(bool allowed) {
        return QMetaObject::invokeMethod(
            &m_store, "setBackgroundExecutionAllowed", Qt::DirectConnection,
            Q_ARG(bool, allowed));
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

    bool feedTorrent(const QString &id, const QString &url) {
        return QMetaObject::invokeMethod(
            &m_store, "feedTorrentUrl", Qt::DirectConnection,
            Q_ARG(QString, id), Q_ARG(QString, url), Q_ARG(QString, m_torrentHash),
            Q_ARG(int, m_torrentFileIdx));
    }

    QVariantMap row(const QString &id) const {
        for (const QVariant &value : m_store.jobs()) {
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
        const int count = ++m_resolveCount[id];
        if (id == m_firstId && count == 1) {
            m_store.feedSource(id, m_server.url(QStringLiteral("/lease-first.mp4"),
                                                QStringLiteral("127.0.0.2")), {});
        } else if (id == m_secondId && count == 1) {
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_secondBody, m_prefixSize);
            m_store.feedSource(id, m_server.url(QStringLiteral("/lease-second.mp4"),
                                                QStringLiteral("127.0.0.2")), {});
        } else if (id == m_torrentId) {
            if (count == 1) {
                m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_torrentBody, m_prefixSize);
                if (!feedTorrent(id, m_server.url(QStringLiteral("/old-torrent-route.mp4"))))
                    fail(QStringLiteral("feedTorrentUrl missing for first torrent route"));
            } else if (count == 2) {
                m_server.setResponse(RangeFixture::Mode::HonorRange, m_torrentBody);
                if (!feedTorrent(id, m_server.url(QStringLiteral("/fresh-torrent-route.mp4"))))
                    fail(QStringLiteral("feedTorrentUrl missing for refreshed torrent route"));
            }
        } else if (id == m_directId && count == 1) {
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_directBody, m_prefixSize);
            m_store.feedSource(id, m_server.url(QStringLiteral("/lease-loss-direct.mp4"),
                                                QStringLiteral("127.0.0.2")), {});
        } else if (id == m_manualId && count == 1) {
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_manualBody, m_prefixSize);
            m_store.feedSource(id, m_server.url(QStringLiteral("/manual-lease.mp4"),
                                                QStringLiteral("127.0.0.2")), {});
        }
    }

    void tick() {
        if (m_phase == Phase::Finished)
            return;
        if (!m_leaseApiAvailable && m_phase == Phase::FirstPrefix) {
            fail(QStringLiteral("baseline: service lease API is absent; hidden queued job did not promote"));
            return;
        }

        switch (m_phase) {
        case Phase::FirstPrefix:
            if (QFileInfo(m_firstPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            if (state(m_firstId) != QStringLiteral("downloading")
                || state(m_secondId) != QStringLiteral("queued")) {
                fail(QStringLiteral("background lease did not run exactly one queued transfer"));
                return;
            }
            pass("background lease admits resolver handshake and starts the first queued transfer while hidden");
            m_phase = Phase::FirstDone;
            if (!m_server.finishHeldResponse())
                fail(QStringLiteral("first background response was not held for completion"));
            break;

        case Phase::FirstDone:
            if (!QFile::exists(m_firstPath) || readFile(m_firstPath) != m_firstBody)
                return;
            pass("HTTP bytes continue growing and complete exactly while hidden under the lease");
            if (m_resolveCount.value(m_secondId) != 1
                || state(m_secondId) != QStringLiteral("downloading")) {
                fail(QStringLiteral("second queued job was not promoted after first completion"));
                return;
            }
            m_phase = Phase::SecondPrefix;
            break;

        case Phase::SecondPrefix: {
            if (QFileInfo(m_secondPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            const int resetsBefore = m_resolutionResetCount;
            if (!setForeground(true) || !setBackgroundExecutionAllowed(false)) {
                fail(QStringLiteral("foreground/lease transition API missing"));
                return;
            }
            if (state(m_secondId) != QStringLiteral("downloading")
                || m_resolutionResetCount != resetsBefore) {
                fail(QStringLiteral("releasing the service lease while foreground interrupted the transfer"));
                return;
            }
            pass("foreground keeps transfer available when the background lease is released");
            m_phase = Phase::SecondDone;
            if (!m_server.finishHeldResponse())
                fail(QStringLiteral("second response was not held for completion"));
            break;
        }

        case Phase::SecondDone:
            if (!QFile::exists(m_secondPath) || readFile(m_secondPath) != m_secondBody)
                return;
            pass("foreground transfer completes after service lease release");
            if (!setForeground(false)) {
                fail(QStringLiteral("foreground lifecycle API missing"));
                return;
            }
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_torrentBody, m_prefixSize);
            m_phase = Phase::TorrentPrefix;
            enqueue(m_torrentId, m_torrentPath, m_torrentHash, m_torrentFileIdx);
            if (state(m_torrentId) != QStringLiteral("queued")
                || m_resolveCount.value(m_torrentId) != 0) {
                fail(QStringLiteral("fresh hidden job promoted after the lease was released"));
                return;
            }
            if (!setBackgroundExecutionAllowed(true)) {
                fail(QStringLiteral("service lease could not be reacquired while hidden"));
                return;
            }
            break;

        case Phase::TorrentPrefix:
            if (QFileInfo(m_torrentPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            if (!setBackgroundExecutionAllowed(false)) {
                fail(QStringLiteral("service lease release failed during torrent transfer"));
                return;
            }
            m_server.dropHeldSocket();
            if (state(m_torrentId) != QStringLiteral("paused")
                || readFile(m_torrentPath + QStringLiteral(".part")) != m_torrentBody.left(m_prefixSize)
                || !row(m_torrentId).value(QStringLiteral("url")).toString().isEmpty()) {
                fail(QStringLiteral("hidden lease loss did not preserve exact torrent bytes and retire its route"));
                return;
            }
            pass("hidden lease loss pauses torrent work, preserves its exact prefix, and retires the route");
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_torrentBody);
            m_phase = Phase::TorrentPaused;
            if (!setBackgroundExecutionAllowed(true)) {
                fail(QStringLiteral("service lease could not be reacquired for torrent recovery"));
                return;
            }
            break;

        case Phase::TorrentPaused:
            if (m_resolveCount.value(m_torrentId) != 2)
                return;
            m_phase = Phase::TorrentDone;
            break;

        case Phase::TorrentDone:
            if (!QFile::exists(m_torrentPath) || readFile(m_torrentPath) != m_torrentBody)
                return;
            if (m_server.requestCount() < 4
                || !m_server.requestAt(3).startsWith("GET /fresh-torrent-route.mp4 ")
                || !m_server.requestAt(3).toLower().contains(
                    "\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                || m_server.statusAt(3) != 206) {
                fail(QStringLiteral("recovered torrent job did not re-resolve and Range/206 from its exact prefix"));
                return;
            }
            pass("lease recovery re-resolves the pinned torrent file and completes via exact Range/206");
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_directBody, m_prefixSize);
            m_phase = Phase::DirectPrefix;
            enqueue(m_directId, m_directPath, QString(), -1);
            break;

        case Phase::DirectPrefix:
            if (QFileInfo(m_directPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            if (!setBackgroundExecutionAllowed(false)) {
                fail(QStringLiteral("service lease release failed during direct HTTP transfer"));
                return;
            }
            m_server.dropHeldSocket();
            if (state(m_directId) != QStringLiteral("paused")
                || readFile(m_directPath + QStringLiteral(".part")) != m_directBody.left(m_prefixSize)) {
                fail(QStringLiteral("lease loss did not pause direct HTTP while preserving exact partial bytes"));
                return;
            }
            pass("hidden lease loss also pauses direct HTTP and retains its exact partial bytes");
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_directBody);
            m_phase = Phase::DirectPaused;
            if (!setBackgroundExecutionAllowed(true)) {
                fail(QStringLiteral("service lease could not be reacquired for direct HTTP recovery"));
                return;
            }
            break;

        case Phase::DirectPaused:
            if (state(m_directId) != QStringLiteral("downloading"))
                return;
            m_phase = Phase::DirectDone;
            break;

        case Phase::DirectDone:
            if (!QFile::exists(m_directPath) || readFile(m_directPath) != m_directBody)
                return;
            if (m_server.requestCount() < 6
                || !m_server.requestAt(5).startsWith("GET /lease-loss-direct.mp4 ")
                || !m_server.requestAt(5).toLower().contains(
                    "\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                || m_server.statusAt(5) != 206) {
                fail(QStringLiteral("direct HTTP recovery failed to Range-resume the retained prefix"));
                return;
            }
            pass("direct HTTP resumes from the retained prefix after lease reacquisition");
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_manualBody, m_prefixSize);
            m_phase = Phase::ManualPrefix;
            enqueue(m_manualId, m_manualPath, QString(), -1);
            break;

        case Phase::ManualPrefix:
            if (QFileInfo(m_manualPath + QStringLiteral(".part")).size() < m_prefixSize)
                return;
            m_store.pauseJob(m_manualId);
            m_server.dropHeldSocket();
            if (state(m_manualId) != QStringLiteral("paused")
                || readFile(m_manualPath + QStringLiteral(".part")) != m_manualBody.left(m_prefixSize)) {
                fail(QStringLiteral("manual pause did not preserve the active transfer prefix"));
                return;
            }
            if (!setBackgroundExecutionAllowed(false) || !setBackgroundExecutionAllowed(true)
                || !setForeground(true) || !setForeground(false)) {
                fail(QStringLiteral("manual pause service/foreground toggles failed"));
                return;
            }
            if (state(m_manualId) != QStringLiteral("paused")
                || m_resolveCount.value(m_manualId) != 1) {
                fail(QStringLiteral("manual pause auto-resumed across service lease changes"));
                return;
            }
            pass("manual pause remains paused across service lease and foreground transitions");
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_manualBody);
            m_phase = Phase::ManualDone;
            m_store.resumeJob(m_manualId);
            break;

        case Phase::ManualDone:
            if (!QFile::exists(m_manualPath) || readFile(m_manualPath) != m_manualBody)
                return;
            pass("explicit manual resume still completes from its preserved prefix");
            finish();
            break;

        case Phase::Finished:
            break;
        }
    }

    void finish() {
        m_phase = Phase::Finished;
        m_poll.stop();
        m_deadline.stop();
        std::printf("RESULT: %d passed, 0 failed\n", m_passCount);
        std::fflush(stdout);
        m_app.quit();
    }

    QCoreApplication &m_app;
    QTemporaryDir m_temp;
    RangeFixture m_server;
    DownloadStore m_store;
    QTimer m_poll;
    QTimer m_deadline;
    Phase m_phase = Phase::FirstPrefix;
    bool m_leaseApiAvailable = false;
    int m_resolutionResetCount = 0;
    QHash<QString, int> m_resolveCount;
    const qint64 m_prefixSize = 96 * 1024;
    const QString m_firstId = QStringLiteral("lease-first-%1").arg(QCoreApplication::applicationPid());
    const QString m_firstPath = m_temp.filePath(QStringLiteral("lease-first.mp4"));
    const QByteArray m_firstBody = patternedBytes(256 * 1024, 11);
    const QString m_secondId = QStringLiteral("lease-second-%1").arg(QCoreApplication::applicationPid());
    const QString m_secondPath = m_temp.filePath(QStringLiteral("lease-second.mp4"));
    const QByteArray m_secondBody = patternedBytes(192 * 1024, 12);
    const QString m_torrentId = QStringLiteral("lease-torrent-%1").arg(QCoreApplication::applicationPid());
    const QString m_torrentPath = m_temp.filePath(QStringLiteral("lease-torrent.mp4"));
    const QString m_torrentHash = QString(40, QLatin1Char('9'));
    const int m_torrentFileIdx = 4;
    const QByteArray m_torrentBody = patternedBytes(320 * 1024, 13);
    const QString m_directId = QStringLiteral("lease-direct-%1").arg(QCoreApplication::applicationPid());
    const QString m_directPath = m_temp.filePath(QStringLiteral("lease-direct.mp4"));
    const QByteArray m_directBody = patternedBytes(224 * 1024, 14);
    const QString m_manualId = QStringLiteral("lease-manual-%1").arg(QCoreApplication::applicationPid());
    const QString m_manualPath = m_temp.filePath(QStringLiteral("lease-manual.mp4"));
    const QByteArray m_manualBody = patternedBytes(160 * 1024, 15);
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

class TransportRetryHarness final : public QObject {
public:
    explicit TransportRetryHarness(QCoreApplication &app)
        : QObject(&app), m_app(app), m_server(this),
          m_store(std::make_unique<DownloadStore>(this)), m_poll(this), m_deadline(this),
          m_rangeHeadersOnly(app.arguments().contains(QStringLiteral("--invalid-range-only"))) {
        m_poll.setInterval(10);
        QObject::connect(&m_poll, &QTimer::timeout, this, [this]() { tick(); });
        m_deadline.setSingleShot(true);
        QObject::connect(&m_deadline, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("transport recovery timed out in phase %1").arg(int(m_phase)));
        });
        connectStore();
    }

    bool start() {
        if (!m_temp.isValid())
            return fail(QStringLiteral("temporary directory unavailable"));
        if (!m_server.listen())
            return fail(QStringLiteral("loopback HTTP fixture failed to listen"));
        if (m_rangeHeadersOnly)
            startWrongRangeCase();
        else
            startSamePinCase();
        m_deadline.start(30000);
        m_poll.start();
        return true;
    }

private:
    enum class Phase {
        SamePrefix, SameFailed, SameResume,
        SameRangePrefix, SameRangeFailed,
        WrongRangePrefix, WrongRangePaused, WrongRangeRejected,
        MissingRangePrefix, MissingRangePaused, MissingRangeRejected,
        MismatchPrefix, MismatchFailed, MismatchResume, MismatchComplete,
        DirectPrefix, DirectFailed, DirectResume, Finished,
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

    void connectStore() {
        QObject::connect(m_store.get(), &DownloadStore::needResolve, this,
                         [this](const QString &id, const QString &, const QString &) {
            onNeedResolve(id);
        });
    }

    void reloadStore() {
        m_store.reset();
        m_store = std::make_unique<DownloadStore>(this);
        connectStore();
    }

    QString state(const QString &id) const {
        for (const QVariant &value : m_store->jobs()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString() == id)
                return row.value(QStringLiteral("state")).toString();
        }
        return {};
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
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    void enqueue(const QString &id, const QString &path,
                 const QString &infoHash = {}, int fileIdx = -1) {
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
        m_store->enqueue(request);
    }

    bool feedTorrent(const QString &id, const QString &url,
                     const QString &hash, int fileIdx) {
        return QMetaObject::invokeMethod(
            m_store.get(), "feedTorrentUrl", Qt::DirectConnection,
            Q_ARG(QString, id), Q_ARG(QString, url), Q_ARG(QString, hash), Q_ARG(int, fileIdx));
    }

    void onNeedResolve(const QString &id) {
        const int count = ++m_resolveCount[id];
        if (id == m_sameId) {
            if (count == 1) {
                if (!feedTorrent(id, m_server.url(QStringLiteral("/interrupted-origin.mp4")),
                                 m_sameHash, m_sameFileIdx))
                    fail(QStringLiteral("feedTorrentUrl unavailable for initial pinned transfer"));
            } else if (count == 2) {
                if (!feedTorrent(id, m_server.url(QStringLiteral("/recovered-fresh-route.mp4")),
                                 m_sameHash, m_sameFileIdx))
                    fail(QStringLiteral("same-pin retry failed to accept its fresh torrent route"));
            } else if (count == 3) {
                if (!feedTorrent(id, m_server.url(QStringLiteral("/recovered-again-fresh-route.mp4")),
                                 m_sameHash, m_sameFileIdx))
                    fail(QStringLiteral("second same-pin retry failed to accept its fresh route"));
            }
        } else if (id == m_mismatchId) {
            if (count == 1) {
                if (!feedTorrent(id, m_server.url(QStringLiteral("/mismatch-origin.mp4")),
                                 m_oldHash, m_oldFileIdx))
                    fail(QStringLiteral("feedTorrentUrl unavailable for mismatch fixture"));
            } else if (count == 2) {
                if (!feedTorrent(id, m_server.url(QStringLiteral("/mismatch-fresh-route.mp4")),
                                 m_newHash, m_newFileIdx))
                    fail(QStringLiteral("mismatched retry failed to accept its fresh route"));
            }
        } else if (id == m_wrongRangeId || id == m_missingRangeId) {
            const bool missing = id == m_missingRangeId;
            const QString route = count == 1
                ? (missing ? QStringLiteral("/missing-range-origin.mp4")
                           : QStringLiteral("/wrong-range-origin.mp4"))
                : (missing ? QStringLiteral("/missing-range-retry.mp4")
                           : QStringLiteral("/wrong-range-retry.mp4"));
            if (!feedTorrent(id, m_server.url(route), m_badRangeHash, m_badRangeFileIdx))
                fail(QStringLiteral("invalid-Range fixture failed to accept its torrent route"));
        } else if (id == m_directId) {
            const QString route = count == 1 ? QStringLiteral("/direct-origin.mp4")
                                             : QStringLiteral("/direct-fresh-route.mp4");
            m_store->feedSource(id, m_server.url(route), {});
        }
    }

    void startSamePinCase() {
        m_samePath = m_temp.filePath(QStringLiteral("same-pin.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_sameBody, m_prefixSize);
        m_phase = Phase::SamePrefix;
        enqueue(m_sameId, m_samePath, m_sameHash, m_sameFileIdx);
    }

    void startMismatchCase() {
        m_mismatchPath = m_temp.filePath(QStringLiteral("mismatch-pin.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_oldBody, m_prefixSize);
        m_phase = Phase::MismatchPrefix;
        enqueue(m_mismatchId, m_mismatchPath, m_oldHash, m_oldFileIdx);
    }

    void startWrongRangeCase() {
        m_badRangePath = m_temp.filePath(QStringLiteral("wrong-range-offset.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_badRangeBody, m_prefixSize);
        m_phase = Phase::WrongRangePrefix;
        enqueue(m_wrongRangeId, m_badRangePath, m_badRangeHash, m_badRangeFileIdx);
    }

    void startMissingRangeCase() {
        m_missingRangePath = m_temp.filePath(QStringLiteral("missing-range-offset.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_badRangeBody, m_prefixSize);
        m_phase = Phase::MissingRangePrefix;
        enqueue(m_missingRangeId, m_missingRangePath, m_badRangeHash, m_badRangeFileIdx);
    }

    void startDirectCase() {
        m_directPath = m_temp.filePath(QStringLiteral("direct-no-pin.mp4"));
        m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_directBody, m_prefixSize);
        m_phase = Phase::DirectPrefix;
        enqueue(m_directId, m_directPath);
    }

    bool interruptedPrefixIsExact(const QString &path, const QByteArray &expectedBytes,
                                  const QString &label) {
        if (!QFile::exists(path + QStringLiteral(".part"))
            || readFile(path + QStringLiteral(".part")) != expectedBytes) {
            fail(QStringLiteral("%1 transport failure did not retain the exact verified prefix")
                     .arg(label));
            return false;
        }
        return true;
    }

    void retry(const QString &id) {
        m_retryRequestIndex = m_server.requestCount();
        m_store->retryJob(id);
    }

    void tick() {
        if (m_phase == Phase::Finished)
            return;
        switch (m_phase) {
        case Phase::SamePrefix:
            if (QFileInfo(m_samePath + QStringLiteral(".part")).size() >= m_prefixSize) {
                if (readFile(m_samePath + QStringLiteral(".part")) != m_sameBody.left(m_prefixSize)) {
                    fail(QStringLiteral("initial torrent response prefix differed from source bytes"));
                    return;
                }
                m_phase = Phase::SameFailed;
                m_server.dropHeldSocket();
            }
            break;
        case Phase::SameFailed:
            if (state(m_sameId) != QStringLiteral("failed"))
                break;
            if (row(m_sameId).value(QStringLiteral("error")).toString().isEmpty()) {
                fail(QStringLiteral("transport failure did not expose a retryable error"));
                return;
            }
            if (!interruptedPrefixIsExact(m_samePath, m_sameBody.left(m_prefixSize),
                                          QStringLiteral("same-pin")))
                return;
            if (row(m_sameId).value(QStringLiteral("infoHash")).toString() != m_sameHash
                || row(m_sameId).value(QStringLiteral("fileIdx")).toInt() != m_sameFileIdx
                || !row(m_sameId).value(QStringLiteral("url")).toString().isEmpty()) {
                fail(QStringLiteral("failed row lost its exact torrent pin or retained a stale route"));
                return;
            }
            pass("interrupted exact-pinned torrent keeps its prefix, pin, and failed/retry state");
            reloadStore();
            if (state(m_sameId) != QStringLiteral("failed")
                || row(m_sameId).value(QStringLiteral("infoHash")).toString() != m_sameHash
                || row(m_sameId).value(QStringLiteral("fileIdx")).toInt() != m_sameFileIdx
                || !interruptedPrefixIsExact(m_samePath, m_sameBody.left(m_prefixSize),
                                             QStringLiteral("reloaded same-pin"))) {
                fail(QStringLiteral("failed pinned partial did not survive DownloadStore reload"));
                return;
            }
            pass("failed pinned partial and identity survive a DownloadStore restart");
            m_server.setResponse(RangeFixture::Mode::HoldRangePrefix, m_sameBody, m_prefixSize);
            m_phase = Phase::SameRangePrefix;
            retry(m_sameId);
            break;
        case Phase::SameRangePrefix:
            if (QFileInfo(m_samePath + QStringLiteral(".part")).size() >= 2 * m_prefixSize) {
                const QByteArray request = m_server.requestAt(m_retryRequestIndex).toLower();
                if (m_server.requestCount() <= m_retryRequestIndex
                    || !request.startsWith("get /recovered-fresh-route.mp4 ")
                    || !request.contains("\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                    || m_server.statusAt(m_retryRequestIndex) != 206
                    || readFile(m_samePath + QStringLiteral(".part"))
                        != m_sameBody.left(2 * m_prefixSize)) {
                    fail(QStringLiteral("first retry did not append only the exact 206 range prefix"));
                    return;
                }
                m_phase = Phase::SameRangeFailed;
                m_server.dropHeldSocket();
            }
            break;
        case Phase::SameRangeFailed:
            if (state(m_sameId) != QStringLiteral("failed"))
                break;
            if (!interruptedPrefixIsExact(m_samePath, m_sameBody.left(2 * m_prefixSize),
                                          QStringLiteral("Range-resumed same-pin")))
                return;
            pass("an interrupted validated 206 response retains the exact combined prefix");
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_sameBody);
            m_phase = Phase::SameResume;
            retry(m_sameId);
            break;
        case Phase::SameResume:
            if (!QFile::exists(m_samePath) || readFile(m_samePath) != m_sameBody)
                break;
            if (m_server.requestCount() <= m_retryRequestIndex
                || !m_server.requestAt(m_retryRequestIndex).toLower().contains(
                       "\r\nrange: bytes=" + QByteArray::number(2 * m_prefixSize))
                || m_server.statusAt(m_retryRequestIndex) != 206
                || !m_server.requestAt(m_retryRequestIndex).startsWith(
                       "GET /recovered-again-fresh-route.mp4 ")) {
                fail(QStringLiteral("same-pin Retry did not use fresh route plus exact Range/206"));
                return;
            }
            pass("manual Retry after repeated interruption completes via fresh Range/206 byte-exactly");
            startWrongRangeCase();
            break;
        case Phase::WrongRangePrefix:
            if (QFileInfo(m_badRangePath + QStringLiteral(".part")).size() >= m_prefixSize) {
                m_store->pauseJob(m_wrongRangeId);
                m_server.dropHeldSocket();
                m_phase = Phase::WrongRangePaused;
            }
            break;
        case Phase::WrongRangePaused:
            if (state(m_wrongRangeId) != QStringLiteral("paused"))
                break;
            if (!interruptedPrefixIsExact(m_badRangePath, m_badRangeBody.left(m_prefixSize),
                                          QStringLiteral("wrong-Content-Range")))
                return;
            m_server.setResponse(RangeFixture::Mode::WrongRangeOffset, m_badRangeBody);
            m_phase = Phase::WrongRangeRejected;
            m_retryRequestIndex = m_server.requestCount();
            m_store->resumeJob(m_wrongRangeId);
            break;
        case Phase::WrongRangeRejected:
            if (m_server.requestCount() <= m_retryRequestIndex)
                break;
            if (state(m_wrongRangeId) == QStringLiteral("queued")
                || state(m_wrongRangeId) == QStringLiteral("resolving")
                || state(m_wrongRangeId) == QStringLiteral("downloading"))
                break;
            if (state(m_wrongRangeId) != QStringLiteral("failed")) {
                fail(QStringLiteral("wrong Content-Range response was accepted (state=%1, outputPromoted=%2)")
                         .arg(state(m_wrongRangeId), QFile::exists(m_badRangePath)
                                  ? QStringLiteral("true") : QStringLiteral("false")));
                return;
            }
            if (m_server.statusAt(m_retryRequestIndex) != 206
                || !m_server.requestAt(m_retryRequestIndex).toLower().contains(
                       "\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                || QFile::exists(m_badRangePath)
                || QFile::exists(m_badRangePath + QStringLiteral(".part"))) {
                fail(QStringLiteral("successful 206 with wrong Content-Range was appended or promoted"));
                return;
            }
            pass("successful 206 with wrong Content-Range is rejected before append and discarded");
            startMissingRangeCase();
            break;
        case Phase::MissingRangePrefix:
            if (QFileInfo(m_missingRangePath + QStringLiteral(".part")).size() >= m_prefixSize) {
                m_store->pauseJob(m_missingRangeId);
                m_server.dropHeldSocket();
                m_phase = Phase::MissingRangePaused;
            }
            break;
        case Phase::MissingRangePaused:
            if (state(m_missingRangeId) != QStringLiteral("paused"))
                break;
            if (!interruptedPrefixIsExact(m_missingRangePath, m_badRangeBody.left(m_prefixSize),
                                          QStringLiteral("missing-Content-Range")))
                return;
            m_server.setResponse(RangeFixture::Mode::MissingRangeOffset, m_badRangeBody);
            m_phase = Phase::MissingRangeRejected;
            m_retryRequestIndex = m_server.requestCount();
            m_store->resumeJob(m_missingRangeId);
            break;
        case Phase::MissingRangeRejected:
            if (m_server.requestCount() <= m_retryRequestIndex)
                break;
            if (state(m_missingRangeId) == QStringLiteral("queued")
                || state(m_missingRangeId) == QStringLiteral("resolving")
                || state(m_missingRangeId) == QStringLiteral("downloading"))
                break;
            if (state(m_missingRangeId) != QStringLiteral("failed")) {
                fail(QStringLiteral("missing Content-Range response was accepted (state=%1, outputPromoted=%2)")
                         .arg(state(m_missingRangeId), QFile::exists(m_missingRangePath)
                                  ? QStringLiteral("true") : QStringLiteral("false")));
                return;
            }
            if (m_server.statusAt(m_retryRequestIndex) != 206
                || !m_server.requestAt(m_retryRequestIndex).toLower().contains(
                       "\r\nrange: bytes=" + QByteArray::number(m_prefixSize))
                || QFile::exists(m_missingRangePath)
                || QFile::exists(m_missingRangePath + QStringLiteral(".part"))) {
                fail(QStringLiteral("successful 206 without Content-Range was appended or promoted"));
                return;
            }
            pass("successful 206 without Content-Range is rejected before append and discarded");
            if (m_rangeHeadersOnly)
                finish();
            else
                startMismatchCase();
            break;
        case Phase::MismatchPrefix:
            if (QFileInfo(m_mismatchPath + QStringLiteral(".part")).size() >= m_prefixSize) {
                m_phase = Phase::MismatchFailed;
                m_server.dropHeldSocket();
            }
            break;
        case Phase::MismatchFailed:
            if (state(m_mismatchId) != QStringLiteral("failed"))
                break;
            if (!interruptedPrefixIsExact(m_mismatchPath, m_oldBody.left(m_prefixSize),
                                          QStringLiteral("mismatch")))
                return;
            m_server.setResponse(RangeFixture::Mode::HoldPrefix, m_newBody, m_prefixSize);
            m_phase = Phase::MismatchResume;
            retry(m_mismatchId);
            break;
        case Phase::MismatchResume:
            if (m_server.requestCount() > m_retryRequestIndex
                && QFileInfo(m_mismatchPath + QStringLiteral(".part")).size() >= m_prefixSize) {
                if (m_server.requestAt(m_retryRequestIndex).toLower().contains("\r\nrange:")
                    || m_server.statusAt(m_retryRequestIndex) != 200
                    || readFile(m_mismatchPath + QStringLiteral(".part")) != m_newBody.left(m_prefixSize)) {
                    fail(QStringLiteral("changed torrent identity did not discard old bytes before retry"));
                    return;
                }
                if (!m_server.finishHeldResponse()) {
                    fail(QStringLiteral("mismatch retry response was not held for clean-prefix assertion"));
                    return;
                }
                m_phase = Phase::MismatchComplete;
            }
            break;
        case Phase::MismatchComplete:
            if (QFile::exists(m_mismatchPath) && readFile(m_mismatchPath) == m_newBody) {
                pass("changed hash/fileIdx discards the old prefix before downloading the new file");
                startDirectCase();
            }
            break;
        case Phase::DirectPrefix:
            if (QFileInfo(m_directPath + QStringLiteral(".part")).size() >= m_prefixSize) {
                m_phase = Phase::DirectFailed;
                m_server.dropHeldSocket();
            }
            break;
        case Phase::DirectFailed:
            if (state(m_directId) != QStringLiteral("failed"))
                break;
            if (row(m_directId).value(QStringLiteral("error")).toString().isEmpty()
                || QFile::exists(m_directPath + QStringLiteral(".part"))) {
                fail(QStringLiteral("un-pinned direct HTTP failure must remain honest and discard unsafe partial bytes"));
                return;
            }
            pass("un-pinned direct HTTP failure does not retain bytes without an immutable file identity");
            m_server.setResponse(RangeFixture::Mode::HonorRange, m_directBody);
            m_phase = Phase::DirectResume;
            retry(m_directId);
            break;
        case Phase::DirectResume:
            if (QFile::exists(m_directPath) && readFile(m_directPath) == m_directBody) {
                if (m_server.requestCount() <= m_retryRequestIndex
                    || m_server.requestAt(m_retryRequestIndex).toLower().contains("\r\nrange:")
                    || m_server.statusAt(m_retryRequestIndex) != 200
                    || !m_server.requestAt(m_retryRequestIndex).startsWith(
                           "GET /direct-fresh-route.mp4 ")) {
                    fail(QStringLiteral("direct Retry appended without an exact pinned identity"));
                    return;
                }
                pass("direct Retry resolves a fresh route and downloads cleanly without unsafe append");
                finish();
            }
            break;
        case Phase::Finished:
            break;
        }
    }

    void finish() {
        m_phase = Phase::Finished;
        m_poll.stop();
        m_deadline.stop();
        std::printf("RESULT: %d passed, 0 failed\n", m_passCount);
        std::fflush(stdout);
        m_app.quit();
    }

    QCoreApplication &m_app;
    QTemporaryDir m_temp;
    RangeFixture m_server;
    std::unique_ptr<DownloadStore> m_store;
    QTimer m_poll;
    QTimer m_deadline;
    const bool m_rangeHeadersOnly;
    Phase m_phase = Phase::SamePrefix;
    const qint64 m_prefixSize = 96 * 1024;
    const QString m_sameId = QStringLiteral("transport-retry-same-%1").arg(QCoreApplication::applicationPid());
    const QString m_sameHash = QString(40, QLatin1Char('a'));
    const int m_sameFileIdx = 2;
    const QByteArray m_sameBody = patternedBytes(512 * 1024, 11);
    const QString m_wrongRangeId = QStringLiteral("transport-retry-wrong-range-%1").arg(QCoreApplication::applicationPid());
    const QString m_missingRangeId = QStringLiteral("transport-retry-missing-range-%1").arg(QCoreApplication::applicationPid());
    const QString m_badRangeHash = QString(40, QLatin1Char('d'));
    const int m_badRangeFileIdx = 5;
    const QByteArray m_badRangeBody = patternedBytes(384 * 1024, 15);
    const QString m_mismatchId = QStringLiteral("transport-retry-mismatch-%1").arg(QCoreApplication::applicationPid());
    const QString m_oldHash = QString(40, QLatin1Char('b'));
    const int m_oldFileIdx = 3;
    const QString m_newHash = QString(40, QLatin1Char('c'));
    const int m_newFileIdx = 8;
    const QByteArray m_oldBody = patternedBytes(512 * 1024, 12);
    const QByteArray m_newBody = patternedBytes(448 * 1024, 13);
    const QString m_directId = QStringLiteral("transport-retry-direct-%1").arg(QCoreApplication::applicationPid());
    const QByteArray m_directBody = patternedBytes(256 * 1024, 14);
    QString m_samePath;
    QString m_badRangePath;
    QString m_missingRangePath;
    QString m_mismatchPath;
    QString m_directPath;
    QHash<QString, int> m_resolveCount;
    int m_retryRequestIndex = 0;
    int m_passCount = 0;
};

} // namespace

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ColosseumAudit"));
    const bool legacyCompatibility = app.arguments().contains(QStringLiteral("--legacy-compat"));
    const bool backgroundExecution = app.arguments().contains(QStringLiteral("--background-execution"));
    const bool transportRetry = app.arguments().contains(QStringLiteral("--transport-retry"));
    QCoreApplication::setApplicationName(QStringLiteral("DownloadStoreLifecycle-%1-%2")
                                             .arg(legacyCompatibility ? QStringLiteral("legacy")
                                                   : backgroundExecution ? QStringLiteral("background")
                                                   : transportRetry ? QStringLiteral("transport-retry")
                                                                         : QStringLiteral("regular"))
                                             .arg(QCoreApplication::applicationPid()));

    if (legacyCompatibility) {
        LegacyCompatHarness harness(app);
        if (!harness.start())
            return 1;
        return app.exec();
    }
    if (backgroundExecution) {
        BackgroundExecutionHarness harness(app);
        if (!harness.start())
            return 1;
        return app.exec();
    }
    if (transportRetry) {
        TransportRetryHarness harness(app);
        if (!harness.start())
            return 1;
        return app.exec();
    }
    Harness harness(app);
    if (!harness.start())
        return 1;
    return app.exec();
}
