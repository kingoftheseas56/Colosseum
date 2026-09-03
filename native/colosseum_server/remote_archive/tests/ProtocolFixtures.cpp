#include "ProtocolFixtures.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace TestProtocol {
namespace {

QByteArray readLine(QTcpSocket &socket)
{
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(5000)) return {};
    }
    return socket.readLine().trimmed();
}

bool writeWire(QTcpSocket &socket, const QByteArray &wire)
{
    if (socket.write(wire) != wire.size()) return false;
    return socket.waitForBytesWritten(5000);
}

void publishPort(std::mutex &mutex, std::condition_variable &cv,
                 bool &ready, quint16 &port, quint16 value)
{
    {
        std::lock_guard lock(mutex);
        port = value;
        ready = true;
    }
    cv.notify_all();
}

} // namespace

struct FtpFixture::Impl {
    QByteArray payload;
    std::atomic_bool stop{false};
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    quint16 boundPort = 0;
    std::thread worker;

    explicit Impl(QByteArray bytes) : payload(std::move(bytes))
    {
        worker = std::thread([this] { run(); });
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return ready; });
    }

    ~Impl()
    {
        stop.store(true, std::memory_order_release);
        if (worker.joinable()) worker.join();
    }

    void run()
    {
        QTcpServer control;
        if (!control.listen(QHostAddress::LocalHost, 0)) {
            publishPort(mutex, cv, ready, boundPort, 0);
            return;
        }
        publishPort(mutex, cv, ready, boundPort, control.serverPort());
        int served = 0;
        while (!stop.load(std::memory_order_acquire) && served < 3) {
            if (!control.waitForNewConnection(100)) continue;
            std::unique_ptr<QTcpSocket> socket(control.nextPendingConnection());
            if (!socket) continue;
            ++served;
            writeWire(*socket, "220 localhost fixture\r\n");
            qint64 restOffset = 0;
            std::unique_ptr<QTcpServer> dataServer;

            while (!stop.load(std::memory_order_acquire) &&
                   socket->state() == QAbstractSocket::ConnectedState) {
                const QByteArray command = readLine(*socket);
                if (command.isEmpty()) break;
                if (command.startsWith("USER ")) writeWire(*socket, "331 Password required\r\n");
                else if (command.startsWith("PASS ")) writeWire(*socket, "230 Logged in\r\n");
                else if (command == "TYPE I") writeWire(*socket, "200 Binary\r\n");
                else if (command.startsWith("MDTM ")) writeWire(*socket, "213 20260903110000\r\n");
                else if (command.startsWith("SIZE ")) {
                    writeWire(*socket, "213 " + QByteArray::number(payload.size()) + "\r\n");
                } else if (command == "FEAT") {
                    writeWire(*socket, "211-Features\r\n REST STREAM\r\n211 End\r\n");
                } else if (command == "EPSV") {
                    dataServer = std::make_unique<QTcpServer>();
                    if (!dataServer->listen(QHostAddress::LocalHost, 0)) break;
                    writeWire(*socket, "229 Entering Extended Passive Mode (|||" +
                              QByteArray::number(dataServer->serverPort()) + "|)\r\n");
                } else if (command.startsWith("REST ")) {
                    bool ok = false;
                    restOffset = command.mid(5).toLongLong(&ok);
                    writeWire(*socket, ok ? "350 Restart accepted\r\n" : "501 Bad restart\r\n");
                } else if (command.startsWith("RETR ")) {
                    if (!dataServer) { writeWire(*socket, "425 No data connection\r\n"); continue; }
                    writeWire(*socket, "150 Opening data\r\n");
                    if (!dataServer->waitForNewConnection(5000)) break;
                    std::unique_ptr<QTcpSocket> data(dataServer->nextPendingConnection());
                    if (!data) break;
                    writeWire(*data, payload.mid(restOffset));
                    data->disconnectFromHost();
                    if (data->state() != QAbstractSocket::UnconnectedState)
                        data->waitForDisconnected(2000);
                    data.reset();
                    dataServer.reset();
                    writeWire(*socket, "226 Transfer complete\r\n");
                } else if (command == "QUIT") {
                    writeWire(*socket, "221 Bye\r\n");
                    socket->disconnectFromHost();
                    break;
                } else {
                    writeWire(*socket, "500 Unsupported\r\n");
                }
            }
        }
    }
};

FtpFixture::FtpFixture(QByteArray payload) : m_impl(std::make_unique<Impl>(std::move(payload))) {}
FtpFixture::~FtpFixture() = default;
quint16 FtpFixture::port() const { return m_impl->boundPort; }
struct NntpFixture::Impl {
    QByteArray payload;
    std::atomic_bool stop{false};
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    quint16 boundPort = 0;
    std::thread worker;

    explicit Impl(QByteArray bytes) : payload(std::move(bytes))
    {
        worker = std::thread([this] { run(); });
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return ready; });
    }

    ~Impl()
    {
        stop.store(true, std::memory_order_release);
        if (worker.joinable()) worker.join();
    }

    QByteArray yenc() const
    {
        QByteArray encoded;
        for (unsigned char byte : payload) encoded.append(char(byte + 42));
        return encoded;
    }

    void run()
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            publishPort(mutex, cv, ready, boundPort, 0);
            return;
        }
        publishPort(mutex, cv, ready, boundPort, server.serverPort());
        while (!stop.load(std::memory_order_acquire)) {
            if (!server.waitForNewConnection(100)) continue;
            std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
            if (!socket) continue;
            writeWire(*socket, "200 localhost nntp fixture\r\n");
            while (!stop.load(std::memory_order_acquire) &&
                   socket->state() == QAbstractSocket::ConnectedState) {
                const QByteArray command = readLine(*socket);
                if (command.isEmpty()) break;
                if (command.startsWith("AUTHINFO USER ")) writeWire(*socket, "381 Password required\r\n");
                else if (command.startsWith("AUTHINFO PASS ")) writeWire(*socket, "281 Authentication accepted\r\n");
                else if (command.startsWith("GROUP ")) writeWire(*socket, "211 1 1 1 alt.binaries.test\r\n");
                else if (command.startsWith("BODY ")) {
                    QByteArray body = "222 0 article follows\r\n";
                    body += "=ybegin line=128 size=" + QByteArray::number(payload.size()) + " name=movie.mp4\r\n";
                    body += "=ypart begin=1 end=" + QByteArray::number(payload.size()) + "\r\n";
                    body += yenc() + "\r\n=yend size=" + QByteArray::number(payload.size()) + "\r\n.\r\n";
                    writeWire(*socket, body);
                } else if (command == "QUIT") {
                    writeWire(*socket, "205 Closing connection\r\n");
                    socket->disconnectFromHost();
                    return;
                } else {
                    writeWire(*socket, "500 Unsupported\r\n");
                }
            }
        }
    }
};

NntpFixture::NntpFixture(QByteArray payload) : m_impl(std::make_unique<Impl>(std::move(payload))) {}
NntpFixture::~NntpFixture() = default;
quint16 NntpFixture::port() const { return m_impl->boundPort; }

} // namespace TestProtocol
