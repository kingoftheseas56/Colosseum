#include "CollectionLinkActivation.h"
#include "InstanceLock.h"
#include "account/CollectionShareLink.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLocalServer>
#include <QMutex>
#include <QSemaphore>
#include <QSet>
#include <atomic>
#include <QThread>
#include <QTimer>
#include <memory>
#include <limits>

namespace {
const QUrl WebOrigin(QStringLiteral("https://getcolosseum.world"));
QString identity(const QString &root) {
    if (root.trimmed().isEmpty()) return {};
    QString path = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
}
// All accepted routes share this single latest-ID mailbox. Posting and receiver
// invalidation use the same mutex; no worker callback captures activation.
struct CollectionActivationMailbox {
    QMutex mutex;
    QObject *receiver = nullptr; // guarded; invalidated before owner destruction
    QString latestId;
    bool pending = false;
    bool notificationPosted = false;
    std::atomic_bool stopped{false};
    void postLocked() {
        if (!stopped && pending && !notificationPosted && receiver) {
            notificationPosted = true;
            QMetaObject::invokeMethod(receiver, "drain", Qt::QueuedConnection);
        }
    }
    bool offer(const QString &id) {
        QMutexLocker lock(&mutex);
        if (stopped) return false;
        latestId = id; pending = true; postLocked(); return true;
    }
};

// The QThread object is parentless and self-reclaimed on finished. Its run-local
// QObjects are created/destroyed only in this thread. The lease is the SAME
// authoritative lock, retained even if bounded shutdown must return early.
class CollectionActivationListener final : public QThread {
public:
    CollectionActivationListener(std::shared_ptr<CollectionActivationMailbox> box,
                                 std::shared_ptr<ColosseumInstanceLock> lease, QString name)
        : mailbox(std::move(box)), lockLease(std::move(lease)), serverName(std::move(name)) {}
    QSemaphore started;
    std::atomic_bool listening{false};
protected:
    void run() override {
        QObject context;
        QLocalServer server;
        QSet<QLocalSocket *> clients;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        server.setMaxPendingConnections(4);
        const auto box = mailbox;
        connect(&server, &QLocalServer::newConnection, &context, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                if (!socket) break;
                if (box->stopped || clients.size() >= 4) { socket->abort(); socket->deleteLater(); continue; }
                socket->setReadBufferSize(45);
                socket->setParent(&context); clients.insert(socket);
                struct Frame { QByteArray bytes; bool complete = false; };
                auto frame = std::make_shared<Frame>();
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QObject::destroyed, &context, [&, socket] { clients.remove(socket); });
                const auto readFrame = [box, socket, frame] {
                    if (frame->complete) return;
                    auto &data = frame->bytes;
                    if (box->stopped || socket->bytesAvailable() > 44 - data.size()) { socket->abort(); return; }
                    data.append(socket->read(44 - data.size()));
                    if (data.size() < 44) return;
                    frame->complete = true;
                    const QString id = QString::fromLatin1(data.left(43));
                    if (data.at(43) != '\n' || Colosseum::Collections::applicationShareLink(id).isEmpty()
                        || !box->offer(id)) { socket->abort(); return; }
                    socket->write("OK\n"); socket->flush(); socket->disconnectFromServer();
                };
                connect(socket, &QLocalSocket::readyRead, &context, readFrame);
                QTimer::singleShot(1000, socket, [socket] { socket->abort(); socket->deleteLater(); });
                // Account for data already buffered before the signal connection.
                if (socket->bytesAvailable()) readFrame();
            }
        });
        listening = !box->stopped && server.listen(serverName);
        // If stop races the interval just before exec(), this first event closes
        // the loop even though QThread::quit() had no event loop to stop yet.
        QTimer::singleShot(0, &context, [this, box] { if (box->stopped) quit(); });
        started.release();
        if (listening && !box->stopped) exec();
        server.close();
        for (auto *socket : clients) socket->abort();
        // Disconnect callbacks which refer to run-local containers before their
        // destruction; deleting the remaining sockets stays in their own thread.
        const auto remaining = clients;
        for (auto *socket : remaining) { disconnect(socket, nullptr, &context, nullptr); delete socket; }
    }
private:
    const std::shared_ptr<CollectionActivationMailbox> mailbox;
    const std::shared_ptr<ColosseumInstanceLock> lockLease;
    const QString serverName;
};

