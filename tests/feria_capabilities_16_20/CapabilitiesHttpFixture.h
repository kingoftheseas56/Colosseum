#pragma once
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>
#include <cmath>

// Loopback HTTP behavior fixture, not a download implementation. The real
// QtWebEngine request owns transfer state; gates only control server delivery.
class CapabilitiesHttpFixture {
public:
    QByteArray payload() const {
        QByteArray bytes(1024 * 1024, Qt::Uninitialized);
        for (qsizetype i = 0; i < bytes.size(); ++i) bytes[i] = char((i * 31 + 17) % 251);
        return bytes;
    }
    QString expectedChecksum(const QString &key) const {
        if (m_hashes.contains(key)) return m_hashes.value(key);
        return QString::fromLatin1(QCryptographicHash::hash(payload(), QCryptographicHash::Sha256).toHex());
    }
    QVariantList requestLog() const { return m_log; }
    int requestCount(const QString &key) const {
        int count = 0;
        for (const auto &item : m_log) {
            const auto row = item.toMap();
            if (row.value("gate").toString() == key || row.value("path").toString() == key) ++count;
        }
        return count;
    }
    int release(const QString &key) {
        m_released.insert(key);
        int count = 0;
        for (auto &pending : m_pending) {
            if (pending.key != key || !pending.socket) continue;
            auto *socket = pending.socket.data();
            if (socket->state() != QAbstractSocket::ConnectedState) continue;
            socket->write(pending.headers);
            socket->write(pending.remaining);
            updateBytes(pending.logIndex, pending.remaining.size());
            pending.remaining.clear();
            socket->disconnectFromHost();
            pending.socket.clear();
            ++count;
        }
        return count;
    }
    int interrupt(const QString &key) {
        int count = 0;
        for (auto &pending : m_pending) {
            if (pending.key == key && pending.socket) {
                pending.socket->abort(); pending.socket.clear(); ++count;
            }
        }
        return count;
    }
    void reset() {
        for (auto &pending : m_pending) if (pending.socket) pending.socket->abort();
        m_pending.clear(); m_released.clear(); m_log.clear();
    }
    bool serve(QTcpSocket *socket, const QByteArray &request) {
        const auto fields = request.left(request.indexOf("\r\n")).split(' ');
        const QByteArray method = fields.value(0);
        const QUrl url = QUrl::fromEncoded(fields.value(1));
        const QString path = url.path();
        const QUrlQuery query(url);
        if (path != "/download" && path != "/gated-download" && path != "/media" && path != "/media-frame"
            && path != "/media-frame-replacement" && path != "/media.wav"
            && path != "/tone.wav" && path != "/media.webm" && path != "/save-context"
            && path != "/save-page" && path != "/image.png") return false;
        QByteArray range, referer;
        for (const auto &line : request.left(request.indexOf("\r\n\r\n")).split('\n')) {
            if (line.toLower().startsWith("range:")) range = line.mid(6).trimmed();
            if (line.toLower().startsWith("referer:")) referer = line.mid(8).trimmed();
        }
        const QString key = query.queryItemValue("gate");
        const int logIndex = m_log.size();
        m_log.append(QVariantMap{{"method", QString::fromLatin1(method)}, {"path", path},
            {"url", url.toString()}, {"referer", QString::fromLatin1(referer)}, {"gate", key}, {"range", QString::fromLatin1(range)},
            {"rangeStart", 0}, {"status", 200}, {"bytesSent", 0}});
        QByteArray body, mime("text/html; charset=utf-8"), disposition;
        bool downloadable = path == "/download" || path == "/gated-download";
        if (downloadable) {
            body = query.queryItemValue("bytes") == "0" ? QByteArray{} : payload(); mime = "application/octet-stream";
            m_hashes.insert(key, QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()));
            QString name = query.queryItemValue("name");
            if (name.isEmpty()) name = QStringLiteral("fixture.bin");
            name.remove('\r'); name.remove('\n'); name.remove('"');
            disposition = "Content-Disposition: attachment; filename=\"" + name.toUtf8() + "\"\r\n";
        } else if (path == "/media.wav" || path == "/tone.wav") {
            mime = "audio/wav"; QDataStream out(&body, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            constexpr int frames = 8000 * 8;
            out.writeRawData("RIFF", 4); out << quint32(36 + frames * 2);
            out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
            out << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
            out.writeRawData("data", 4); out << quint32(frames * 2);
            for (int i = 0; i < frames; ++i) out << qint16(1500 * std::sin(6.283185307179586 * 440 * i / 8000));
        } else if (path == "/media.webm") {
            mime = "video/webm";
            QFile file(QStringLiteral(FERIA_FIXTURE_ASSET_DIR "/media.webm"));
            if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read generated WebM fixture");
            body = file.readAll();
        } else if (path == "/image.png") {
            mime = "image/png";
            QImage image(80, 60, QImage::Format_RGB32); image.fill(QColor("#60a5fa"));
            QBuffer buffer(&body); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        } else if (path == "/media") {
            body = R"HTML(<!doctype html><title>Fixture /media</title><body style="background:#173044;color:white;font:18px sans-serif">
<h1>Real QtWebEngine media ownership fixture</h1><p>Playing audio, manually paused audio, and actual VP8/Vorbis video.</p>
<audio id="playing" controls loop preload="auto" src="/media.wav"></audio>
<audio id="manual" controls loop preload="auto" src="/media.wav"></audio>
<video id="video" controls loop muted preload="auto" width="320" height="180" src="/media.webm"></video></body>)HTML";
        } else if (path == "/media-frame" || path == "/media-frame-replacement") {
            body = "<!doctype html><title>" + path.toUtf8() + "</title><audio id='frameAudio' controls loop preload='auto' src='/media.wav'></audio>";
        } else if (path == "/save-context") {
            body = R"HTML(<!doctype html><title>Fixture /save-context</title><body style="background:#173044;color:white;font:18px sans-serif">
<h1>Native Save/context fixture</h1><a id="linkA" href="/download?name=alpha.bin">Alpha download</a>
<a id="linkB" href="/download?name=beta.bin">Beta download</a><p><img id="image" src="/image.png" width="160" height="120"></p>
<audio id="audio" controls src="/media.wav"></audio><canvas id="canvas" width="120" height="80"></canvas>
<a id="javascriptLink" href="javascript:void(0)">Ineligible script link</a><p>FERIA_SAVE_CONTEXT_MARKER</p>
<script>let c=document.getElementById('canvas').getContext('2d');c.fillStyle='#fbbf24';c.fillRect(0,0,120,80)</script></body>)HTML";
        } else {
            body = "<!doctype html><title>Fixture /save-page</title><h1>FERIA_SAVE_PAGE_MARKER</h1><img src='/image.png'><p>Native save page fixture.</p>";
        }
        m_hashes.insert(path, QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()));
        const qsizetype fullSize = body.size();
        qsizetype start = 0, end = fullSize - 1;
        bool partial = false;
        if (range.startsWith("bytes=")) {
            const auto specification = range.mid(6).split('-');
            bool valid = false;
            start = specification.value(0).toLongLong(&valid);
            if (!valid || start < 0 || start >= fullSize) {
                socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + QByteArray::number(fullSize)
                    + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost(); return true;
            }
            if (!specification.value(1).isEmpty()) end = qMin(end, qsizetype(specification.value(1).toLongLong()));
            if (end < start) end = fullSize - 1;
            body = body.mid(start, end - start + 1); partial = true;
        }
        auto row = m_log[logIndex].toMap(); row.insert("rangeStart", qlonglong(start)); row.insert("status", partial ? 206 : 200);
        m_log[logIndex] = row;
        QByteArray headers = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        headers += "Content-Type: " + mime + "\r\nAccept-Ranges: bytes\r\nETag: \"feria-fixture-v1\"\r\n" + disposition;
        if (partial) headers += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(fullSize) + "\r\n";
        if (query.queryItemValue("unknown") != "1") headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        headers += "Connection: close\r\n\r\n";
        if (path == "/gated-download" && !key.isEmpty() && !m_released.contains(key)) {
            m_pending.append(Pending{socket, key, body, logIndex, headers});
            return true;
        }
        socket->write(headers);
        if (method == "HEAD") { socket->disconnectFromHost(); return true; }
        if (downloadable && !key.isEmpty() && !m_released.contains(key)) {
            const qsizetype initial = qMin(qsizetype(64 * 1024), body.size());
            socket->write(body.left(initial)); socket->flush(); updateBytes(logIndex, initial);
            m_pending.append(Pending{socket, key, body.mid(initial), logIndex});
        } else {
            socket->write(body); updateBytes(logIndex, body.size()); socket->disconnectFromHost();
        }
        return true;
    }
private:
    struct Pending { QPointer<QTcpSocket> socket; QString key; QByteArray remaining; int logIndex; QByteArray headers; };
    void updateBytes(int index, qlonglong bytes) {
        if (index < 0 || index >= m_log.size()) return;
        auto row = m_log[index].toMap(); row.insert("bytesSent", row.value("bytesSent").toLongLong() + bytes); m_log[index] = row;
    }
    QList<Pending> m_pending;
    QSet<QString> m_released;
    QHash<QString, QString> m_hashes;
    QVariantList m_log;
};
