#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QAbstractSocket>
#include <QHash>
#include <QHostAddress>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSGRendererInterface>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <chrono>
#include "vinyl/VinylLocalSource.h"
#include "vinyl/VinylBackendSession.h"
#include "vinyl/VinylLyricsService.h"
#include "vinyl/VinylGeniusService.h"
#include "vinyl/VinylRecommendationService.h"
#include "engine/VaultIdentity.h"
// Controllable transport boundary for cancellation and untrusted-video tests.
// Successful decoding remains exercised by the native player/provider journey.
class VideoTestSource : public QObject {
    Q_OBJECT
public:
    explicit VideoTestSource(QObject *parent=nullptr):QObject(parent){}
    Q_INVOKABLE int request(const QString &lane,const QString &operation,const QString &value,const QVariantMap &options) {
        const int id=++serial;emit requested(lane,id,operation,value,options);return id;
    }
    Q_INVOKABLE void cancel(const QString &lane){emit cancelled(lane);}
    Q_INVOKABLE void reply(const QString &lane,int id,const QVariantMap &data,const QString &error) {
        emit completed(lane,id,data,error);
    }
signals:
    void requested(const QString &lane,int id,const QString &operation,const QString &value,const QVariantMap &options);
    void cancelled(const QString &lane);
    void completed(const QString &lane,int id,const QVariant &data,const QString &error);
private:
    int serial=0;
};
class Setup : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    VaultIdentity *identity = nullptr;
    VinylLocalSource *source = nullptr;
    int identitySerial = 0;
    QTcpServer progressiveServer;
    QByteArray progressiveWav;
    QString progressiveWavPath;
    QHash<QTcpSocket *, QByteArray> progressiveRequestBuffers;
    QSet<QTcpSocket *> progressiveSockets;
    QSet<QTcpSocket *> heldProgressiveSockets;
    QVariantList progressiveRequestOffsets;
    QVariantList progressiveServedRanges;

    void acceptProgressiveConnections() {
        while (progressiveServer.hasPendingConnections()) {
            auto *socket = progressiveServer.nextPendingConnection();
            if (!socket) continue;
            progressiveSockets.insert(socket);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                auto &buffer = progressiveRequestBuffers[socket];
                buffer.append(socket->readAll());
                const auto headerEnd = buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                const QByteArray request = buffer.left(headerEnd);
                progressiveRequestBuffers.remove(socket);
                serveProgressiveRequest(socket, request);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                heldProgressiveSockets.remove(socket);
                progressiveRequestBuffers.remove(socket);
                progressiveSockets.remove(socket);
                socket->deleteLater();
            });
        }
    }

    void serveProgressiveRequest(QTcpSocket *socket, const QByteArray &request) {
        const QStringList lines = QString::fromLatin1(request).split(QStringLiteral("\r\n"));
        const QStringList requestLine = lines.value(0).split(QChar(' '));
        const QString method = requestLine.value(0);
        const QString path = requestLine.value(1);
        bool authorized = false;
        for (const auto &line : lines)
            authorized |= line.compare(QStringLiteral("X-Vinyl-Seek-Test: required"), Qt::CaseInsensitive) == 0;
        if (!authorized) {
            socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        if ((method != QStringLiteral("GET") && method != QStringLiteral("HEAD"))
            || path != QStringLiteral("/media")) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        qint64 start = 0;
        qint64 end = progressiveWav.size() - 1;
        bool hasRange = false;
        static const QRegularExpression rangePattern(
            QStringLiteral("^bytes=(\\d+)-(\\d*)$"), QRegularExpression::CaseInsensitiveOption);
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines.at(i).indexOf(QChar(':'));
            if (colon < 0 || lines.at(i).left(colon).compare(QStringLiteral("Range"), Qt::CaseInsensitive) != 0)
                continue;
            const auto match = rangePattern.match(lines.at(i).mid(colon + 1).trimmed());
            if (match.hasMatch()) {
                hasRange = true;
                start = match.captured(1).toLongLong();
                if (!match.captured(2).isEmpty()) end = qMin(end, match.captured(2).toLongLong());
            }
            break;
        }
        progressiveRequestOffsets.append(QVariant::fromValue<qlonglong>(start));
        if (progressiveWav.isEmpty() || start < 0 || start >= progressiveWav.size() || end < start) {
            socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        if (method == QStringLiteral("HEAD")) {
            QByteArray header = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: ")
                + QByteArray::number(progressiveWav.size());
            header += QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
            socket->write(header);
            socket->disconnectFromHost();
            return;
        }

        const qint64 length = end - start + 1;
        const qint64 frontier = qMin(qint64(progressiveWav.size()), qint64(44 + 20 * 8000 * 2));
        const qint64 availableBodyLength = qMax(qint64(0), frontier - start);
        const bool crossesFrontier = start < frontier && length > availableBodyLength;
        // The origin has bytes only through the twenty-second boundary. Every
        // sequential request crossing that boundary remains open after the
        // available prefix. Requests starting at or beyond it are fully served.
        if (method == QStringLiteral("GET") && crossesFrontier) {
            heldProgressiveSockets.insert(socket);
            QByteArray header;
            if (hasRange) {
                header = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes ")
                    + QByteArray::number(start) + '-'
                    + QByteArray::number(end) + '/' + QByteArray::number(progressiveWav.size()) + QByteArrayLiteral("\r\n");
            } else {
                header = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
            }
            header += QByteArrayLiteral("Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nConnection: keep-alive\r\nContent-Length: ")
                + QByteArray::number(length) + QByteArrayLiteral("\r\n\r\n");
            socket->write(header);
            socket->write(progressiveWav.constData() + start, int(availableBodyLength));
            progressiveServedRanges.append(QVariantMap{{"start", start},
                {"end", start + availableBodyLength - 1}, {"requestEnd", end}, {"held", true}});
            socket->flush();
            return;
        }

        const QByteArray body = progressiveWav.mid(int(start), int(length));
        QByteArray header;
        if (hasRange) {
            header = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes ")
                + QByteArray::number(start) + '-' + QByteArray::number(end) + '/'
                + QByteArray::number(progressiveWav.size()) + QByteArrayLiteral("\r\n");
        } else {
            header = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
        }
        header += QByteArrayLiteral("Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: ")
            + QByteArray::number(length) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
        socket->write(header);
        socket->write(body);
        progressiveServedRanges.append(QVariantMap{{"start", start}, {"end", end}, {"held", false}});
        socket->disconnectFromHost();
    }
public:
    Setup() {
        connect(&progressiveServer, &QTcpServer::newConnection,
                this, [this] { acceptProgressiveConnections(); });
    }
    ~Setup() override { stopProgressiveHttpFixture(); }

public slots:
    qint64 monotonicMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void applicationAvailable() { QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL); }
    QString createIdentityAudio() {
        const QString path=directory.path()+QString("/identity-%1.wav").arg(++identitySerial);
        changeIdentityAudio(path,160);
        return path;
    }
    QString createTaggedAudio(const QString &title,const QString &artist,const QString &album) {
        const auto path=createIdentityAudio();changeIdentityAudio(path,16000);
        QByteArray info("INFO",4);
        const auto append=[&info](const char *key,const QString &value){
            const auto text=value.toUtf8()+char(0);QByteArray field;
            QDataStream bytes(&field,QIODevice::WriteOnly);bytes.setByteOrder(QDataStream::LittleEndian);
            bytes.writeRawData(key,4);bytes<<quint32(text.size());bytes.writeRawData(text.constData(),text.size());
            if(text.size()%2)bytes<<quint8(0);info+=field;
        };
        append("INAM",title);append("IART",artist);append("IPRD",album);
        QFile file(path);if(!file.open(QIODevice::ReadWrite))qFatal("Cannot tag audio fixture");
        file.seek(file.size());QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("LIST",4);stream<<quint32(info.size());stream.writeRawData(info.constData(),info.size());
        const auto length=file.size();file.seek(4);stream<<quint32(length-8);return path;
    }
    void changeIdentityAudio(const QString &path, int frames) {
        QFile file(path);if(!file.open(QIODevice::WriteOnly)) qFatal("Cannot write identity audio");
        QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF",4);stream << quint32(36+frames*2);stream.writeRawData("WAVEfmt ",8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data",4);stream << quint32(frames*2);
        for(int i=0;i<frames;++i) stream << qint16(i%2 ? 1000 : -1000);
    }
    QString startProgressiveHttpFixture() {
        if (progressiveServer.isListening())
            return QStringLiteral("http://127.0.0.1:%1/media").arg(progressiveServer.serverPort());
        stopProgressiveHttpFixture();
        progressiveWavPath = directory.path() + QStringLiteral("/progressive-120s.wav");
        changeIdentityAudio(progressiveWavPath, 120 * 8000);
        QFile file(progressiveWavPath);
        if (!file.open(QIODevice::ReadOnly)) return {};
        progressiveWav = file.readAll();
        progressiveRequestOffsets.clear();
        progressiveServedRanges.clear();
        if (!progressiveServer.listen(QHostAddress::LocalHost, 0)) return {};
        return QStringLiteral("http://127.0.0.1:%1/media").arg(progressiveServer.serverPort());
    }
    QString progressiveHttpWavUrl() const { return QUrl::fromLocalFile(progressiveWavPath).toString(); }
    QVariantList progressiveHttpRequestOffsets() const { return progressiveRequestOffsets; }
    QVariantList progressiveHttpServedRanges() const { return progressiveServedRanges; }
    bool progressiveHttpIsHolding() const {
        for (auto *socket : heldProgressiveSockets) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState)
                return true;
        }
        return false;
    }
    void stopProgressiveHttpFixture() {
        progressiveServer.close();
        const auto sockets = progressiveSockets;
        for (auto *socket : sockets) {
            if (!socket) continue;
            socket->abort();
            socket->deleteLater();
        }
        progressiveSockets.clear();
        heldProgressiveSockets.clear();
        progressiveRequestBuffers.clear();
        progressiveRequestOffsets.clear();
        progressiveServedRanges.clear();
        progressiveWav.clear();
        progressiveWavPath.clear();
    }
    void refreshManifest() {
        changeIdentityAudio(directory.path()+"/two.wav",16000);
        QFile manifest(directory.path()+"/record.vinyl.json");if(!manifest.open(QIODevice::WriteOnly)) qFatal("Cannot refresh bridge manifest");
        manifest.write(R"({"version":1,"artist":{"key":"artist","name":"Native Artist"},"releases":[{"key":"release","title":"Fresh Native Release","editions":[{"key":"standard","tracks":[{"file":"two.wav","title":"New Track","disc":1,"number":1}]}]}]})");manifest.close();
    }

    void qmlEngineAvailable(QQmlEngine *engine) {
        qmlRegisterType<VideoTestSource>("Colosseum.Vinyl",1,0,"VideoTestSource");
        qmlRegisterType<VinylBackendSession>("Colosseum.Vinyl",1,0,"VinylBackendSession");
        qmlRegisterType<VinylLyricsService>("Colosseum.Vinyl",1,0,"VinylLyricsService");
        qmlRegisterType<VinylGeniusService>("Colosseum.Vinyl",1,0,"VinylGeniusService");
        qmlRegisterType<VinylRecommendationService>("Colosseum.Vinyl",1,0,"VinylRecommendationService");
        engine->addImportPath(QStringLiteral(VINYL_CONTRACT_IMPORTS));
        QDir().mkpath(directory.path()+"/identity");
        changeIdentityAudio(directory.path()+"/one.wav",16000);
        QFile manifest(directory.path()+"/record.vinyl.json");if(!manifest.open(QIODevice::WriteOnly)) qFatal("Cannot create bridge manifest");
        manifest.write(R"({"version":1,"artist":{"key":"artist","name":"Native Artist"},"releases":[{"key":"release","title":"Native Release","editions":[{"key":"standard","tracks":[{"file":"one.wav","title":"Native Track","disc":1,"number":1,"seconds":2}]}]}]})");manifest.close();
        identity=new VaultIdentity(directory.path()+"/identity");
        source=new VinylLocalSource(identity,this);
        engine->rootContext()->setContextProperty("NativeSource",source);
        engine->rootContext()->setContextProperty("BridgeFixtures",this);
        engine->rootContext()->setContextProperty("ManifestPath",manifest.fileName());
    }
};
QUICK_TEST_MAIN_WITH_SETUP(vinyl_native_session_bridge,Setup)
#include "runner.moc"
