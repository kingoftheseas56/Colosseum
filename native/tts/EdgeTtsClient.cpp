#include "EdgeTtsClient.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#ifdef HAS_WEBSOCKETS
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketProtocol>
#include <QtNetwork/QNetworkRequest>
#endif

// ── Endpoint constants (public knowledge from rany2/edge-tts ecosystem) ─────
namespace {

// Microsoft Edge consumer Read Aloud trusted client token. Public constant
// reverse-engineered by the rany2/edge-tts community in 2021; stable since.
constexpr const char* kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";

// Endpoint base. Connection params (TrustedClientToken, ConnectionId,
// Sec-MS-GEC, Sec-MS-GEC-Version) are appended at handshake time.
constexpr const char* kWssBase =
    "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1";

// Edge browser version pinned to the current public edge-tts protocol value.
// The endpoint rejects stale Sec-MS-GEC-Version values with HTTP 403 before
// the WebSocket upgrade, so keep this aligned with rany2/edge-tts constants.
constexpr const char* kEdgeVersion = "1-143.0.3650.75";

// User-Agent + Origin spoofed to match what real Edge sends. Microsoft's
// endpoint does not strictly require these but inconsistent values can
// trigger rate-limit / soft-block behavior reported by the rany2 community.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/143.0.3650.75 Safari/537.36 "
    "Edg/143.0.3650.75";

constexpr const char* kOrigin =
    "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold";

// Default output format. Phase 2 will use this in speech.config; Phase 1 uses
// it in the probe round-trip too.
constexpr const char* kDefaultOutputFormat = "audio-24khz-48kbitrate-mono-mp3";

// Probe text — short enough to round-trip in <2s on a healthy network.
constexpr const char* kProbeText = "Edge probe.";

// Maximum time we wait for a probe round-trip before giving up.
constexpr int kProbeTimeoutMs = 8000;

}  // namespace

// ── Static helpers (testable, no instance state) ─────────────────────────────

QString EdgeTtsClient::trustedClientToken() {
    return QString::fromLatin1(kTrustedClientToken);
}

QString EdgeTtsClient::wssBaseUrl() {
    return QString::fromLatin1(kWssBase);
}

QString EdgeTtsClient::edgeVersion() {
    return QString::fromLatin1(kEdgeVersion);
}

QString EdgeTtsClient::edgeUserAgent() {
    return QString::fromLatin1(kUserAgent);
}

QString EdgeTtsClient::edgeOrigin() {
    return QString::fromLatin1(kOrigin);
}

QString EdgeTtsClient::generateSecMSGEC(qint64 unixSecondsSinceEpoch) {
    // File-time epoch (1601-01-01) is 11644473600 seconds before Unix epoch.
    // Each tick is 100ns; 5-minute window = 5 * 60 * 10_000_000 = 3_000_000_000.
    constexpr qint64 kFileTimeEpochOffset = 11644473600LL;
    constexpr qint64 kTicksPerSecond = 10000000LL;
    constexpr qint64 kFiveMinTicks = 3000000000LL;

    qint64 ticks = (unixSecondsSinceEpoch + kFileTimeEpochOffset) * kTicksPerSecond;
    ticks -= ticks % kFiveMinTicks;

    const QString concat = QString::number(ticks) + QString::fromLatin1(kTrustedClientToken);
    const QByteArray digest = QCryptographicHash::hash(concat.toLatin1(),
                                                      QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().toUpper());
}

// ── Construction / destruction ───────────────────────────────────────────────

EdgeTtsClient::EdgeTtsClient(QObject* parent) : QObject(parent) {
    buildVoiceTable();
}

EdgeTtsClient::~EdgeTtsClient() {
#ifdef HAS_WEBSOCKETS
    closeSocket();
#endif
}

// ── Voice table ──────────────────────────────────────────────────────────────