CollectionLinkActivation::CollectionLinkActivation(std::shared_ptr<ColosseumInstanceLock> lock, const QUrl &legacy, QObject *parent)
    : QObject(parent), m_lock(std::move(lock)), m_root(m_lock ? identity(m_lock->appDataRoot()) : QString()),
      m_legacyOrigin(legacy), m_mailbox(std::make_shared<CollectionActivationMailbox>()) {
    m_mailbox->receiver = this;
}
CollectionLinkActivation::~CollectionLinkActivation() { shutdown(); }
QString CollectionLinkActivation::channelName() const {
    return QStringLiteral("colosseum-collection-") + QString::fromLatin1(
        QCryptographicHash::hash(m_root.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool CollectionLinkActivation::startPrimary(const QString &root) {
    if (m_mailbox->stopped || !m_lock || m_root.isEmpty() || identity(root) != m_root || !m_lock->isLocked()) return false;
    if (m_listener) return true;
    QLocalServer::removeServer(channelName());
    auto *listener = new CollectionActivationListener(m_mailbox, m_lock, channelName());
    m_listener = listener;
    connect(listener, &QThread::finished, listener, &QObject::deleteLater);
    listener->start();
    if (!listener->started.tryAcquire(1, 1000) || !listener->listening) { shutdown(); return false; }
    return true;
}
bool CollectionLinkActivation::forwardToPrimary(const QUrl &url, int timeoutMs) {
    const auto id = Colosseum::Collections::shareIdFromUrl(url, WebOrigin, m_legacyOrigin);
    if (m_mailbox->stopped || m_root.isEmpty() || !id || timeoutMs <= 0 || timeoutMs > 5000) return false;
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < timeoutMs) {
        QLocalSocket socket; socket.setReadBufferSize(4); socket.connectToServer(channelName());
        const int remaining = timeoutMs - int(elapsed.elapsed());
        if (remaining <= 0) return false;
        if (!socket.waitForConnected(qMin(remaining, 100))) { QThread::msleep(10); continue; }
        const QByteArray message = id->toLatin1() + '\n';
        if (socket.write(message) != message.size()) return false;
        if (socket.bytesToWrite() && !socket.waitForBytesWritten(qMax(1, timeoutMs - int(elapsed.elapsed())))) return false;
        QByteArray ack;
        while (ack.size() < 3 && elapsed.elapsed() < timeoutMs) {
            ack += socket.read(4 - ack.size());
            if (ack.size() >= 3) break;
            if (!socket.waitForReadyRead(qMax(1, timeoutMs - int(elapsed.elapsed())))) { ack += socket.read(4 - ack.size()); break; }
        }
        return ack == QByteArrayLiteral("OK\n");
    }
    return false;
}
bool CollectionLinkActivation::submit(const QUrl &url) {
    const auto id = Colosseum::Collections::shareIdFromUrl(url, WebOrigin, m_legacyOrigin);
    return id && m_mailbox->offer(*id);
}
void CollectionLinkActivation::drain() {
    QUrl url; QString ticket;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->notificationPosted = false;
        if (m_mailbox->stopped || !m_ready || !m_mailbox->pending) return;
        if (m_ticketSequence == std::numeric_limits<quint64>::max()) return;
        m_mailbox->pending = false;
        url = Colosseum::Collections::publicShareLink(m_mailbox->latestId, WebOrigin);
        ticket = m_presentedTicket = QString::number(++m_ticketSequence);
    }
    emit openRequested(url, ticket);
}
void CollectionLinkActivation::setDestinationReady(bool ready) {
    m_ready = ready && !m_mailbox->stopped;
    if (m_ready) { QMutexLocker lock(&m_mailbox->mutex); m_mailbox->postLocked(); }
}
void CollectionLinkActivation::clearPending() {
    QMutexLocker lock(&m_mailbox->mutex);
    m_mailbox->latestId.clear(); m_mailbox->pending = false; m_presentedTicket.clear();
}
void CollectionLinkActivation::deferForProfile() {
    m_ready = false;
    QMutexLocker lock(&m_mailbox->mutex);
    m_mailbox->pending = !m_mailbox->latestId.isEmpty();
}
void CollectionLinkActivation::registerPlatformHandlers() {
    if (m_mailbox->stopped || m_handlers) return;
    QDesktopServices::setUrlHandler(QStringLiteral("colosseum"), this, "handleUrl");
#if defined(Q_OS_ANDROID) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "handleUrl");
#endif
    m_handlers = true;
}
void CollectionLinkActivation::handleUrl(const QUrl &url) {
    if (m_mailbox->stopped) return;
    if (!Colosseum::Collections::shareIdFromUrl(url, WebOrigin, m_legacyOrigin)) {
        // Qt documents calling openUrl with the same argument *inside* its handler
        // as the native fallback. Keep that call synchronous on the handler thread.
        if (url.scheme() == QLatin1String("https")) QDesktopServices::openUrl(url);
        return;
    }
    submit(url);
}
bool CollectionLinkActivation::shutdown(int timeoutMs) {
    if (m_mailbox->stopped.exchange(true)) return !m_listener || m_listener->isFinished();
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->receiver = nullptr;
        m_mailbox->latestId.clear(); m_mailbox->pending = false;
    }
    if (m_handlers) {
        QDesktopServices::unsetUrlHandler(QStringLiteral("colosseum"));
#if defined(Q_OS_ANDROID) || defined(Q_OS_MACOS) || defined(Q_OS_IOS)
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
#endif
        m_handlers = false;
    }
    m_ready = false; m_presentedTicket.clear();
    if (!m_listener) return true;
    m_listener->quit();
    // Zero is an explicitly nonblocking stop. Parentless listener/mailbox/lock
    // lease safely outlive this owner until finished/deferred cleanup.
    if (timeoutMs <= 0 || !m_listener->wait(qMin(timeoutMs, 1500))) return false;
    delete m_listener.data(); m_listener = nullptr; return true;
}
bool CollectionLinkActivation::dismissPresented(const QString &ticket) {
    QMutexLocker lock(&m_mailbox->mutex);
    // Includes an IPC arrival not yet processed by any GUI notification.
    if (m_mailbox->stopped || m_mailbox->pending || ticket.isEmpty() || ticket != m_presentedTicket) return false;
    m_mailbox->latestId.clear(); m_presentedTicket.clear(); return true;
}

bool CollectionLinkActivation::deferPresented(const QString &ticket) {
    QMutexLocker lock(&m_mailbox->mutex);
    if (m_mailbox->stopped || ticket.isEmpty() || ticket != m_presentedTicket) return false;
    // Retry the retained latest intent, not a new arrival of the rejected URL.
    m_mailbox->pending = !m_mailbox->latestId.isEmpty();
    m_mailbox->postLocked();
    return m_mailbox->pending;
}
