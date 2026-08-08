#include "update/UpdateDownload.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <iostream>

using namespace Colosseum::Update;

namespace {

constexpr qint64 kPayloadSize = 2 * 1024 * 1024;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray payload()
{
    QByteArray bytes(kPayloadSize, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(i % 251);
    return bytes;
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

class DownloadServer final : public QTcpServer {
public:
    QByteArray bytes = payload();
    QByteArray lastRequest;
    QByteArray lastRange;
    QByteArray lastIfRange;
    QByteArray etag = QByteArrayLiteral("\"download-1\"");
    bool slowChunks = false;
    bool ignoreRange = false;
    bool changedEtag = false;
    bool truncated = false;
    bool wrongLength = false;

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/download/installer.exe").arg(serverPort()));
    }

    void reset()
    {
        lastRequest.clear();
        lastRange.clear();
        lastIfRange.clear();
        slowChunks = false;
        ignoreRange = false;
        changedEtag = false;
        truncated = false;
        wrongLength = false;
        etag = QByteArrayLiteral("\"download-1\"");
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            socket->deleteLater();
            return;
        }
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            if (!request.contains("\r\n\r\n"))
                return;
            lastRequest = request;
            const int rangeStart = request.indexOf("Range: ");
            if (rangeStart >= 0) {
                const int end = request.indexOf("\r\n", rangeStart);
                lastRange = request.mid(rangeStart + 7, end - rangeStart - 7).trimmed();
            }
            const int ifRangeStart = request.indexOf("If-Range: ");
            if (ifRangeStart >= 0) {
                const int end = request.indexOf("\r\n", ifRangeStart);
                lastIfRange = request.mid(ifRangeStart + 10, end - ifRangeStart - 10).trimmed();
            }

            qint64 offset = 0;
            const bool hasRange = !lastRange.isEmpty();
            if (hasRange && lastRange.startsWith("bytes="))
                offset = lastRange.mid(6).split('-').value(0).toLongLong();
            const bool serveRange = hasRange && !ignoreRange && !changedEtag && offset >= 0
                && offset < bytes.size();
            const int status = serveRange ? 206 : 200;
            const QByteArray responseEtag = changedEtag ? QByteArrayLiteral("\"download-2\"") : etag;
            QByteArray body = serveRange ? bytes.mid(offset) : bytes;
            if (truncated)
                body = body.left(body.size() / 2);
            qint64 advertised = body.size();
            if (wrongLength)
                ++advertised;
            QByteArray response = "HTTP/1.1 " + QByteArray::number(status)
                + (status == 206 ? " Partial Content\r\n" : " OK\r\n");
            response += "ETag: " + responseEtag + "\r\nContent-Length: "
                + QByteArray::number(advertised) + "\r\nConnection: close\r\n\r\n";
            socket->write(response);
            socket->flush();
            if (slowChunks)
                writeChunks(socket, body, 0);
            else {
                socket->write(body);
                socket->flush();
                socket->disconnectFromHost();
            }
        });
    }

private:
    void writeChunks(QTcpSocket* socket, const QByteArray& body, int offset)
    {
        if (!socket->isOpen())
            return;
        if (offset >= body.size()) {
            socket->disconnectFromHost();
            return;
        }
        const int count = qMin(64 * 1024, body.size() - offset);
        socket->write(body.constData() + offset, count);
        socket->flush();
        QTimer::singleShot(5, socket, [this, socket, body, offset, count] {
            writeChunks(socket, body, offset + count);
        });
    }
};

DownloadRequest requestFor(const DownloadServer& server, const QByteArray& bytes,
                           const QString& assetName = QStringLiteral("installer.exe"))
{
    DownloadRequest request;
    request.version = Version{1, 1, 1};
    request.url = server.url();
    request.assetName = assetName;
    request.expectedSize = bytes.size();
    request.expectedSha256 = digest(bytes);
    request.expectedEtag = QStringLiteral("\"download-1\"");
    request.allowHttpForTests = true;
    return request;
}

struct Outcome {
    bool completed = false;
    QString path;
    QString error;
    bool resumable = false;
    qint64 lastProgress = 0;
};

Outcome runDownload(UpdateCache& cache, DownloadServer& server, const DownloadRequest& request,
                    qint64 cancelAt = -1)
{
    QNetworkAccessManager nam;
    UpdateDownload download(&nam, &cache);
    Outcome outcome;
    QEventLoop loop;
    QObject::connect(&download, &UpdateDownload::progress, &loop,
                     [&](qint64 received, qint64, qint64) {
                         outcome.lastProgress = received;
                         if (cancelAt >= 0 && received >= cancelAt)
                             download.cancel();
                     });
    QObject::connect(&download, &UpdateDownload::completed, &loop,
                     [&](const QString& path) {
                         outcome.completed = true;
                         outcome.path = path;
                         loop.quit();
                     });
    QObject::connect(&download, &UpdateDownload::failed, &loop,
                     [&](const QString& error, bool resumable) {
                         outcome.error = error;
                         outcome.resumable = resumable;
                         loop.quit();
                     });
    download.start(request);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    require(outcome.completed || !outcome.error.isEmpty(), "download reaches a terminal signal");
    return outcome;
}