void EdgeTtsClient::buildVoiceTable() {
    // Reimplemented from Edge Read Aloud's public voice list (Microsoft's
    // ssml/voice-list endpoint is the source of truth). Subset here covers
    // the four English locales the HUD groups by (`tts_hud.js` :257).
    // Future: dynamic refresh from the live voice-list endpoint if voices drift.
    struct VoiceEntry {
        const char* name;
        const char* locale;
        const char* gender;
        const char* displayName;
    };
    static constexpr VoiceEntry kEntries[] = {
        // en-US
        {"en-US-AriaNeural",        "en-US", "Female", "Aria"},
        {"en-US-AnaNeural",         "en-US", "Female", "Ana"},
        {"en-US-AndrewNeural",      "en-US", "Male",   "Andrew"},
        {"en-US-AndrewMultilingualNeural", "en-US", "Male", "Andrew (Multilingual)"},
        {"en-US-BrianNeural",       "en-US", "Male",   "Brian"},
        {"en-US-ChristopherNeural", "en-US", "Male",   "Christopher"},
        {"en-US-EmmaNeural",        "en-US", "Female", "Emma"},
        {"en-US-EricNeural",        "en-US", "Male",   "Eric"},
        {"en-US-GuyNeural",         "en-US", "Male",   "Guy"},
        {"en-US-JennyNeural",       "en-US", "Female", "Jenny"},
        {"en-US-MichelleNeural",    "en-US", "Female", "Michelle"},
        {"en-US-RogerNeural",       "en-US", "Male",   "Roger"},
        {"en-US-SteffanNeural",     "en-US", "Male",   "Steffan"},
        // en-GB
        {"en-GB-LibbyNeural",       "en-GB", "Female", "Libby"},
        {"en-GB-MaisieNeural",      "en-GB", "Female", "Maisie"},
        {"en-GB-RyanNeural",        "en-GB", "Male",   "Ryan"},
        {"en-GB-SoniaNeural",       "en-GB", "Female", "Sonia"},
        {"en-GB-ThomasNeural",      "en-GB", "Male",   "Thomas"},
        // en-AU
        {"en-AU-NatashaNeural",     "en-AU", "Female", "Natasha"},
        {"en-AU-WilliamNeural",     "en-AU", "Male",   "William"},
        // en-IN
        {"en-IN-NeerjaExpressiveNeural", "en-IN", "Female", "Neerja (Expressive)"},
        {"en-IN-NeerjaNeural",      "en-IN", "Female", "Neerja"},
        {"en-IN-PrabhatNeural",     "en-IN", "Male",   "Prabhat"},
    };

    QJsonArray arr;
    for (const auto& v : kEntries) {
        QJsonObject o;
        o.insert("name", QString::fromLatin1(v.name));
        o.insert("locale", QString::fromLatin1(v.locale));
        o.insert("gender", QString::fromLatin1(v.gender));
        o.insert("displayName", QString::fromLatin1(v.displayName));
        // Mirror Web Speech API surface that tts_hud.js's existing voice
        // populator expects (it reads `voiceURI` or `name`, `lang`).
        o.insert("voiceURI", QString::fromLatin1(v.name));
        o.insert("lang", QString::fromLatin1(v.locale));
        arr.append(o);
    }
    m_voices = arr;
}

// ── Probe / synth ────────────────────────────────────────────────────────────

#ifndef HAS_WEBSOCKETS

namespace {

struct ManualFrame {
    bool ok = false;
    quint8 opcode = 0;
    QByteArray payload;
    QString error;
};

struct ManualRoundTripOutcome {
    bool gotAudio = false;
    bool gotTurnEnd = false;
    QByteArray mp3;
    QJsonArray boundaries;
    QString errorReason;
};

QString manualConnectionId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').toLower();
}

QString manualRequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').toLower();
}

QString manualMuidCookie() {
    QString out;
    out.reserve(32);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 32; ++i) {
        const int v = rng->bounded(16);
        out.append(QChar::fromLatin1(v < 10 ? '0' + v : 'A' + (v - 10)));
    }
    return out;
}

QString manualTimestamp() {
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
}

QString manualSanitizeXml(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar c : raw) {
        if (c == QChar('&')) out.append(QStringLiteral("&amp;"));
        else if (c == QChar('<')) out.append(QStringLiteral("&lt;"));
        else if (c == QChar('>')) out.append(QStringLiteral("&gt;"));
        else if (c == QChar('"')) out.append(QStringLiteral("&quot;"));
        else if (c == QChar('\'')) out.append(QStringLiteral("&apos;"));
        else if (c.isPrint() || c.isSpace()) out.append(c);
    }
    return out;
}

QString manualRatePercent(double rate) {
    const int pct = static_cast<int>((rate - 1.0) * 100.0 + (rate >= 1.0 ? 0.5 : -0.5));
    return pct >= 0 ? QStringLiteral("+%1%").arg(pct)
                    : QStringLiteral("%1%").arg(pct);
}

QString manualPitchHz(double pitch) {
    const int hz = static_cast<int>((pitch - 1.0) * 50.0);
    return hz >= 0 ? QStringLiteral("+%1Hz").arg(hz)
                   : QStringLiteral("%1Hz").arg(hz);
}

QString manualSpeechConfigMessage(const QString& outputFormat) {
    QJsonObject metadataOpts{
        {"sentenceBoundaryEnabled", QStringLiteral("false")},
        {"wordBoundaryEnabled", QStringLiteral("true")},
    };
    QJsonObject audio{
        {"metadataoptions", metadataOpts},
        {"outputFormat", outputFormat},
    };
    QJsonObject synthesis{{"audio", audio}};
    QJsonObject context{{"synthesis", synthesis}};
    QJsonObject root{{"context", context}};
    const QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);

    return QStringLiteral("X-Timestamp:%1\r\n"
                          "Content-Type:application/json; charset=utf-8\r\n"
                          "Path:speech.config\r\n\r\n%2")
        .arg(manualTimestamp(), QString::fromUtf8(body));
}

QString manualSsmlMessage(const QString& requestId, const QString& text,
                          const QString& voice, double rate, double pitch) {
    QString locale = QStringLiteral("en-US");
    const int dash2 = voice.indexOf('-', voice.indexOf('-') + 1);
    if (dash2 > 0) locale = voice.left(dash2);

    const QString ssml =
        QStringLiteral("<speak version='1.0' "
                       "xmlns='http://www.w3.org/2001/10/synthesis' "
                       "xml:lang='%1'>"
                       "<voice name='%2'>"
                       "<prosody pitch='%3' rate='%4' volume='+0%'>%5</prosody>"
                       "</voice></speak>")
            .arg(locale, voice, manualPitchHz(pitch), manualRatePercent(rate),
                 manualSanitizeXml(text));

    return QStringLiteral("X-RequestId:%1\r\n"
                          "Content-Type:application/ssml+xml\r\n"
                          "X-Timestamp:%2\r\n"
                          "Path:ssml\r\n\r\n%3")
        .arg(requestId, manualTimestamp(), ssml);
}

