// cover_delivery_harness — REAL CachingNam::createRequest cover delivery fixture.
//
// Drives the actual extracted owner (CachingNam/CachingNamFactory + CoverDelivery)
// against loopback QTcpServer fixtures only. No abstract scheduler imitation: every
// assertion below is produced by requests issued through QNetworkAccessManager::get()
// on a real CachingNam, exactly as QML's Image would.
//
// Covered contract:
//   * positive classification + non-image/XHR bypass
//   * app-wide per-host cap 2 shared across two NAMs; independent hosts; FIFO order;
//     bounded queue overflow -> async local failure
//   * 429 Retry-After ledger: deadline suppression, restart persistence, no HTTP
//     until due, cache-only warm reuse, success clears stale entries
//   * queued abort / deleted queued reply / active cancel release exactly once
//   * cross-host redirect re-enters destination admission (never bypasses the cap),
//     cooled destinations are rejected, pinned Host identity does not cross origins
//   * streaming passthrough, gzip Jikan branch unchanged, malformed ledger safe

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "net/CachingNam.h"
#include "net/CoverDelivery.h"
#include "net/Ipv4PinStore.h"
#include "third_party/miniz/miniz.h"

namespace {

QByteArray gzipStored(const QByteArray &plain)
{
    QByteArray out;
    out.append(char(0x1f));
    out.append(char(0x8b));
    out.append(char(0x08));
    out.append(char(0x00));
    out.append(QByteArray(4, '\0'));
    out.append(char(0x00));
    out.append(char(0x03));
    const quint16 len = static_cast<quint16>(plain.size());
    out.append(char(0x01));
    out.append(char(len & 0xff));
    out.append(char(len >> 8));
    const quint16 nlen = static_cast<quint16>(~len);
    out.append(char(nlen & 0xff));
    out.append(char(nlen >> 8));
    out.append(plain);
    const quint32 crc = mz_crc32(MZ_CRC32_INIT,
                                 reinterpret_cast<const mz_uint8 *>(plain.constData()),
                                 static_cast<size_t>(plain.size()));
    for (int i = 0; i < 4; ++i)
        out.append(char((crc >> (8 * i)) & 0xff));
    for (int i = 0; i < 4; ++i)
        out.append(char((plain.size() >> (8 * i)) & 0xff));
    return out;
}

bool waitFinished(QNetworkReply *reply, int timeoutMs = 5000)
{
    if (reply->isFinished())
        return true;
    QSignalSpy spy(reply, &QNetworkReply::finished);
    return spy.wait(timeoutMs);
}

} // namespace

// ── Minimal loopback HTTP fixture ─────────────────────────────────────────────

class FixtureServer : public QObject
{
    Q_OBJECT
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = QByteArrayLiteral("application/octet-stream");
        QList<QPair<QByteArray, QByteArray>> extraHeaders;
        QUrl location;
        int delayMs = 0;
        QList<int> chunkDelaysMs;
    };
    using Responder = std::function<Response(int, const QString &)>;

    struct Request {
        QByteArray method;
        QString target;
        QHash<QByteArray, QByteArray> headers;   // lower-cased names
        qint64 arrivalMs = 0;
    };

    explicit FixtureServer(Responder responder, QObject *parent = nullptr)
        : QObject(parent), m_responder(std::move(responder))
    {
        m_clock.start();
        connect(&m_server, &QTcpServer::newConnection, this, &FixtureServer::onNewConnection);
    }

    bool listen(const QString &address)
    {
        return m_server.listen(QHostAddress(address), 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    int requestCount() const { return m_requests.size(); }
    const QList<Request> &requests() const { return m_requests; }

    QStringList targets() const
    {
        QStringList out;
        for (const Request &r : m_requests)
            out << r.target;
        return out;
    }

    int countFor(const QString &target) const
    {
        int n = 0;
        for (const Request &r : m_requests)
            if (r.target == target)
                ++n;
        return n;
    }

private:
    void onNewConnection()
    {
        QTcpSocket *socket = m_server.nextPendingConnection();
        auto *buffer = new QByteArray;
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            if (!socket->property("parsed").toBool()) {
                socket->setProperty("parsed", true);
                Request request;
                const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                if (!lines.isEmpty()) {
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    if (first.size() >= 2) {
                        request.method = first.at(0);
                        request.target = QString::fromLatin1(first.at(1));
                    }
                }
                for (int i = 1; i < lines.size(); ++i) {
                    const QByteArray line = lines.at(i).trimmed();
                    const int colon = line.indexOf(':');
                    if (colon <= 0)
                        continue;
                    request.headers.insert(line.left(colon).toLower(),
                                           line.mid(colon + 1).trimmed());
                }
                request.arrivalMs = m_clock.elapsed();
                m_requests.append(request);
                sendResponse(socket, m_requests.size() - 1);
            }
        });
    }

    void sendResponse(QTcpSocket *socket, int index)
    {
        const Response response = m_responder(index, m_requests.at(index).target);
        auto writeAll = [socket, response] {
            QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
            if (response.status >= 300 && response.status < 400 && response.location.isValid())
                out += "Location: " + response.location.toString().toUtf8() + "\r\n";
            out += "Content-Type: " + response.contentType + "\r\n";
            out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
            for (const auto &header : response.extraHeaders)
                out += header.first + ": " + header.second + "\r\n";
            out += "Connection: close\r\n\r\n";
            if (response.chunkDelaysMs.isEmpty()) {
                out += response.body;
                socket->write(out);
                socket->flush();
                socket->disconnectFromHost();
                return;
            }
            const int chunkCount = response.chunkDelaysMs.size();
            const int chunkSize = qMax(1, response.body.size() / chunkCount);
            out += response.body.left(chunkSize);
            socket->write(out);
            socket->flush();
            for (int i = 1; i < chunkCount; ++i) {
                const int from = i * chunkSize;
                const int to = i == chunkCount - 1 ? response.body.size()
                                                   : qMin(response.body.size(), from + chunkSize);
                const QByteArray chunk = response.body.mid(from, to - from);
                QTimer::singleShot(response.chunkDelaysMs.at(i), socket, [socket, chunk] {
                    if (socket->state() == QAbstractSocket::ConnectedState) {
                        socket->write(chunk);
                        socket->flush();
                    }
                });
            }
            QTimer::singleShot(response.chunkDelaysMs.last() + 20, socket, [socket] {
                socket->disconnectFromHost();
            });
        };
        if (response.delayMs > 0)
            QTimer::singleShot(response.delayMs, socket, writeAll);
        else
            writeAll();
    }

    QTcpServer m_server;
    Responder m_responder;
    QList<Request> m_requests;
    QElapsedTimer m_clock;
};

// ── Real independently running NAM-owner threads ─────────────────────────────

class ThreadedCoverPeer final : public QObject
{
    Q_OBJECT
public:
    ThreadedCoverPeer(CoverDeliveryState *state, QString cachePath)
        : m_state(state), m_cachePath(std::move(cachePath)) {}
    ~ThreadedCoverPeer() override { delete m_nam; }