void seedPartial(UpdateCache& cache, DownloadServer& server, const DownloadRequest& request)
{
    server.slowChunks = true;
    const Outcome cancelled = runDownload(cache, server, request, 600 * 1024);
    require(cancelled.error == QStringLiteral("cancelled"), "cancel emits cancelled");
    const QString partPath = cache.partPath(request.version, request.assetName);
    require(QFileInfo(partPath).size() > 0 && QFileInfo(partPath).size() < request.expectedSize,
            "cancel persists a bounded partial file");
    const auto metadata = cache.readMetadata(request.version);
    require(metadata.has_value() && metadata->receivedBytes == QFileInfo(partPath).size(),
            "cancel persists received byte count");
    server.slowChunks = false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("UpdateDownloadHarness"));

    DownloadServer server;
    require(server.listen(QHostAddress::LocalHost), "download fixture server listens");
    const QByteArray bytes = server.bytes;
    const DownloadRequest request = requestFor(server, bytes);

    {
        QTemporaryDir temp;
        require(temp.isValid(), "fresh cache temp exists");
        UpdateCache cache(temp.path());
        const Outcome result = runDownload(cache, server, request);
        require(result.completed, "fresh streaming download completes");
        require(QFile(result.path).size() == bytes.size(), "promoted installer has expected size");
        QFile file(result.path);
        require(file.open(QIODevice::ReadOnly) && digest(file.readAll()) == request.expectedSha256,
                "promoted installer has expected hash");
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        seedPartial(cache, server, request);
        server.lastRange.clear();
        const Outcome result = runDownload(cache, server, request);
        require(result.completed, "range restart completes");
        require(server.lastRange.startsWith("bytes=") && server.lastIfRange == request.expectedEtag.toUtf8(),
                "range restart sends Range and If-Range");
        require(QFileInfo(result.path).size() == bytes.size(), "range restart promotes full installer");
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        seedPartial(cache, server, request);
        server.ignoreRange = true;
        const Outcome result = runDownload(cache, server, request);
        require(result.completed, "server-ignored range restarts from zero");
        require(server.lastRange.startsWith("bytes="), "ignored-range case attempted resume");
        server.reset();
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        seedPartial(cache, server, request);
        server.changedEtag = true;
        const Outcome result = runDownload(cache, server, request);
        require(result.completed, "changed ETag restarts from zero");
        require(server.lastRange.startsWith("bytes="), "changed ETag case attempted resume");
        server.reset();
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        server.truncated = true;
        const Outcome result = runDownload(cache, server, request);
        require(result.error == QStringLiteral("truncated_response") && result.resumable,
                "truncated response fails resumably");
        server.reset();
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        server.wrongLength = true;
        const Outcome result = runDownload(cache, server, request);
        require(result.error == QStringLiteral("wrong_length"), "wrong response length rejected");
        server.reset();
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        DownloadRequest wrong = request;
        wrong.expectedSha256[0] = static_cast<char>(wrong.expectedSha256.at(0) ^ 0x01);
        const Outcome result = runDownload(cache, server, wrong);
        require(result.error == QStringLiteral("sha256_mismatch"), "wrong SHA-256 rejected");
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        DownloadRequest unsafe = request;
        unsafe.assetName = QStringLiteral("../outside.exe");
        const Outcome result = runDownload(cache, server, unsafe);
        require(result.error == QStringLiteral("unsafe_asset_name"), "unsafe output path rejected");
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        cache.setAvailableSpaceOverrideForTests(0);
        const Outcome result = runDownload(cache, server, request);
        require(result.error == QStringLiteral("insufficient_space"), "insufficient space preflight rejects");
    }

    {
        QTemporaryDir temp;
        UpdateCache cache(temp.path());
        const Version oldVersion{1, 0, 0};
        const Version keepVersion{1, 1, 1};
        require(cache.ensureVersionDirectory(oldVersion) && cache.ensureVersionDirectory(keepVersion),
                "version cache directories create");
        const QString outside = temp.path() + QStringLiteral("-outside");
        QDir().mkpath(outside);
        require(cache.removeSuperseded(keepVersion), "superseded cleanup succeeds");
        require(!QDir(cache.rootPath() + QStringLiteral("/1.0.0")).exists(),
                "old version removed inside injected root");
        require(QDir(outside).exists(), "cleanup does not escape injected root");
        QDir(outside).removeRecursively();
    }

    std::cout << "UPDATE_DOWNLOAD_OK\n";
}