QByteArray manualParseBinaryFrame(const QByteArray& raw, QString* outPath) {
    if (outPath) outPath->clear();
    if (raw.size() < 2) return {};
    const quint16 headerLen = (static_cast<quint8>(raw[0]) << 8) |
                              static_cast<quint8>(raw[1]);
    if (raw.size() < 2 + headerLen) return {};
    const QByteArray headerBytes = raw.mid(2, headerLen);
    if (outPath) {
        const QStringList lines = QString::fromUtf8(headerBytes).split(QStringLiteral("\r\n"));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("Path:"))) {
                *outPath = line.mid(5).trimmed();
                break;
            }
        }
    }
    return raw.mid(2 + headerLen);
}

bool manualReadUntil(QSslSocket& socket, QByteArray* buffer, qsizetype bytesNeeded, QElapsedTimer& timer, int timeoutMs) {
    while (buffer->size() < bytesNeeded) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) return false;
        if (!socket.waitForReadyRead(qMin(remaining, 1000))) {
            if (socket.error() != QAbstractSocket::UnknownSocketError) return false;
        }
        buffer->append(socket.readAll());
    }
    return true;
}

bool manualWriteFrame(QSslSocket& socket, quint8 opcode, const QByteArray& payload) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));
    const quint64 len = static_cast<quint64>(payload.size());
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | static_cast<quint8>(len)));
    } else if (len <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((len >> shift) & 0xFF));
        }
    }

    QByteArray mask(4, Qt::Uninitialized);
    const quint32 m = QRandomGenerator::global()->generate();
    mask[0] = static_cast<char>((m >> 24) & 0xFF);
    mask[1] = static_cast<char>((m >> 16) & 0xFF);
    mask[2] = static_cast<char>((m >> 8) & 0xFF);
    mask[3] = static_cast<char>(m & 0xFF);
    frame.append(mask);
    for (int i = 0; i < payload.size(); ++i) {
        frame.append(static_cast<char>(static_cast<quint8>(payload[i]) ^
                                       static_cast<quint8>(mask[i % 4])));
    }

    return socket.write(frame) == frame.size() && socket.flush();
}

ManualFrame manualReadFrame(QSslSocket& socket, QByteArray* buffer, QElapsedTimer& timer, int timeoutMs) {
    ManualFrame out;
    if (!manualReadUntil(socket, buffer, 2, timer, timeoutMs)) {
        out.error = QStringLiteral("wss_read_timeout");
        return out;
    }
    const quint8 b0 = static_cast<quint8>((*buffer)[0]);
    const quint8 b1 = static_cast<quint8>((*buffer)[1]);
    const bool masked = (b1 & 0x80) != 0;
    quint64 len = b1 & 0x7F;
    qsizetype pos = 2;
    if (len == 126) {
        if (!manualReadUntil(socket, buffer, pos + 2, timer, timeoutMs)) {
            out.error = QStringLiteral("wss_read_timeout");
            return out;
        }
        len = (static_cast<quint8>((*buffer)[pos]) << 8) |
              static_cast<quint8>((*buffer)[pos + 1]);
        pos += 2;
    } else if (len == 127) {
        if (!manualReadUntil(socket, buffer, pos + 8, timer, timeoutMs)) {
            out.error = QStringLiteral("wss_read_timeout");
            return out;
        }
        len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | static_cast<quint8>((*buffer)[pos + i]);
        }
        pos += 8;
    }
    QByteArray mask;
    if (masked) {
        if (!manualReadUntil(socket, buffer, pos + 4, timer, timeoutMs)) {
            out.error = QStringLiteral("wss_read_timeout");
            return out;
        }
        mask = buffer->mid(pos, 4);
        pos += 4;
    }
    if (len > static_cast<quint64>(64 * 1024 * 1024)) {
        out.error = QStringLiteral("wss_frame_too_large");
        return out;
    }
    if (!manualReadUntil(socket, buffer, pos + static_cast<qsizetype>(len), timer, timeoutMs)) {
        out.error = QStringLiteral("wss_read_timeout");
        return out;
    }
    QByteArray payload = buffer->mid(pos, static_cast<qsizetype>(len));
    buffer->remove(0, pos + static_cast<qsizetype>(len));
    if (masked) {
        for (int i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^
                                           static_cast<quint8>(mask[i % 4]));
        }
    }
    out.ok = true;
    out.opcode = b0 & 0x0F;
    out.payload = std::move(payload);
    return out;
}

void manualAppendMetadataBoundaries(const QByteArray& payload, QJsonArray* boundaries) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonArray metadata = doc.object().value(QStringLiteral("Metadata")).toArray();
    for (const QJsonValue& v : metadata) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("Type")).toString() != QStringLiteral("WordBoundary")) continue;
        const QJsonObject data = m.value(QStringLiteral("Data")).toObject();
        const qint64 offsetTicks = static_cast<qint64>(data.value(QStringLiteral("Offset")).toDouble(0.0));
        const QJsonObject textObj = data.value(QStringLiteral("text")).toObject();
        const QString text = textObj.value(QStringLiteral("Text")).toString();
        if (text.isEmpty()) continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("text"), text);
        entry.insert(QStringLiteral("offsetMs"), static_cast<qint64>(offsetTicks / 10000));
        boundaries->append(entry);
    }
}