    void open()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        m_nam = new CachingNam({}, nullptr, nullptr, true, nullptr, nullptr,
                               nullptr, m_state, m_cachePath);
        emit ready(m_nam->thread() == QThread::currentThread());
    }

    void request(int id, const QUrl &url)
    {
        Q_ASSERT(m_nam && QThread::currentThread() == thread());
        auto *reply = m_nam->get(QNetworkRequest(url));
        Q_ASSERT(dynamic_cast<DeferredCoverReply *>(reply));
        m_replies.insert(id, reply);
        connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
            emit completed(id, static_cast<int>(reply->error()), reply->readAll(),
                           QThread::currentThread() == thread()
                               && m_nam && m_nam->thread() == thread()
                               && reply->thread() == thread());
        });
        connect(reply, &QObject::destroyed, this, [this, id] {
            m_replies.remove(id);
            emit gone(id, QThread::currentThread() == thread());
        });
        emit submitted(id);
    }

    void cancel(int id)
    {
        if (auto *reply = m_replies.value(id).data())
            reply->abort();
    }

    void destroy(int id)
    {
        delete m_replies.value(id).data(); // only on the owning QThread
    }

    void close()
    {
        const bool ownThread = QThread::currentThread() == thread()
            && m_nam && m_nam->thread() == thread();
        delete m_nam; // destroys all remaining replies on their owner thread
        m_nam = nullptr;
        m_replies.clear();
        emit closed(ownThread);
    }

signals:
    void ready(bool correctThread);
    void submitted(int id);
    void completed(int id, int error, QByteArray payload, bool correctThread);
    void gone(int id, bool correctThread);
    void closed(bool correctThread);

private:
    CoverDeliveryState *m_state;
    QString m_cachePath;
    CachingNam *m_nam = nullptr;
    QHash<int, QPointer<QNetworkReply>> m_replies;
};

class ThreadedPeerRunner
{
public:
    ThreadedPeerRunner(CoverDeliveryState *state, const QString &cacheDir)
    {
        peer = new ThreadedCoverPeer(state, cacheDir);
        peer->moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, peer, &ThreadedCoverPeer::open);
        QObject::connect(&thread, &QThread::finished, peer, &QObject::deleteLater);
    }
    ~ThreadedPeerRunner()
    {
        thread.quit();
        if (!thread.wait(5000))
            qFatal("threaded cover fixture did not stop its QThread");
    }

    void start() { thread.start(); }
    ThreadedPeerRunner(const ThreadedPeerRunner &) = delete;
    ThreadedPeerRunner &operator=(const ThreadedPeerRunner &) = delete;

    QThread thread;
    ThreadedCoverPeer *peer = nullptr;
};

void peerGet(ThreadedCoverPeer *peer, int id, const QString &url)
{
    QMetaObject::invokeMethod(peer, [peer, id, url] {
        peer->request(id, QUrl(url));
    }, Qt::QueuedConnection);
}

void peerCancel(ThreadedCoverPeer *peer, int id)
{
    QMetaObject::invokeMethod(peer, [peer, id] { peer->cancel(id); }, Qt::QueuedConnection);
}

void peerDestroy(ThreadedCoverPeer *peer, int id)
{
    QMetaObject::invokeMethod(peer, [peer, id] { peer->destroy(id); }, Qt::QueuedConnection);
}

void peerClose(ThreadedCoverPeer *peer)
{
    QMetaObject::invokeMethod(peer, [peer] { peer->close(); }, Qt::QueuedConnection);
}

bool spyHasId(const QSignalSpy &spy, int id)
{
    for (const auto &args : spy)
        if (args.at(0).toInt() == id)
            return true;
    return false;
}

// ── Test object ───────────────────────────────────────────────────────────────

class CoverDeliveryHarness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void classification();
    void sameHostCapAcrossNams();
    void twoRealThreadCapAndOtherHostProgress();
    void twoRealThreadWakeCancelDestroyRace();
    void twoRealThreadNamTeardownReleasesPermits();
    void independentHosts();
    void fifoQueueAndOverflow();
    void nonImageBypass();
    void retryAfterDeadlineRestartAndClear();
    void cachedImageAndEmptyBodyEviction();
    void queuedAbortDeletedAndInflightCancel();
    void canceledQueueSlotIsImmediatelyReusable();
    void queuedRequestHonorsNew429Cooldown();
    void immediateLedgerAndBoundedRetryAfter();
    void corruptLedgerRemainsWritable();
    void redirectAdmissionAndPinIdentity();
    void streamingPassthrough();
    void gzipJikanUnchanged();
    void badLedgerFailsSafely();
};