ManualRoundTripOutcome manualRoundTrip(const QString& text, const QString& voice,
                                       double rate, double pitch, int timeoutMs) {
    ManualRoundTripOutcome out;
    const QString secMsGec = EdgeTtsClient::generateSecMSGEC(QDateTime::currentSecsSinceEpoch());
    const QString urlText = QStringLiteral("%1?TrustedClientToken=%2"
                                           "&ConnectionId=%3"
                                           "&Sec-MS-GEC=%4"
                                           "&Sec-MS-GEC-Version=%5")
                                .arg(QString::fromLatin1(kWssBase),
                                     QString::fromLatin1(kTrustedClientToken),
                                     manualConnectionId(), secMsGec,
                                     QString::fromLatin1(kEdgeVersion));
    const QUrl url(urlText);
    QSslSocket socket;
    socket.connectToHostEncrypted(url.host(), 443);
    if (!socket.waitForEncrypted(5000)) {
        qDebug() << "[EdgeTtsClient] manual WSS TLS failed:" << socket.errorString();
        out.errorReason = QStringLiteral("wss_handshake_fail");
        return out;
    }

    QByteArray keyBytes(16, Qt::Uninitialized);
    for (int i = 0; i < keyBytes.size(); ++i) {
        keyBytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    const QByteArray secKey = keyBytes.toBase64();
    const QByteArray target = (url.path() + QStringLiteral("?") + url.query(QUrl::FullyEncoded)).toUtf8();
    QByteArray req;
    req += "GET " + target + " HTTP/1.1\r\n";
    req += "Host: " + url.host().toLatin1() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "Sec-WebSocket-Key: " + secKey + "\r\n";
    req += "User-Agent: " + QByteArray(kUserAgent) + "\r\n";
    req += "Origin: " + QByteArray(kOrigin) + "\r\n";
    req += "Cache-Control: no-cache\r\n";
    req += "Pragma: no-cache\r\n";
    req += "Cookie: MUID=" + manualMuidCookie().toLatin1() + "\r\n";
    req += "\r\n";
    socket.write(req);
    if (!socket.waitForBytesWritten(5000)) {
        out.errorReason = QStringLiteral("wss_handshake_fail");
        return out;
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (!buffer.contains("\r\n\r\n")) {
        const int remaining = 5000 - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || !socket.waitForReadyRead(qMin(remaining, 1000))) {
            qDebug() << "[EdgeTtsClient] manual WSS HTTP upgrade timeout/error:" << socket.errorString();
            out.errorReason = QStringLiteral("wss_handshake_fail");
            return out;
        }
        buffer.append(socket.readAll());
    }
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    const QByteArray header = buffer.left(headerEnd);
    buffer.remove(0, headerEnd + 4);
    if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
        qDebug() << "[EdgeTtsClient] manual WSS upgrade rejected:" << header.left(160);
        out.errorReason = QStringLiteral("wss_handshake_fail");
        return out;
    }

    const QString requestId = manualRequestId();
    if (!manualWriteFrame(socket, 0x1, manualSpeechConfigMessage(QString::fromLatin1(kDefaultOutputFormat)).toUtf8()) ||
        !manualWriteFrame(socket, 0x1, manualSsmlMessage(requestId, text, voice, rate, pitch).toUtf8())) {
        out.errorReason = QStringLiteral("wss_send_fail");
        return out;
    }

    QElapsedTimer roundTimer;
    roundTimer.start();
    QByteArray currentMessage;
    quint8 currentOpcode = 0;
    while (roundTimer.elapsed() < timeoutMs) {
        ManualFrame frame = manualReadFrame(socket, &buffer, roundTimer, timeoutMs);
        if (!frame.ok) {
            if (!out.gotAudio) out.errorReason = QStringLiteral("network_blocked");
            break;
        }
        if (frame.opcode == 0x8) break; // close
        if (frame.opcode == 0x9) { // ping
            manualWriteFrame(socket, 0xA, frame.payload);
            continue;
        }
        if (frame.opcode == 0xA) continue; // pong
        if (frame.opcode == 0x1 || frame.opcode == 0x2) {
            currentOpcode = frame.opcode;
            currentMessage = frame.payload;
        } else if (frame.opcode == 0x0 && currentOpcode) {
            currentMessage.append(frame.payload);
        } else {
            continue;
        }

        if (currentOpcode == 0x1) {
            const QString msg = QString::fromUtf8(currentMessage);
            if (msg.contains(QStringLiteral("Path:turn.end")) ||
                msg.contains(QStringLiteral("Path: turn.end"))) {
                out.gotTurnEnd = true;
                break;
            }
        } else if (currentOpcode == 0x2) {
            QString path;
            const QByteArray payload = manualParseBinaryFrame(currentMessage, &path);
            if (path == QStringLiteral("audio") && !payload.isEmpty()) {
                out.gotAudio = true;
                out.mp3.append(payload);
            } else if (path == QStringLiteral("audio.metadata") && !payload.isEmpty()) {
                manualAppendMetadataBoundaries(payload, &out.boundaries);
            }
        }
        currentOpcode = 0;
        currentMessage.clear();
    }
    socket.disconnectFromHost();
    if (!out.gotAudio && out.errorReason.isEmpty()) out.errorReason = QStringLiteral("network_blocked");
    return out;
}

}  // namespace