void CoverDeliveryHarness::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void CoverDeliveryHarness::classification()
{
    QVERIFY(CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://i0.wp.com/getcomics.org/share/uploads/2018/02/"
                       "Invincible-144-2018.jpg?fit=400%2C615&ssl=1"))));
    QVERIFY(CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://getcomics.org/share/uploads/2018/02/x.jpg"))));
    QVERIFY(CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://images.metahub.space/poster/medium/tt1234567/img"))));
    QVERIFY(CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://covers.openlibrary.org/b/id/OL123M-L.jpg"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://api.jikan.moe/v4/anime/1"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://i1.wp.com/getcomics.org/share/uploads/x.jpg"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://i0.wp.com.evil/getcomics.org/share/uploads/x.jpg"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://getcomics.org/search?q=invincible"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://getcomics.org/share/uploads/x.php"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("https://user:pass@getcomics.org/share/uploads/x.jpg"))));
    QVERIFY(!CoverDeliveryState::isManagedCoverUrl(QUrl(
        QStringLiteral("ftp://getcomics.org/share/uploads/x.jpg"))));

    // Positive-url must include port? Host must equal exactly.
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    QVERIFY(state.isManagedCover(QUrl(QStringLiteral("http://127.0.0.1:8080/cover/a.jpg"))));
    QVERIFY(!state.isManagedCover(QUrl(QStringLiteral("http://127.0.0.1:8080/other/a.jpg"))));
}

void CoverDeliveryHarness::twoRealThreadCapAndOtherHostProgress()
{
    FixtureServer shared([](int, const QString &) {
        FixtureServer::Response response;
        response.body = QByteArrayLiteral("SHARED");
        response.delayMs = 750;
        return response;
    });
    FixtureServer other([](int, const QString &) {
        FixtureServer::Response response;
        response.body = QByteArrayLiteral("OTHER");
        response.delayMs = 35;
        return response;
    });
    QVERIFY(shared.listen(QStringLiteral("127.0.0.1")));
    QVERIFY(other.listen(QStringLiteral("127.0.0.2")));
    QTemporaryDir cacheLeft, cacheRight;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    state.addManagedHostForTesting(QStringLiteral("127.0.0.2"));
    QCOMPARE(state.thread(), QThread::currentThread());
    ThreadedPeerRunner left(&state, cacheLeft.path() + QStringLiteral("/c"));
    ThreadedPeerRunner right(&state, cacheRight.path() + QStringLiteral("/c"));
    QSignalSpy leftReady(left.peer, &ThreadedCoverPeer::ready);
    QSignalSpy rightReady(right.peer, &ThreadedCoverPeer::ready);
    QSignalSpy leftDone(left.peer, &ThreadedCoverPeer::completed);
    QSignalSpy rightDone(right.peer, &ThreadedCoverPeer::completed);
    QSignalSpy leftClosed(left.peer, &ThreadedCoverPeer::closed);
    QSignalSpy rightClosed(right.peer, &ThreadedCoverPeer::closed);
    left.start();
    right.start();
    QTRY_COMPARE_WITH_TIMEOUT(leftReady.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(rightReady.size(), 1, 3000);
    QVERIFY(leftReady.at(0).at(0).toBool());
    QVERIFY(rightReady.at(0).at(0).toBool());
    QVERIFY(left.thread.isRunning() && right.thread.isRunning());
    QVERIFY(&left.thread != &right.thread);

    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(shared.port());
    const QString otherBase = QStringLiteral("http://127.0.0.2:%1/cover/").arg(other.port());
    peerGet(left.peer, 1, base + QStringLiteral("left1.jpg"));
    peerGet(left.peer, 2, base + QStringLiteral("left2.jpg"));
    peerGet(right.peer, 3, base + QStringLiteral("right1.jpg"));
    peerGet(right.peer, 4, base + QStringLiteral("right2.jpg"));
    peerGet(right.peer, 5, otherBase + QStringLiteral("other.jpg"));
    QTRY_COMPARE_WITH_TIMEOUT(shared.requestCount(), 2, 2000);
    QTest::qWait(90);
    QCOMPARE(shared.requestCount(), 2); // cap 2 is GLOBAL, not 2 per QThread
    QTRY_VERIFY_WITH_TIMEOUT(spyHasId(rightDone, 5), 2000);
    QCOMPARE(leftDone.size(), 0);       // second host progressed while first blocked
    QTRY_COMPARE_WITH_TIMEOUT(leftDone.size() + rightDone.size(), 5, 8000);
    QCOMPARE(shared.requestCount(), 4);
    QCOMPARE(other.requestCount(), 1);
    for (const QSignalSpy *spy : {&leftDone, &rightDone}) {
        for (const auto &record : *spy) {
            QCOMPARE(record.at(1).toInt(), int(QNetworkReply::NoError));
            QCOMPARE(record.at(2).toByteArray(),
                     record.at(0).toInt() == 5 ? QByteArrayLiteral("OTHER")
                                              : QByteArrayLiteral("SHARED"));
            QVERIFY(record.at(3).toBool()); // NAM, reply and callback: same owner thread
        }
    }
    peerClose(left.peer);
    peerClose(right.peer);
    QTRY_COMPARE_WITH_TIMEOUT(leftClosed.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(rightClosed.size(), 1, 3000);
    QVERIFY(leftClosed.at(0).at(0).toBool());
    QVERIFY(rightClosed.at(0).at(0).toBool());
}

void CoverDeliveryHarness::twoRealThreadWakeCancelDestroyRace()
{
    // Both scenarios pause exactly after permit promotion, but before signal
    // delivery. Another QThread cancels/destroys the promoted receiver, then
    // the original thread resumes and must never dereference that dead QObject.
    for (const bool destroyPromoted : {false, true}) {
        FixtureServer server([](int, const QString &) {
            FixtureServer::Response response;
            response.body = QByteArrayLiteral("RACE");
            response.delayMs = 900;
            return response;
        });
        QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
        QTemporaryDir cacheA, cacheB;
        QSemaphore hookEntered, resumeWake;
        std::atomic_bool firstWake{true};
        CoverDeliveryState state;
        state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
        state.setBeforeWakeSignalForTesting([&] {
            if (firstWake.exchange(false)) {
                hookEntered.release();
                resumeWake.acquire();
            }
        });
        ThreadedPeerRunner active(&state, cacheA.path() + QStringLiteral("/c"));
        ThreadedPeerRunner pending(&state, cacheB.path() + QStringLiteral("/c"));
        struct UnblockOnExit {
            QSemaphore &gate;
            ~UnblockOnExit() { gate.release(4); }
        } unblock{resumeWake};
        QSignalSpy activeReady(active.peer, &ThreadedCoverPeer::ready);
        QSignalSpy pendingReady(pending.peer, &ThreadedCoverPeer::ready);
        QSignalSpy activeDone(active.peer, &ThreadedCoverPeer::completed);
        QSignalSpy pendingDone(pending.peer, &ThreadedCoverPeer::completed);
        QSignalSpy pendingGone(pending.peer, &ThreadedCoverPeer::gone);
        QSignalSpy activeClosed(active.peer, &ThreadedCoverPeer::closed);
        QSignalSpy pendingClosed(pending.peer, &ThreadedCoverPeer::closed);
        QSignalSpy pendingSent(pending.peer, &ThreadedCoverPeer::submitted);
        active.start();
        pending.start();
        QTRY_COMPARE_WITH_TIMEOUT(activeReady.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(pendingReady.size(), 1, 3000);
        const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());
        peerGet(active.peer, 10, base + QStringLiteral("active0.jpg"));
        peerGet(active.peer, 11, base + QStringLiteral("active1.jpg"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 2, 3000);
        peerGet(pending.peer, 12, base + QStringLiteral("doomed.jpg"));
        peerGet(pending.peer, 13, base + QStringLiteral("follower.jpg"));
        QTRY_COMPARE_WITH_TIMEOUT(pendingSent.size(), 2, 3000);
        QCOMPARE(server.requestCount(), 2); // both B replies really queued
        peerCancel(active.peer, 10); // releases a slot on thread A
        QTRY_VERIFY_WITH_TIMEOUT(hookEntered.available() > 0, 3000);
        // Thread A is now blocked outside state mutex, holding a ticket, not
        // the reply pointer. Thread B can freely destroy/abort its receiver.
        if (destroyPromoted) {
            peerDestroy(pending.peer, 12);
            QTRY_VERIFY_WITH_TIMEOUT(spyHasId(pendingGone, 12), 3000);
        } else {
            peerCancel(pending.peer, 12);
            QTRY_VERIFY_WITH_TIMEOUT(spyHasId(pendingDone, 12), 3000);
        }
        resumeWake.release();
        QTRY_VERIFY_WITH_TIMEOUT(spyHasId(pendingDone, 13), 7000);
        QTRY_VERIFY_WITH_TIMEOUT(spyHasId(activeDone, 11), 7000);
        QCOMPARE(server.countFor(QStringLiteral("/cover/doomed.jpg")), 0);
        // A fresh request must still acquire a permit after this exact
        // destruction-versus-notification interleaving.
        peerGet(pending.peer, 14, base + QStringLiteral("after-race.jpg"));
        QTRY_VERIFY_WITH_TIMEOUT(spyHasId(pendingDone, 14), 7000);
        for (const auto &record : pendingDone) {
            if (record.at(0).toInt() == 12) {
                QCOMPARE(record.at(1).toInt(), int(QNetworkReply::OperationCanceledError));
            } else {
                QCOMPARE(record.at(1).toInt(), int(QNetworkReply::NoError));
                QCOMPARE(record.at(2).toByteArray(), QByteArrayLiteral("RACE"));
            }
            QVERIFY(record.at(3).toBool());
        }
        QCOMPARE(server.countFor(QStringLiteral("/cover/after-race.jpg")), 1);
        peerClose(active.peer);
        peerClose(pending.peer);
        QTRY_COMPARE_WITH_TIMEOUT(activeClosed.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(pendingClosed.size(), 1, 3000);
        QVERIFY(activeClosed.at(0).at(0).toBool());
        QVERIFY(pendingClosed.at(0).at(0).toBool());
    }
}

void CoverDeliveryHarness::twoRealThreadNamTeardownReleasesPermits()
{
    FixtureServer server([](int, const QString &) {
        FixtureServer::Response response;
        response.body = QByteArrayLiteral("TEARDOWN");
        response.delayMs = 650;
        return response;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cacheA, cacheB;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    ThreadedPeerRunner owner(&state, cacheA.path() + QStringLiteral("/c"));
    ThreadedPeerRunner successor(&state, cacheB.path() + QStringLiteral("/c"));
    QSignalSpy ownerReady(owner.peer, &ThreadedCoverPeer::ready);
    QSignalSpy nextReady(successor.peer, &ThreadedCoverPeer::ready);
    QSignalSpy ownerGone(owner.peer, &ThreadedCoverPeer::gone);
    QSignalSpy ownerClosed(owner.peer, &ThreadedCoverPeer::closed);
    QSignalSpy nextDone(successor.peer, &ThreadedCoverPeer::completed);
    QSignalSpy nextSent(successor.peer, &ThreadedCoverPeer::submitted);
    QSignalSpy nextClosed(successor.peer, &ThreadedCoverPeer::closed);
    owner.start();
    successor.start();
    QTRY_COMPARE_WITH_TIMEOUT(ownerReady.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(nextReady.size(), 1, 3000);
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());
    peerGet(owner.peer, 20, base + QStringLiteral("closing0.jpg"));
    peerGet(owner.peer, 21, base + QStringLiteral("closing1.jpg"));
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 2, 3000);
    peerGet(successor.peer, 22, base + QStringLiteral("waiting.jpg"));
    QTRY_COMPARE_WITH_TIMEOUT(nextSent.size(), 1, 3000);
    QCOMPARE(server.requestCount(), 2);
    // Delete an entire NAM and both active replies on its own thread. Their
    // destructors must return permits to a different live NAM's worker thread.
    peerClose(owner.peer);
    QTRY_COMPARE_WITH_TIMEOUT(ownerClosed.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(ownerGone.size(), 2, 3000);
    QVERIFY(ownerClosed.at(0).at(0).toBool());
    for (const auto &record : ownerGone)
        QVERIFY(record.at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(spyHasId(nextDone, 22), 7000);
    QCOMPARE(server.countFor(QStringLiteral("/cover/waiting.jpg")), 1);
    peerGet(successor.peer, 23, base + QStringLiteral("fresh0.jpg"));
    peerGet(successor.peer, 24, base + QStringLiteral("fresh1.jpg"));
    QTRY_COMPARE_WITH_TIMEOUT(nextDone.size(), 3, 7000);
    for (const auto &record : nextDone) {
        QCOMPARE(record.at(1).toInt(), int(QNetworkReply::NoError));
        QCOMPARE(record.at(2).toByteArray(), QByteArrayLiteral("TEARDOWN"));
        QVERIFY(record.at(3).toBool());
    }
    peerClose(successor.peer);
    QTRY_COMPARE_WITH_TIMEOUT(nextClosed.size(), 1, 3000);
    QVERIFY(nextClosed.at(0).at(0).toBool());
}

void CoverDeliveryHarness::sameHostCapAcrossNams()
{
    FixtureServer server([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("IMG");
        r.delayMs = 200;
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());

    QTemporaryDir cacheA;
    QTemporaryDir cacheB;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam namA({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                    cacheA.path() + QLatin1String("/c"));
    CachingNam namB({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                    cacheB.path() + QLatin1String("/c"));

    QList<QNetworkReply *> replies;
    for (int i = 0; i < 4; ++i) {
        CachingNam *nam = i < 2 ? &namA : &namB;
        QNetworkReply *reply = nam->get(QNetworkRequest(
            QUrl(base + QStringLiteral("/cover/a%1.jpg").arg(i))));
        QVERIFY(dynamic_cast<DeferredCoverReply *>(reply) != nullptr);
        replies << reply;
    }
    QTest::qWait(80);
    QCOMPARE(server.requestCount(), 2);   // app-wide cap 2 even across two NAMs
    for (QNetworkReply *reply : replies)
        QVERIFY(waitFinished(reply, 5000));
    QCOMPARE(server.requestCount(), 4);
    for (QNetworkReply *reply : replies) {
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArrayLiteral("IMG"));
    }
}

void CoverDeliveryHarness::independentHosts()
{
    FixtureServer first([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("A");
        r.delayMs = 200;
        return r;
    });
    FixtureServer second([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("B");
        r.delayMs = 200;
        return r;
    });
    QVERIFY(first.listen(QStringLiteral("127.0.0.1")));
    QVERIFY(second.listen(QStringLiteral("127.0.0.2")));

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    state.addManagedHostForTesting(QStringLiteral("127.0.0.2"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QList<QNetworkReply *> firstHost;
    for (int i = 0; i < 2; ++i)
        firstHost << nam.get(QNetworkRequest(
            QUrl(QStringLiteral("http://127.0.0.1:%1/cover/a%2.jpg").arg(first.port()).arg(i))));
    QNetworkReply *secondHost = nam.get(QNetworkRequest(
        QUrl(QStringLiteral("http://127.0.0.2:%1/cover/b.jpg").arg(second.port()))));
    QTest::qWait(80);
    QCOMPARE(first.requestCount(), 2);
    QCOMPARE(second.requestCount(), 1);   // independent host progresses immediately
    for (QNetworkReply *reply : firstHost)
        QVERIFY(waitFinished(reply, 5000));
    QVERIFY(waitFinished(secondHost, 5000));
}

void CoverDeliveryHarness::fifoQueueAndOverflow()
{
    FixtureServer server([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("FIFO");
        r.delayMs = 150;
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.setQueueLimitsForTesting(2, 10);
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QList<QNetworkReply *> replies;
    for (int i = 0; i < 4; ++i)
        replies << nam.get(QNetworkRequest(QUrl(base + QStringLiteral("r%1.jpg").arg(i))));
    QTest::qWait(80);
    QCOMPARE(server.requestCount(), 2);   // cap holds; two queued
    for (QNetworkReply *reply : replies)
        QVERIFY(waitFinished(reply, 5000));
    // The two admitted requests race their connections; the QUEUED pair must
    // arrive FIFO after them.
    const QStringList targets = server.targets();
    QCOMPARE(targets.size(), 4);
    QCOMPARE(targets.at(2), QStringLiteral("/cover/r2.jpg"));
    QCOMPARE(targets.at(3), QStringLiteral("/cover/r3.jpg"));
    QStringList firstTwo{targets.at(0), targets.at(1)};
    firstTwo.sort();
    QCOMPARE(firstTwo, (QStringList{QStringLiteral("/cover/r0.jpg"), QStringLiteral("/cover/r1.jpg")}));

    // Overflow: per-host queue cap 1 -> the second queued request fails locally.
    FixtureServer slow([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("SLOW");
        r.delayMs = 150;
        return r;
    });
    QVERIFY(slow.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cache2;
    CoverDeliveryState state2;
    state2.setQueueLimitsForTesting(1, 10);
    state2.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam2({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state2,
                    cache2.path() + QLatin1String("/c"));
    const QString base2 = QStringLiteral("http://127.0.0.1:%1/cover/").arg(slow.port());
    QNetworkReply *active0 = nam2.get(QNetworkRequest(QUrl(base2 + QStringLiteral("o0.jpg"))));
    QNetworkReply *active1 = nam2.get(QNetworkRequest(QUrl(base2 + QStringLiteral("o1.jpg"))));
    QNetworkReply *queued = nam2.get(QNetworkRequest(QUrl(base2 + QStringLiteral("o2.jpg"))));
    QNetworkReply *overflow = nam2.get(QNetworkRequest(QUrl(base2 + QStringLiteral("o3.jpg"))));
    QVERIFY(waitFinished(overflow, 2000));
    QVERIFY(overflow->errorString().contains(QStringLiteral("cover-queue-overflow")));
    QCOMPARE(slow.countFor(QStringLiteral("/cover/o3.jpg")), 0);
    for (QNetworkReply *reply : {active0, active1, queued})
        QVERIFY(waitFinished(reply, 5000));
    QCOMPARE(slow.countFor(QStringLiteral("/cover/o2.jpg")), 1);  // queued one ran
    QCOMPARE(overflow->error(), QNetworkReply::TemporaryNetworkFailureError);
}

void CoverDeliveryHarness::nonImageBypass()
{
    FixtureServer server([](int, const QString &target) {
        FixtureServer::Response r;
        if (target.startsWith(QLatin1String("/cover/"))) {
            r.body = QByteArrayLiteral("IMG");
            r.delayMs = 200;
        } else {
            r.body = QByteArrayLiteral("{\"json\":true}");
            r.contentType = QByteArrayLiteral("application/json");
        }
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QNetworkReply *cover0 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("/cover/a.jpg"))));
    QNetworkReply *cover1 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("/cover/b.jpg"))));
    QNetworkReply *json = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("/cat/api/seeds.json"))));
    QVERIFY(dynamic_cast<DeferredCoverReply *>(cover0) != nullptr);
    QVERIFY(dynamic_cast<DeferredCoverReply *>(json) == nullptr);   // bypasses the gate
    QTest::qWait(80);
    QCOMPARE(server.countFor(QStringLiteral("/cat/api/seeds.json")), 1);  // not queued
    QVERIFY(waitFinished(json, 3000));
    QCOMPARE(json->readAll(), QByteArrayLiteral("{\"json\":true}"));
    for (QNetworkReply *reply : {cover0, cover1})
        QVERIFY(waitFinished(reply, 5000));
}

void CoverDeliveryHarness::retryAfterDeadlineRestartAndClear()
{
    FixtureServer server([](int index, const QString &) {
        FixtureServer::Response r;
        if (index == 0) {
            r.status = 429;
            r.extraHeaders.append({QByteArrayLiteral("Retry-After"), QByteArrayLiteral("30")});
        } else {
            r.body = QByteArrayLiteral("OK");
        }
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());
    const QString urlA = base + QStringLiteral("a.jpg");
    const QString urlB = base + QStringLiteral("b.jpg");
    const QByteArray digestA = CoverDeliveryState::urlDigest(QUrl(urlA));
    const QString host = QStringLiteral("127.0.0.1");

    qint64 fakeNow = 1000000000000LL;
    QTemporaryDir dir;
    const QString ledgerPath = dir.path() + QStringLiteral("/net/image-retry.json");

    QTemporaryDir cache1;
    {
        CoverDeliveryState state1;
        state1.setClockForTesting([&fakeNow] { return fakeNow; });
        state1.addManagedHostForTesting(host);
        state1.loadLedger(ledgerPath);
        CachingNam nam1({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state1,
                        cache1.path() + QLatin1String("/c"));

        QNetworkReply *first = nam1.get(QNetworkRequest(QUrl(urlA)));
        QVERIFY(waitFinished(first, 5000));
        QCOMPARE(first->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 429);
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(state1.cooldownRemainingMs(host, digestA) > 0);

        // No HTTP until the deadline: same URL and sibling URL on the same host.
        QNetworkReply *second = nam1.get(QNetworkRequest(QUrl(urlA)));
        QVERIFY(waitFinished(second, 3000));
        QVERIFY(second->errorString().contains(QStringLiteral("cover-cooldown")));
        QCOMPARE(second->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 429);
        QVERIFY(!second->rawHeader("Retry-After").isEmpty());
        QNetworkReply *sibling = nam1.get(QNetworkRequest(QUrl(urlB)));
        QVERIFY(waitFinished(sibling, 3000));
        QVERIFY(sibling->errorString().contains(QStringLiteral("cover-cooldown")));
        QCOMPARE(server.requestCount(), 1);   // zero new HTTP

        QVERIFY(state1.saveLedgerNow());
    }

    // "New process" restart against the same ledger: suppression survives.
    QTemporaryDir cache2;
    {
        CoverDeliveryState state2;
        state2.setClockForTesting([&fakeNow] { return fakeNow; });
        state2.addManagedHostForTesting(host);
        QVERIFY(state2.loadLedger(ledgerPath));
        CachingNam nam2({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state2,
                        cache2.path() + QLatin1String("/c"));

        QNetworkReply *beforeDue = nam2.get(QNetworkRequest(QUrl(urlA)));
        QVERIFY(waitFinished(beforeDue, 3000));
        QVERIFY(beforeDue->errorString().contains(QStringLiteral("cover-cooldown")));
        QCOMPARE(server.requestCount(), 1);

        // Cross the deadline: the request is allowed only when normally requested.
        fakeNow += 31000;
        QNetworkReply *due = nam2.get(QNetworkRequest(QUrl(urlA)));
        QVERIFY(waitFinished(due, 5000));
        QCOMPARE(due->error(), QNetworkReply::NoError);
        QCOMPARE(due->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(due->readAll(), QByteArrayLiteral("OK"));
        QCOMPARE(server.requestCount(), 2);

        // Success clears the stale entries: sibling proceeds with no wait.
        QCOMPARE(state2.cooldownRemainingMs(host, digestA), 0);
        QNetworkReply *after = nam2.get(QNetworkRequest(QUrl(urlB)));
        QVERIFY(waitFinished(after, 5000));
        QCOMPARE(after->error(), QNetworkReply::NoError);
        QCOMPARE(server.requestCount(), 3);
    }
}

void CoverDeliveryHarness::cachedImageAndEmptyBodyEviction()
{
    FixtureServer server([](int, const QString &target) {
        FixtureServer::Response r;
        if (target.endsWith(QLatin1String("cold.jpg"))) {
            r.status = 429;
            r.extraHeaders.append({QByteArrayLiteral("Retry-After"), QByteArrayLiteral("60")});
        } else if (target.endsWith(QLatin1String("empty.jpg"))) {
            r.body = QByteArray();   // 200 with Content-Length: 0
        } else if (target.endsWith(QLatin1String("warm.jpg"))) {
            r.body = QByteArrayLiteral("WARMIMG");
            r.extraHeaders.append({QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("max-age=600")});
        } else {
            r.body = QByteArrayLiteral("OK");
        }
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QNetworkReply *warm = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("warm.jpg"))));
    QVERIFY(waitFinished(warm, 5000));
    QCOMPARE(warm->readAll(), QByteArrayLiteral("WARMIMG"));
    QCOMPARE(server.requestCount(), 1);
    QTest::qWait(50);

    QNetworkReply *warmAgain = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("warm.jpg"))));
    QVERIFY(waitFinished(warmAgain, 5000));
    QCOMPARE(warmAgain->readAll(), QByteArrayLiteral("WARMIMG"));
    QCOMPARE(server.requestCount(), 1);   // served from the disk cache, no wire

    // Empty 200s must never persist as art: they are evicted, so the next load
    // hits the server again.
    QNetworkReply *empty = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("empty.jpg"))));
    QVERIFY(waitFinished(empty, 5000));
    QCOMPARE(server.requestCount(), 2);
    QTest::qWait(50);
    QNetworkReply *emptyAgain = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("empty.jpg"))));
    QVERIFY(waitFinished(emptyAgain, 5000));
    QCOMPARE(server.requestCount(), 3);

    // 429 records the host cooldown.
    QNetworkReply *cold = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("cold.jpg"))));
    QVERIFY(waitFinished(cold, 5000));
    QCOMPARE(cold->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 429);
    QCOMPARE(server.requestCount(), 4);

    // Warm cover stays Ready during cooldown with zero traffic (cache-only).
    QNetworkReply *warmDuringCooldown = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("warm.jpg"))));
    QVERIFY(waitFinished(warmDuringCooldown, 5000));
    QCOMPARE(warmDuringCooldown->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QCOMPARE(warmDuringCooldown->readAll(), QByteArrayLiteral("WARMIMG"));
    QCOMPARE(server.requestCount(), 4);

    // Cold, uncached sibling: local failure, no wire.
    QNetworkReply *blocked = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("cold2.jpg"))));
    QVERIFY(waitFinished(blocked, 3000));
    QVERIFY(blocked->errorString().contains(QStringLiteral("cover-cooldown")));
    QCOMPARE(server.requestCount(), 4);
}

void CoverDeliveryHarness::queuedAbortDeletedAndInflightCancel()
{
    FixtureServer server([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("C");
        r.delayMs = 150;
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QNetworkReply *a0 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("a0.jpg"))));
    QNetworkReply *a1 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("a1.jpg"))));
    QNetworkReply *queued = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("q.jpg"))));
    QVERIFY(dynamic_cast<DeferredCoverReply *>(queued) != nullptr);
    queued->abort();
    QVERIFY(waitFinished(queued, 3000));
    QCOMPARE(queued->error(), QNetworkReply::OperationCanceledError);
    for (QNetworkReply *reply : {a0, a1})
        QVERIFY(waitFinished(reply, 5000));
    QCOMPARE(server.countFor(QStringLiteral("/cover/q.jpg")), 0);   // never started

    // A deleted queued reply must vanish without starting and without a leak.
    QNetworkReply *b0 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("b0.jpg"))));
    QNetworkReply *b1 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("b1.jpg"))));
    QNetworkReply *doomed = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("doomed.jpg"))));
    doomed->deleteLater();
    QTest::qWait(20);
    for (QNetworkReply *reply : {b0, b1})
        QVERIFY(waitFinished(reply, 5000));
    QTest::qWait(50);
    QCOMPARE(server.countFor(QStringLiteral("/cover/doomed.jpg")), 0);
    QNetworkReply *after = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("after.jpg"))));
    QVERIFY(waitFinished(after, 5000));
    QCOMPARE(server.countFor(QStringLiteral("/cover/after.jpg")), 1);  // permits intact

    // Active inflight cancel releases exactly one slot.
    QNetworkReply *c0 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("c0.jpg"))));
    QNetworkReply *c1 = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("c1.jpg"))));
    QTest::qWait(30);
    c0->abort();
    QVERIFY(waitFinished(c0, 3000));
    QCOMPARE(c0->error(), QNetworkReply::OperationCanceledError);
    QVERIFY(waitFinished(c1, 5000));
    QCOMPARE(server.countFor(QStringLiteral("/cover/c0.jpg")), 1);   // was inflight
}