EdgeTtsClient::ProbeResult EdgeTtsClient::probe(const QString& voice) {
    if (voice.isEmpty()) {
        return {false, QStringLiteral("voice_empty")};
    }
    auto rt = manualRoundTrip(QString::fromLatin1(kProbeText), voice, 1.0, 1.0,
                              kProbeTimeoutMs);
    if (!rt.errorReason.isEmpty()) return {false, rt.errorReason};
    if (!rt.gotAudio) return {false, QStringLiteral("no_audio_received")};
    return {true, {}};
}

EdgeTtsClient::SynthResult EdgeTtsClient::synth(const QString& text,
                                                const QString& voice,
                                                double rate, double pitch) {
    if (text.trimmed().isEmpty()) {
        return {false, {}, {}, QStringLiteral("text_empty")};
    }
    if (voice.isEmpty()) {
        return {false, {}, {}, QStringLiteral("voice_empty")};
    }

    const QString outputFormat = QString::fromLatin1(kDefaultOutputFormat);
    const QByteArray cacheKey = makeCacheKey(text, voice, rate, pitch, outputFormat);
    CacheEntry hit;
    if (cacheLookup(cacheKey, &hit)) {
        return {true, hit.mp3, hit.boundaries, {}};
    }

    constexpr int kSynthTimeoutMs = 12000;
    auto rt = manualRoundTrip(text, voice, rate, pitch, kSynthTimeoutMs);
    if (!rt.errorReason.isEmpty()) return {false, {}, {}, rt.errorReason};
    if (!rt.gotAudio) return {false, {}, {}, QStringLiteral("no_audio_received")};
    if (!rt.gotTurnEnd) return {false, {}, {}, QStringLiteral("incomplete_synth")};
    cacheInsert(cacheKey, CacheEntry{rt.mp3, rt.boundaries});
    return {true, rt.mp3, rt.boundaries, {}};
}

#else  // HAS_WEBSOCKETS

EdgeTtsClient::ProbeResult EdgeTtsClient::probe(const QString& voice) {
    if (voice.isEmpty()) {
        return {false, QStringLiteral("voice_empty")};
    }
    auto rt = runRoundTrip(QString::fromLatin1(kProbeText), voice, 1.0, 1.0,
                           kProbeTimeoutMs);
    if (!rt.errorReason.isEmpty()) return {false, rt.errorReason};
    if (!rt.gotAudio) return {false, QStringLiteral("no_audio_received")};
    if (!rt.gotTurnEnd) {
        // Soft success for probe: protocol delivered audio, server closure was
        // just slow. Phase 2 synth path is stricter.
        qDebug() << "[EdgeTtsClient] probe got audio but no turn.end before timeout";
    }
    return {true, {}};
}

EdgeTtsClient::SynthResult EdgeTtsClient::synth(const QString& text,
                                                const QString& voice,
                                                double rate, double pitch) {
    // EDGE_TTS_FIX Phase 2.1 + Phase 3.1 — synth round-trip with LRU cache.
    if (text.trimmed().isEmpty()) {
        return {false, {}, {}, QStringLiteral("text_empty")};
    }
    if (voice.isEmpty()) {
        return {false, {}, {}, QStringLiteral("voice_empty")};
    }

    // EDGE_TTS_FIX Phase 3.1 — cache lookup BEFORE any WSS work. On hit we
    // skip the round-trip entirely (~10ms return vs ~1-2s synth on healthy
    // network). Voice + rate + pitch are all part of the key so a voice or
    // rate change naturally produces a cache miss.
    const QString outputFormat = QString::fromLatin1(kDefaultOutputFormat);
    const QByteArray cacheKey = makeCacheKey(text, voice, rate, pitch, outputFormat);
    CacheEntry hit;
    if (cacheLookup(cacheKey, &hit)) {
        return {true, hit.mp3, hit.boundaries, {}};
    }

    // Phase 2 timeout — sentence-by-sentence synth on a healthy network is
    // typically well under 5s. 12s cap deliberately UNDER the JS-side
    // _synthWithTimeout 15000ms (`tts_engine_edge.js:1075`) so we cleanly
    // surface our own structured failure rather than racing the JS abort.
    constexpr int kSynthTimeoutMs = 12000;
    auto rt = runRoundTrip(text, voice, rate, pitch, kSynthTimeoutMs);

    if (!rt.errorReason.isEmpty()) {
        return {false, {}, {}, rt.errorReason};
    }
    if (!rt.gotAudio) {
        return {false, {}, {}, QStringLiteral("no_audio_received")};
    }
    if (!rt.gotTurnEnd) {
        // Synth is stricter than probe: missing turn.end means we may have a
        // truncated MP3, which would produce a partial/glitched playback. Fail
        // and let JS retry rather than ship corrupt audio.
        return {false, {}, {}, QStringLiteral("incomplete_synth")};
    }

    // WordBoundary entries collected from audio.metadata frames during the
    // round-trip; JS reader consumes these to drive read-along highlight.
    // Stored in the LRU alongside the MP3 so cache hits also paint highlight.
    cacheInsert(cacheKey, CacheEntry{rt.mp3, rt.boundaries});
    return {true, rt.mp3, rt.boundaries, {}};
}

EdgeTtsClient::RoundTripOutcome
EdgeTtsClient::runRoundTrip(const QString& text, const QString& voice,
                            double rate, double pitch, int timeoutMs) {
    RoundTripOutcome out;

    if (!ensureSocketOpen()) {
        out.errorReason = QStringLiteral("wss_handshake_fail");
        return out;
    }

    const QString requestId = generateRequestId();
    const QString configMsg = buildSpeechConfigMessage(
        requestId, QString::fromLatin1(kDefaultOutputFormat));
    const QString ssmlMsg = buildSsmlMessage(requestId, text, voice, rate, pitch);

    if (m_socket->sendTextMessage(configMsg) <= 0) {
        out.errorReason = QStringLiteral("wss_send_fail");
        return out;
    }
    if (m_socket->sendTextMessage(ssmlMsg) <= 0) {
        out.errorReason = QStringLiteral("wss_send_fail");
        return out;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(timeoutMs);
    bool errored = false;

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        if (!errored && out.errorReason.isEmpty()) {
            // No error fired but timer ran out — classify based on what we
            // saw. If zero audio + zero text frames, the network is blocked /
            // server didn't respond. If audio came but turn.end didn't, the
            // server stalled mid-stream.
            if (!out.gotAudio) {
                out.errorReason = QStringLiteral("network_blocked");
            }
            // else: leave errorReason empty; caller (synth) treats missing
            // turn.end as incomplete_synth, probe treats it as soft-success.
        }
        loop.quit();
    });

    auto textHandler = QObject::connect(m_socket, &QWebSocket::textMessageReceived,
                                        &loop, [&](const QString& msg) {
        if (msg.contains(QStringLiteral("Path:turn.end")) ||
            msg.contains(QStringLiteral("Path: turn.end"))) {
            out.gotTurnEnd = true;
            loop.quit();
        }
    });

    auto binaryHandler = QObject::connect(m_socket, &QWebSocket::binaryMessageReceived,
                                          &loop, [&](const QByteArray& raw) {
        QString path;
        QByteArray payload = parseBinaryFrame(raw, &path);
        if (path == QStringLiteral("audio") && !payload.isEmpty()) {
            out.gotAudio = true;
            out.mp3.append(payload);
        } else if (path == QStringLiteral("audio.metadata") && !payload.isEmpty()) {
            // WordBoundary frames carry JSON:
            //   {"Metadata":[{"Type":"WordBoundary",
            //                 "Data":{"Offset":<100ns ticks>,
            //                         "Duration":<100ns ticks>,
            //                         "text":{"Text":"hello","Length":5,
            //                                 "BoundaryType":"WordBoundary"}}}]}
            // We flatten into the shape tts_engine_edge.js:506/531 expects:
            //   [{"text":"hello","offsetMs":500}, ...]
            // Offset conversion: 100ns ticks / 10000 = ms (10M ticks per sec).
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
            const QJsonArray metadata = doc.object().value(QStringLiteral("Metadata")).toArray();
            for (const QJsonValue& v : metadata) {
                const QJsonObject m = v.toObject();
                if (m.value(QStringLiteral("Type")).toString() != QStringLiteral("WordBoundary")) {
                    continue;
                }
                const QJsonObject data = m.value(QStringLiteral("Data")).toObject();
                const qint64 offsetTicks = static_cast<qint64>(
                    data.value(QStringLiteral("Offset")).toDouble(0.0));
                const QJsonObject textObj = data.value(QStringLiteral("text")).toObject();
                const QString text = textObj.value(QStringLiteral("Text")).toString();
                if (text.isEmpty()) continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("text"), text);
                entry.insert(QStringLiteral("offsetMs"),
                             static_cast<qint64>(offsetTicks / 10000));
                out.boundaries.append(entry);
            }
        }
    });

    auto errorHandler = QObject::connect(m_socket,
        QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
        &loop, [&](QAbstractSocket::SocketError err) {
            errored = true;
            // Edge sometimes closes the connection on voice-not-found instead
            // of returning an error frame. We can't reliably distinguish that
            // from a real network error here, so report wss_socket_error and
            // let the JS engine's failure-state HUD show it.
            qDebug() << "[EdgeTtsClient] WSS error during synth:" << err
                     << m_socket->errorString();
            if (out.errorReason.isEmpty()) {
                out.errorReason = QStringLiteral("wss_socket_error");
            }
            loop.quit();
        });

    timeout.start();
    loop.exec();
    timeout.stop();

    QObject::disconnect(textHandler);
    QObject::disconnect(binaryHandler);
    QObject::disconnect(errorHandler);

    return out;
}

// ── WSS connection lifecycle ─────────────────────────────────────────────────