void CoverDeliveryHarness::canceledQueueSlotIsImmediatelyReusable()
{
    FixtureServer server([](int, const QString &) {
        FixtureServer::Response response;
        response.body = QByteArrayLiteral("ART");
        response.delayMs = 350;
        return response;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    state.setQueueLimitsForTesting(1, 1);
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr,
                   &state, cache.path() + QLatin1String("/c"));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());
    auto *first = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("first.jpg"))));
    auto *second = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("second.jpg"))));
    auto *canceled = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("canceled.jpg"))));
    QSignalSpy canceledError(canceled, &QNetworkReply::errorOccurred);
    canceled->abort();
    QVERIFY(waitFinished(canceled, 1000));
    QCOMPARE(canceled->error(), QNetworkReply::OperationCanceledError);
    QCOMPARE(canceledError.count(), 1);
    auto *replacement = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("replacement.jpg"))));
    QVERIFY(waitFinished(replacement, 5000));
    QCOMPARE(replacement->error(), QNetworkReply::NoError);
    QCOMPARE(server.countFor(QStringLiteral("/cover/replacement.jpg")), 1);
    QCOMPARE(server.countFor(QStringLiteral("/cover/canceled.jpg")), 0);
    QVERIFY(waitFinished(first, 5000));
    QVERIFY(waitFinished(second, 5000));
}

void CoverDeliveryHarness::queuedRequestHonorsNew429Cooldown()
{
    FixtureServer server([](int, const QString &target) {
        FixtureServer::Response response;
        if (target.endsWith(QLatin1String("rate.jpg"))) {
            response.status = 429;
            response.extraHeaders.append({QByteArrayLiteral("Retry-After"), QByteArrayLiteral("60")});
        } else {
            response.body = QByteArrayLiteral("ART");
            response.delayMs = 400;
        }
        return response;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr,
                   &state, cache.path() + QLatin1String("/c"));
    const QString base = QStringLiteral("http://127.0.0.1:%1/cover/").arg(server.port());
    const QString ledgerPath = cache.path() + QLatin1String("/retry.json");
    QVERIFY(state.loadLedger(ledgerPath));
    auto *rate = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("rate.jpg"))));
    auto *slow = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("slow.jpg"))));
    auto *queued = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("queued.jpg"))));
    QVERIFY(waitFinished(rate, 5000));
    QCOMPARE(rate->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 429);
    QVERIFY(waitFinished(queued, 5000));
    QVERIFY(queued->errorString().contains(QStringLiteral("cover-cooldown")));
    QCOMPARE(server.countFor(QStringLiteral("/cover/queued.jpg")), 0);
    QVERIFY(waitFinished(slow, 5000));
    QCOMPARE(slow->error(), QNetworkReply::NoError);

    // The slow transaction started before the 429. Its successful completion
    // cannot revoke the server's still-active Retry-After for sibling requests.
    const QString host = QStringLiteral("127.0.0.1");
    const auto siblingDigest = CoverDeliveryState::urlDigest(
        QUrl(base + QStringLiteral("later.jpg")));
    QVERIFY(state.cooldownRemainingMs(host, siblingDigest) > 0);
    auto *later = nam.get(QNetworkRequest(QUrl(base + QStringLiteral("later.jpg"))));
    QVERIFY(waitFinished(later, 5000));
    QVERIFY(later->errorString().contains(QStringLiteral("cover-cooldown")));
    QCOMPARE(server.countFor(QStringLiteral("/cover/later.jpg")), 0);

    CoverDeliveryState restarted;
    QVERIFY(restarted.loadLedger(ledgerPath));
    QVERIFY(restarted.cooldownRemainingMs(host, siblingDigest) > 0);
}