bool EdgeTtsClient::ensureSocketOpen() {
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        return true;
    }
    closeSocket();
    m_socket = new QWebSocket(QString::fromLatin1(kOrigin),
                              QWebSocketProtocol::VersionLatest, this);

    // Build the connect URL with required query params.
    const QString secMsGec = generateSecMSGEC(QDateTime::currentSecsSinceEpoch());
    const QString connectionId = generateConnectionId();
    const QString url = QStringLiteral("%1?TrustedClientToken=%2"
                                       "&ConnectionId=%3"
                                       "&Sec-MS-GEC=%4"
                                       "&Sec-MS-GEC-Version=%5")
                            .arg(QString::fromLatin1(kWssBase),
                                 QString::fromLatin1(kTrustedClientToken),
                                 connectionId, secMsGec,
                                 QString::fromLatin1(kEdgeVersion));

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Origin", kOrigin);
    req.setRawHeader("Cache-Control", "no-cache");
    req.setRawHeader("Pragma", "no-cache");
    // Optional MUID cookie — Edge sends one; absence does not block the
    // handshake but adding it matches real-Edge behavior.
    const QString muid = generateMuidCookie();
    req.setRawHeader("Cookie", QStringLiteral("MUID=%1").arg(muid).toLatin1());

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    bool connected = false;

    QObject::connect(m_socket, &QWebSocket::connected, &loop, [&]() {
        connected = true;
        loop.quit();
    });
    QObject::connect(m_socket,
        QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
        &loop, [&](QAbstractSocket::SocketError err) {
            qDebug() << "[EdgeTtsClient] WSS connect error:" << err
                     << m_socket->errorString();
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    m_socket->open(req);
    timeout.start();
    loop.exec();
    timeout.stop();

    if (!connected) {
        closeSocket();
        return false;
    }
    return true;
}

void EdgeTtsClient::closeSocket() {
    if (!m_socket) return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->close();
    }
    m_socket->deleteLater();
    m_socket = nullptr;
}

// ── Edge protocol message builders ───────────────────────────────────────────

QString EdgeTtsClient::buildSpeechConfigMessage(const QString& requestId,
                                                const QString& outputFormat) {
    (void)requestId;  // speech.config doesn't carry X-RequestId; signature kept
                      // symmetric with buildSsmlMessage for Phase 2 use.
    // speech.config sets the audio format + boundary metadata flags.
    // Word-boundary enabled so the server emits audio.metadata frames carrying
    // per-word {offsetMs, text} entries — consumed by the JS reader to drive
    // read-along highlight (tts_engine_edge.js:495 fireBoundaries + :422
    // _bdPoll). Sentence-boundary stays off; JS doesn't use it (sentence
    // boundaries are already derived from Foliate marks at a higher layer).
    // String-valued booleans required — the Edge endpoint rejects raw JSON
    // true/false for this particular pair of flags (rany2/edge-tts quirk).
    QJsonObject metadataOpts{
        {"sentenceBoundaryEnabled", QStringLiteral("false")},
        {"wordBoundaryEnabled", QStringLiteral("true")},
    };
    QJsonObject audio{
        {"metadataoptions", metadataOpts},
        {"outputFormat", outputFormat},
    };
    QJsonObject synthesis{{"audio", audio}};
    QJsonObject context{{"synthesis", synthesis}};
    QJsonObject root{{"context", context}};
    const QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);

    return QStringLiteral("X-Timestamp:%1\r\n"
                          "Content-Type:application/json; charset=utf-8\r\n"
                          "Path:speech.config\r\n\r\n%2")
        .arg(edgeIso8601Timestamp(), QString::fromUtf8(body));
}

QString EdgeTtsClient::buildSsmlMessage(const QString& requestId, const QString& text,
                                        const QString& voice, double rate, double pitch) {
    // Edge consumer endpoint accepts only a constrained SSML subset:
    // single <voice> with single <prosody>. No bookmarks / phonemes / lexicons.
    // Locale on the <speak> tag is derived from the voice name (en-US-AndrewNeural -> en-US).
    QString locale = QStringLiteral("en-US");
    const int dash2 = voice.indexOf('-', voice.indexOf('-') + 1);
    if (dash2 > 0) {
        locale = voice.left(dash2);
    }

    const QString ssml =
        QStringLiteral("<speak version='1.0' "
                       "xmlns='http://www.w3.org/2001/10/synthesis' "
                       "xml:lang='%1'>"
                       "<voice name='%2'>"
                       "<prosody pitch='%3' rate='%4' volume='+0%'>%5</prosody>"
                       "</voice></speak>")
            .arg(locale, voice, pitchToEdgeHz(pitch), rateToEdgePercent(rate),
                 sanitizeForXml(text));

    return QStringLiteral("X-RequestId:%1\r\n"
                          "Content-Type:application/ssml+xml\r\n"
                          "X-Timestamp:%2\r\n"
                          "Path:ssml\r\n\r\n%3")
        .arg(requestId, edgeIso8601Timestamp(), ssml);
}

// ── Binary frame parser ──────────────────────────────────────────────────────

QByteArray EdgeTtsClient::parseBinaryFrame(const QByteArray& raw, QString* outPath) {
    if (outPath) outPath->clear();
    if (raw.size() < 2) return {};

    // Big-endian uint16 header length prefix.
    const quint16 headerLen = (static_cast<quint8>(raw[0]) << 8) |
                              static_cast<quint8>(raw[1]);
    if (raw.size() < 2 + headerLen) return {};

    const QByteArray headerBytes = raw.mid(2, headerLen);
    const QByteArray audioBytes = raw.mid(2 + headerLen);

    // Parse Path: header from headerBytes.
    if (outPath) {
        const QString headerStr = QString::fromUtf8(headerBytes);
        const QStringList lines = headerStr.split(QStringLiteral("\r\n"));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("Path:"))) {
                *outPath = line.mid(5).trimmed();
                break;
            }
        }
    }
    return audioBytes;
}

// ── ID + helper utilities ────────────────────────────────────────────────────

QString EdgeTtsClient::generateConnectionId() {
    // Edge expects 32-char lowercase hex with no dashes.
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').toLower();
}

QString EdgeTtsClient::generateRequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').toLower();
}

QString EdgeTtsClient::generateMuidCookie() {
    // Real Edge MUID is 32 hex chars; random suffices for our purposes.
    QString out;
    out.reserve(32);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 32; ++i) {
        const int v = rng->bounded(16);
        out.append(QChar::fromLatin1(v < 10 ? '0' + v : 'A' + (v - 10)));
    }
    return out;
}

QString EdgeTtsClient::edgeIso8601Timestamp() {
    // Edge expects "Sun Jan 30 2022 22:13:25 GMT+0000 (Coordinated Universal Time)"
    // style or RFC3339. Microsoft's server has historically been forgiving;
    // RFC3339 with millisecond precision works.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return now.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
}

QString EdgeTtsClient::sanitizeForXml(const QString& raw) {
    // Strip control chars + escape XML entities. SSML body must not contain
    // unescaped &, <, > characters.
    QString out;
    out.reserve(raw.size());
    for (QChar c : raw) {
        if (c == QChar('&'))      out.append(QStringLiteral("&amp;"));
        else if (c == QChar('<')) out.append(QStringLiteral("&lt;"));
        else if (c == QChar('>')) out.append(QStringLiteral("&gt;"));
        else if (c == QChar('"')) out.append(QStringLiteral("&quot;"));
        else if (c == QChar('\'')) out.append(QStringLiteral("&apos;"));
        else if (c.isPrint() || c.isSpace()) out.append(c);
        // else: drop control chars silently
    }
    return out;
}

QString EdgeTtsClient::rateToEdgePercent(double rate) {
    // 1.0 -> "+0%", 1.5 -> "+50%", 0.5 -> "-50%".
    const int pct = static_cast<int>((rate - 1.0) * 100.0 + (rate >= 1.0 ? 0.5 : -0.5));
    return pct >= 0 ? QStringLiteral("+%1%").arg(pct)
                    : QStringLiteral("%1%").arg(pct);
}

QString EdgeTtsClient::pitchToEdgeHz(double pitch) {
    // 1.0 -> "+0Hz". Future use; baseline pitch passed through unchanged.
    const int hz = static_cast<int>((pitch - 1.0) * 50.0);
    return hz >= 0 ? QStringLiteral("+%1Hz").arg(hz)
                   : QStringLiteral("%1Hz").arg(hz);
}

#endif  // HAS_WEBSOCKETS

// ── LRU cache (Phase 3.1) ────────────────────────────────────────────────────
// Defined outside HAS_WEBSOCKETS guard — the cache itself is just bytes +
// metadata, no WSS dependency. (Cache lookups still happen on WebSockets
// builds only since synth() is only callable there.)

QByteArray EdgeTtsClient::makeCacheKey(const QString& text, const QString& voice,
                                       double rate, double pitch,
                                       const QString& outputFormat) const {
    // Fixed-precision rate/pitch (3 decimals) so 1.0 vs 1.000 hash identically.
    // Pipe separators ensure no field-collision under unusual inputs (text
    // containing a literal voice name, etc.).
    const QString concat = text + QStringLiteral("|") + voice + QStringLiteral("|") +
                           QString::number(rate, 'f', 3) + QStringLiteral("|") +
                           QString::number(pitch, 'f', 3) + QStringLiteral("|") +
                           outputFormat;
    return QCryptographicHash::hash(concat.toUtf8(),
                                    QCryptographicHash::Sha1).toHex();
}

bool EdgeTtsClient::cacheLookup(const QByteArray& key, CacheEntry* out) {
    auto it = m_cacheIndex.find(key);
    if (it == m_cacheIndex.end()) return false;
    // Strict LRU touch-on-read: splice the matching list node to front.
    // QHash iterator's value() is the std::list iterator pointing to the node.
    m_cacheList.splice(m_cacheList.begin(), m_cacheList, it.value());
    if (out) *out = it.value()->second;
    return true;
}

void EdgeTtsClient::cacheInsert(const QByteArray& key, CacheEntry entry) {
    // If key already exists (concurrent synth raced past lookup-miss), update
    // in place rather than duplicating.
    auto existing = m_cacheIndex.find(key);
    if (existing != m_cacheIndex.end()) {
        existing.value()->second = std::move(entry);
        m_cacheList.splice(m_cacheList.begin(), m_cacheList, existing.value());
        return;
    }
    // Capacity check — drop the oldest (back of list) before insert.
    while (m_cacheList.size() >= static_cast<size_t>(kCacheCapacity)) {
        const QByteArray& evictKey = m_cacheList.back().first;
        m_cacheIndex.remove(evictKey);
        m_cacheList.pop_back();
    }
    m_cacheList.emplace_front(key, std::move(entry));
    m_cacheIndex.insert(key, m_cacheList.begin());
}