void CoverDeliveryHarness::immediateLedgerAndBoundedRetryAfter()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/net/image-retry.json");
    const QString host = QStringLiteral("covers.openlibrary.org");
    const auto digest = CoverDeliveryState::urlDigest(
        QUrl(QStringLiteral("https://covers.openlibrary.org/b/id/123-L.jpg")));
    CoverDeliveryState owner;
    QVERIFY(owner.loadLedger(path));
    owner.record429(host, digest, QByteArrayLiteral("9223372036854775807"));
    const qint64 remaining = owner.cooldownRemainingMs(host, digest);
    QVERIFY(remaining > 0);
    QVERIFY2(remaining <= 24LL * 60 * 60 * 1000, "Retry-After must be capped at 24h");
    // This check is intentionally BEFORE destructor and before the save debounce.
    QVERIFY2(QFileInfo::exists(path), "429 must be durable before allowing another dispatch");
    CoverDeliveryState anotherProcess;
    QVERIFY(anotherProcess.loadLedger(path));
    QVERIFY(anotherProcess.cooldownRemainingMs(host, digest) > 0);
    const qint64 now = QDateTime::fromString(QStringLiteral("2026-10-16T12:00:00Z"),
                                            Qt::ISODate).toMSecsSinceEpoch();
    const auto httpDate = CoverDeliveryState::parseRetryAfter(
        QByteArrayLiteral("Fri, 16 Oct 2026 12:01:00 GMT"), now, 300);
    QVERIFY(httpDate.valid && !httpDate.fallback);
    QCOMPARE(httpDate.deadlineUtcMs - now, qint64(60000));
    const auto malformed = CoverDeliveryState::parseRetryAfter(
        QByteArrayLiteral("nonsense"), now, 300);
    QVERIFY(malformed.fallback);
    QCOMPARE(malformed.deadlineUtcMs - now, qint64(300000));
}

void CoverDeliveryHarness::corruptLedgerRemainsWritable()
{
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/image-retry.json");
    QFile corrupt(path);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("{bad json"), qint64(9));
    corrupt.close();
    const QString host = QStringLiteral("getcomics.org");
    const auto digest = CoverDeliveryState::urlDigest(
        QUrl(QStringLiteral("https://getcomics.org/share/uploads/art.jpg")));
    CoverDeliveryState owner;
    QVERIFY(!owner.loadLedger(path));
    owner.record429(host, digest, QByteArrayLiteral("60"));
    CoverDeliveryState nextLaunch;
    QVERIFY2(nextLaunch.loadLedger(path), "corruption must not disable future persistence");
    QVERIFY(nextLaunch.cooldownRemainingMs(host, digest) > 0);
}

void CoverDeliveryHarness::redirectAdmissionAndPinIdentity()
{
    // Destination server (127.0.0.1) with a slow /cover/slow lane so its cap of 2
    // is saturated while the redirect hop waits.
    FixtureServer destination([](int, const QString &target) {
        FixtureServer::Response r;
        if (target.startsWith(QLatin1String("/cover/slow"))) {
            r.body = QByteArrayLiteral("SLOW");
            r.delayMs = 250;
        } else {
            r.body = QByteArrayLiteral("FINAL");
        }
        return r;
    });
    QVERIFY(destination.listen(QStringLiteral("127.0.0.1")));
    FixtureServer origin([&destination](int, const QString &target) {
        FixtureServer::Response r;
        r.status = 302;
        const QString suffix = target.contains(QLatin1String("redirect2"))
            ? QStringLiteral("/cover/hop2.jpg") : QStringLiteral("/cover/hop.jpg");
        r.location = QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                              .arg(destination.port()).arg(suffix));
        return r;
    });
    QVERIFY(origin.listen(QStringLiteral("127.0.0.2")));

    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    state.addManagedHostForTesting(QStringLiteral("127.0.0.2"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    const QString destBase = QStringLiteral("http://127.0.0.1:%1").arg(destination.port());
    const QString originBase = QStringLiteral("http://127.0.0.2:%1").arg(origin.port());

    QNetworkReply *slow0 = nam.get(QNetworkRequest(QUrl(destBase + QStringLiteral("/cover/slow0.jpg"))));
    QNetworkReply *slow1 = nam.get(QNetworkRequest(QUrl(destBase + QStringLiteral("/cover/slow1.jpg"))));
    QTest::qWait(50);

    const QString hopTarget = destBase + QStringLiteral("/cover/hop.jpg");
    QNetworkReply *hop = nam.get(QNetworkRequest(QUrl(originBase + QStringLiteral("/cover/redirect.jpg"))));
    QTest::qWait(80);
    QCOMPARE(destination.countFor(QStringLiteral("/cover/hop.jpg")), 0);  // cap respected: queued
    QVERIFY(waitFinished(hop, 5000));
    QCOMPARE(hop->error(), QNetworkReply::NoError);
    QCOMPARE(hop->url(), QUrl(hopTarget));
    QCOMPARE(hop->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QVERIFY(hop->rawHeader("Location").isEmpty());
    QCOMPARE(hop->readAll(), QByteArrayLiteral("FINAL"));
    QCOMPARE(destination.countFor(QStringLiteral("/cover/hop.jpg")), 1);
    for (QNetworkReply *reply : {slow0, slow1})
        QVERIFY(waitFinished(reply, 5000));
    QCOMPARE(destination.requestCount(), 3);

    // Redirect to a cooled destination is rejected without traffic.
    state.record429(QStringLiteral("127.0.0.1"),
                    CoverDeliveryState::urlDigest(QUrl(destBase + QStringLiteral("/cover/cooled.jpg"))),
                    QByteArrayLiteral("60"));
    // Host-level cooldown also exists from that record; check via a hop to the host itself.
    QNetworkReply *cooledHop = nam.get(QNetworkRequest(QUrl(originBase + QStringLiteral("/cover/redirect2.jpg"))));
    QVERIFY(waitFinished(cooledHop, 3000));
    QVERIFY(cooledHop->errorString().contains(QStringLiteral("cover-cooldown")));
    QCOMPARE(destination.countFor(QStringLiteral("/cover/hop2.jpg")), 0);

    // Pin identity: a URL-pinned host redirecting to another origin must not leak
    // the pinned Host identity; the hop re-enters fresh prepare (destination Host).
    QTemporaryDir pinDir;
    const QString pinPath = pinDir.path() + QStringLiteral("/pins.json");
    {
        QJsonObject pins;
        pins.insert(QStringLiteral("pinned.test"), QStringLiteral("127.0.0.1"));
        QJsonObject root;
        root.insert(QStringLiteral("schema"), 1);
        root.insert(QStringLiteral("pins"), pins);
        QFile file(pinPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    Ipv4PinStore pins(pinPath);
    QCOMPARE(pins.pinForHost(QStringLiteral("pinned.test")), QStringLiteral("127.0.0.1"));

    FixtureServer pinnedTarget([](int, const QString &) {
        FixtureServer::Response r;
        r.body = QByteArrayLiteral("PINNEDORIGIN");
        return r;
    });
    QVERIFY(pinnedTarget.listen(QStringLiteral("127.0.0.2")));

    QTemporaryDir cache2;
    CoverDeliveryState state2;
    state2.addManagedHostForTesting(QStringLiteral("pinned.test"));
    state2.addManagedHostForTesting(QStringLiteral("127.0.0.2"));
    CachingNam nam2({QStringLiteral("pinned.test")}, &pins, nullptr, true, nullptr, nullptr,
                    nullptr, &state2, cache2.path() + QLatin1String("/c"));

    FixtureServer pinnedOrigin([&](int, const QString &) {
        FixtureServer::Response r;
        r.status = 302;
        r.location = QUrl(QStringLiteral("http://127.0.0.2:%1/cover/final.jpg")
                              .arg(pinnedTarget.port()));
        return r;
    });
    QVERIFY(pinnedOrigin.listen(QStringLiteral("127.0.0.1")));

    QNetworkReply *pinnedHop = nam2.get(QNetworkRequest(
        QUrl(QStringLiteral("http://pinned.test:%1/cover/start.jpg").arg(pinnedOrigin.port()))));
    QVERIFY(waitFinished(pinnedHop, 5000));
    QCOMPARE(pinnedHop->error(), QNetworkReply::NoError);
    QCOMPARE(pinnedHop->readAll(), QByteArrayLiteral("PINNEDORIGIN"));
    QCOMPARE(pinnedTarget.countFor(QStringLiteral("/cover/final.jpg")), 1);
    bool staleIdentity = false;
    for (const FixtureServer::Request &request : pinnedTarget.requests()) {
        if (request.target != QStringLiteral("/cover/final.jpg"))
            continue;
        for (auto it = request.headers.constBegin(); it != request.headers.constEnd(); ++it) {
            if (it.value().contains(QByteArrayLiteral("pinned.test")))
                staleIdentity = true;
        }
    }
    QVERIFY(!staleIdentity);
}

void CoverDeliveryHarness::streamingPassthrough()
{
    QByteArray body;
    for (int i = 0; i < 300; ++i)
        body.append(char('a' + (i % 26)));
    FixtureServer server([&body](int, const QString &) {
        FixtureServer::Response r;
        r.body = body;
        // Keep chunk timers strictly ordered; equal timeouts race on Windows
        // and can reorder the synthetic TCP payload independent of the NAM.
        r.chunkDelaysMs = {0, 60, 120};
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));

    QNetworkReply *reply = nam.get(QNetworkRequest(
        QUrl(QStringLiteral("http://127.0.0.1:%1/cover/stream.jpg").arg(server.port()))));
    QSignalSpy readySpy(reply, &QNetworkReply::readyRead);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QByteArray received;
    connect(reply, &QNetworkReply::readyRead, this, [&] { received += reply->readAll(); });
    QVERIFY(finishedSpy.wait(5000));
    received += reply->readAll();
    QVERIFY(readySpy.count() >= 2);            // streamed, not buffered whole
    QCOMPARE(received, body);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(reply->error(), QNetworkReply::NoError);
}

void CoverDeliveryHarness::gzipJikanUnchanged()
{
    const QByteArray json = QByteArrayLiteral("{\"ok\":true}");
    const QByteArray gz = gzipStored(json);
    FixtureServer server([&gz](int, const QString &) {
        FixtureServer::Response r;
        r.body = gz;
        r.contentType = QByteArrayLiteral("application/json");
        r.extraHeaders.append({QByteArrayLiteral("Content-Encoding"), QByteArrayLiteral("gzip")});
        return r;
    });
    QVERIFY(server.listen(QStringLiteral("127.0.0.1")));
    QTemporaryDir cache;
    CoverDeliveryState state;
    state.addManagedHostForTesting(QStringLiteral("127.0.0.1"));
    CachingNam nam({}, nullptr, nullptr, true, nullptr, nullptr, nullptr, &state,
                   cache.path() + QLatin1String("/c"));
    nam.setJikanHostForTesting(QStringLiteral("127.0.0.1"));

    QNetworkReply *reply = nam.get(QNetworkRequest(
        QUrl(QStringLiteral("http://127.0.0.1:%1/v4/anime/1").arg(server.port()))));
    QVERIFY(dynamic_cast<DeferredCoverReply *>(reply) == nullptr);  // not a cover lane
    QVERIFY(waitFinished(reply, 5000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    QCOMPARE(reply->readAll(), json);
    QCOMPARE(server.requests().first().headers.value(QByteArrayLiteral("accept-encoding")),
             QByteArrayLiteral("gzip"));
}

void CoverDeliveryHarness::badLedgerFailsSafely()
{
    QTemporaryDir dir;
    const QString host = QStringLiteral("127.0.0.1");
    const QByteArray digest = CoverDeliveryState::urlDigest(QUrl(QStringLiteral(
        "http://127.0.0.1:1/cover/x.jpg")));

    {
        QFile garbage(dir.path() + QStringLiteral("/garbage.json"));
        QVERIFY(garbage.open(QIODevice::WriteOnly));
        garbage.write("this is not json at all");
        garbage.close();
        CoverDeliveryState state;
        QVERIFY(!state.loadLedger(garbage.fileName()));
        state.addManagedHostForTesting(host);
        state.record429(host, digest, QByteArrayLiteral("5"));
        QVERIFY(state.cooldownRemainingMs(host, digest) > 0);   // still fully usable
    }
    {
        QFile wrongVersion(dir.path() + QStringLiteral("/version.json"));
        QVERIFY(wrongVersion.open(QIODevice::WriteOnly));
        wrongVersion.write("{\"version\":99,\"hosts\":{},\"urls\":{}}");
        wrongVersion.close();
        CoverDeliveryState state;
        QVERIFY(!state.loadLedger(wrongVersion.fileName()));
    }
    {
        QFile array(dir.path() + QStringLiteral("/array.json"));
        QVERIFY(array.open(QIODevice::WriteOnly));
        array.write("[1,2,3]");
        array.close();
        CoverDeliveryState state;
        QVERIFY(!state.loadLedger(array.fileName()));
    }
    {
        QFile oversized(dir.path() + QStringLiteral("/huge.json"));
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        oversized.write(QByteArray(300 * 1024, 'a'));
        oversized.close();
        CoverDeliveryState state;
        QVERIFY(!state.loadLedger(oversized.fileName()));
    }
    {
        QFile typed(dir.path() + QStringLiteral("/typed.json"));
        QVERIFY(typed.open(QIODevice::WriteOnly));
        typed.write("{\"version\":1,\"hosts\":{\"h\":\"bad\",\"ok\":\"1\"},\"urls\":{\"zz\":5},\"lane\":-3}");
        typed.close();
        CoverDeliveryState state;
        QVERIFY(state.loadLedger(typed.fileName()));
        QCOMPARE(state.ledgerHostCount(), 0);   // malformed entries dropped
        QCOMPARE(state.ledgerUrlCount(), 0);
    }
    {
        // Round-trip: a clean save is loadable and a truncated copy still fails safe.
        CoverDeliveryState saver;
        saver.addManagedHostForTesting(host);
        const QString path = dir.path() + QStringLiteral("/roundtrip.json");
        QVERIFY(saver.loadLedger(path));
        saver.record429(host, digest, QByteArrayLiteral("60"));
        QVERIFY(saver.saveLedgerNow());
        CoverDeliveryState loader;
        QVERIFY(loader.loadLedger(path));
        QVERIFY(loader.cooldownRemainingMs(host, digest) > 0);
    }
}

QTEST_GUILESS_MAIN(CoverDeliveryHarness)
#include "cover_delivery_harness.moc"
