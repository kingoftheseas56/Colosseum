#include "NuvioNetwork.h"
#include "NuvioTmdbCredentials.h"
#include "NuvioWorkerProtocol.h"
#include <QCoreApplication>
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QLockFile>
#include <QSaveFile>
#include <QHash>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QWebSocket>
#include <QThread>
#include <QUrl>
#include <QtGlobal>
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C" {
#include "quickjs.h"
}
struct Host {
    NuvioTmdb::Credential tmdb;
    bool credentialChecked = false;
    QElapsedTimer clock;
    QHash<QString, QNetworkAccessManager *> origins;
    QHash<QString, qint64> blockedUntil;
    QString retryDirectory;
    QHash<int, QNetworkReply *> requests;
    QHash<int, QTimer *> timers;
    int requestId = 0;
    int timerId = 0;
    QHash<int, QWebSocket *> sockets;
    int socketId = 0;
    bool done = false;
    QString error;
    QJsonArray results;
    JSRuntime *runtime = nullptr;
    JSContext *context = nullptr;
    QNetworkAccessManager network;
};
static Host *state(JSContext *ctx) { return static_cast<Host*>(JS_GetContextOpaque(ctx)); }
static QString str(JSContext *ctx, JSValueConst val) {
    const char *p = JS_ToCString(ctx, val);
    if (!p) return {};
    const QString s = QString::fromUtf8(p);
    JS_FreeCString(ctx, p);
    return s;
}
static QByteArray encoded(JSContext *ctx, JSValueConst val) {
    JSValue asJson = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    QByteArray bytes = str(ctx, asJson).toUtf8();
    JS_FreeValue(ctx, asJson);
    return bytes;
}
static JSValue reject(JSContext *ctx, const QString &reason) {
    return JS_ThrowTypeError(ctx, "%s", reason.toUtf8().constData());
}
static constexpr qsizetype bodyLimit = 2 * 1024 * 1024;
static constexpr int requestLimit = 80;
static constexpr int inFlightLimit = 8;
static constexpr int timerLimit = 128;
static constexpr int totalTimerLimit = 4096;

static QString failureClass(const QString &message)
{
    if (message.contains("interrupted")) return "deadline";
    if (message.contains("out of memory")) return "memory_limit";
    if (message.contains("stack overflow")) return "stack_limit";
    if (message.contains("require blocked")) return "host_module_unsupported";
    if (message.contains("ReferenceError") && message.contains("not defined")) return "host_or_script_global_missing";
    if (message.contains("not a function") || message.contains("not a constructor")) return "host_or_script_api_missing";
    if (message.contains("getStreams missing")) return "entry_point_missing";
    if (message.contains("SyntaxError")) return "script_syntax";
    return "plugin_exception";
}

static void captureException(Host *s)
{
    JSValue error = JS_GetException(s->context);
    s->error = failureClass(str(s->context, error));
    JS_FreeValue(s->context, error);
}

static JSValue jsConsole(JSContext *, JSValueConst, int, JSValueConst *)
{
    return JS_UNDEFINED;
}

static JSValue jsDone(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    auto *s = state(ctx);
    if (!s->done && argc) {
        const auto json = encoded(ctx, argv[0]);
        if (json.size() > NuvioWorkerProtocol::maxOutputBytes - 256) {
            s->error = QStringLiteral("result_limit");
            s->done = true;
            return JS_UNDEFINED;
        }
        const auto doc = QJsonDocument::fromJson(json);
        if (doc.isArray()) s->results = doc.array();
        else s->error = "invalid_result";
    }
    s->done = true;
    return JS_UNDEFINED;
}

static JSValue jsError(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    auto *s = state(ctx);
    s->error = argc ? failureClass(str(ctx, argv[0])) : QString("plugin_exception");
    s->done = true;
    return JS_UNDEFINED;
}

static int jsInterrupt(JSContext *, void *opaque)
{
    return static_cast<Host *>(opaque)->clock.elapsed() >= NuvioWorkerProtocol::pluginDeadlineMs;
}

static void callback(Host *s, const char *name, int count, JSValue *args)
{
    JSValue global = JS_GetGlobalObject(s->context);
    JSValue fn = JS_GetPropertyStr(s->context, global, name);
    JSValue value = JS_Call(s->context, fn, JS_UNDEFINED, count, args);
    if (JS_IsException(value)) captureException(s);
    JS_FreeValue(s->context, value);
    JS_FreeValue(s->context, fn);
    JS_FreeValue(s->context, global);
    for (int i = 0; i < count; ++i) JS_FreeValue(s->context, args[i]);
}

static JSValue jsStartTimer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    auto *s = state(ctx);
    if (s->timers.size() >= timerLimit || s->timerId >= totalTimerLimit)
        return reject(ctx, "timer limit");
    int32_t delay = 1;
    if (argc && JS_ToInt32(ctx, &delay, argv[0]) < 0) return JS_EXCEPTION;
    const bool repeat = argc > 1 && JS_ToBool(ctx, argv[1]);
    const int id = ++s->timerId;
    auto *timer = new QTimer(&s->network);
    timer->setSingleShot(!repeat);
    s->timers.insert(id, timer);
    QObject::connect(timer, &QTimer::timeout, &s->network, [s, timer, id, repeat]() {
        if (!repeat) {
            s->timers.remove(id);
            timer->deleteLater();
        }
        if (s->done || !s->error.isEmpty()) return;
        JSValue arg = JS_NewInt32(s->context, id);
        callback(s, "__fireTimer", 1, &arg);
    });
    timer->start(std::max(1, delay));
    return JS_NewInt32(ctx, id);
}

static JSValue jsClearTimer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t id = 0;
    if (argc && JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (auto *timer = state(ctx)->timers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
    return JS_UNDEFINED;
}

static JSValue jsDecodeUtf8(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (!argc) return JS_NewString(ctx, "");
    size_t offset = 0, length = 0, size = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, nullptr);
    if (JS_IsException(buffer)) return buffer;
    const auto *data = JS_GetArrayBuffer(ctx, &size, buffer);
    const QByteArray utf8 = data
        ? QString::fromUtf8(reinterpret_cast<const char *>(data + offset), length).toUtf8() : QByteArray();
    JS_FreeValue(ctx, buffer);
    return JS_NewStringLen(ctx, utf8.constData(), utf8.size());
}

static JSValue jsRandom(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t size = 0;
    if (argc && JS_ToInt32(ctx, &size, argv[0]) < 0) return JS_EXCEPTION;
    if (size < 0 || size > 65536) return reject(ctx, "invalid random size");
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i = 0; i < size;) {
        const quint32 word = QRandomGenerator::system()->generate();
        const int n = std::min<int>(sizeof(word), size - i);
        std::memcpy(bytes.data() + i, &word, n);
        i += n;
    }
    return JS_NewString(ctx, bytes.toBase64().constData());
}

struct Request {
    QUrl url;
    QByteArray method;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers;
    QString redirect;
    int hops = 0;
};

static void deliver(Host *s, int id, const QString &error, const QJsonObject &packet = {})
{
    if (!s->requests.remove(id) || s->done || !s->error.isEmpty()) return;
    const auto json = QJsonDocument(packet).toJson(QJsonDocument::Compact);
    JSValue args[] = {
        JS_NewInt32(s->context, id),
        JS_ParseJSON(s->context, json.constData(), json.size(), "response"),
        JS_NewString(s->context, error.toLatin1().constData())
    };
    callback(s, "__deliver", 3, args);
}

static int retrySeconds(const QByteArray &header)
{
    bool ok = false;
    const qint64 seconds = header.trimmed().toLongLong(&ok);
    if (ok) return static_cast<int>(std::clamp<qint64>(seconds, 0, 86400));
    const auto date = QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
    return date.isValid() ? static_cast<int>(std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().secsTo(date) + 1, 0, 86400)) : 0;
}

static QString cooldownPath(Host *s, const QString &origin)
{
    const auto hash = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha256).toHex();
    return s->retryDirectory + "/" + QString::fromLatin1(hash);
}

static qint64 readCooldown(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(32).toLongLong() : 0;
}

static void publishCooldown(Host *s, const QString &origin, int seconds)
{
    s->blockedUntil[origin] = s->clock.elapsed() + 1000LL * seconds;
    if (s->retryDirectory.isEmpty()) return;
    const auto path = cooldownPath(s, origin);
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(100)) return;
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + 1000LL * seconds;
    if (readCooldown(path) >= until) return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray::number(until));
        file.commit();
    }
}

static void startRequest(Host *s, int id, Request request);

static void sendRequest(Host *s, int id, Request request, const QHostAddress &ip)
{
    if (s->done || !s->requests.contains(id)) return;

    QNetworkRequest req(request.url);
    req.setDecompressedSafetyCheckThreshold(bodyLimit);
    req.setRawHeader("User-Agent", "Colosseum/Nuvio");
    for (auto it = request.headers.cbegin(); it != request.headers.cend(); ++it)
        req.setRawHeader(it.key(), it.value());
    auto authorization = NuvioTmdb::Authorization::Unchanged;
    if (request.method == "GET" && request.body.isEmpty()) {
        authorization = NuvioTmdb::authorize(req, s->tmdb);
        if (authorization == NuvioTmdb::Authorization::Missing && !s->credentialChecked) {
            s->credentialChecked = true;
            s->tmdb = NuvioTmdb::loadCredential();
            authorization = NuvioTmdb::authorize(req, s->tmdb);
        }
        if (authorization == NuvioTmdb::Authorization::Missing)
            return deliver(s, id, QStringLiteral("tmdb_credential_unavailable"));
    }
    const bool authenticated = authorization == NuvioTmdb::Authorization::Attached;
    NuvioNetwork::pin(req, ip);

    // Separate managers prevent connection and cookie reuse across origins sharing an IP.
    const QString origin = request.url.scheme() + "://" + request.url.authority();
    auto *&manager = s->origins[origin];
    if (!manager) {
        manager = new QNetworkAccessManager(&s->network);
        manager->setProxy(QNetworkProxy::NoProxy);
    }
    if (s->blockedUntil.value(origin) > s->clock.elapsed() ||
        (!s->retryDirectory.isEmpty() &&
         readCooldown(cooldownPath(s, origin)) > QDateTime::currentMSecsSinceEpoch()))
        return deliver(s, id, "upstream_retry_after");

    QNetworkReply *reply = manager->sendCustomRequest(req, request.method, request.body);
    s->requests[id] = reply;
    reply->setReadBufferSize(bodyLimit + 1);
    QTimer::singleShot(static_cast<int>(std::max<qint64>(1, NuvioWorkerProtocol::pluginDeadlineMs - s->clock.elapsed())),
                       reply, [reply]() { if (!reply->isFinished()) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, request]() {
        const auto mime = reply->rawHeader("Content-Type").toLower().split(';').first().trimmed();
        if (request.method != "HEAD" && ((mime.startsWith("video/") && mime != "video/vnd.mpegurl") ||
            mime.startsWith("audio/") || mime == "application/octet-stream")) {
            reply->setProperty("mediaBlocked", true);
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > bodyLimit) {
            reply->setProperty("bodyLimit", true);
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &s->network,
        [s, reply, id, request, authenticated, origin]() mutable {
        reply->deleteLater();
        if (!s->requests.contains(id) || s->done) return;
        if (s->clock.elapsed() >= NuvioWorkerProtocol::pluginDeadlineMs) return deliver(s, id, "deadline");
        if (reply->property("mediaBlocked").toBool()) return deliver(s, id, "media_get_blocked");
        if (reply->property("bodyLimit").toBool()) return deliver(s, id, "host_body_limit");

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || (status < 400 && reply->error() != QNetworkReply::NoError))
            return deliver(s, id, QString("upstream_transport_%1").arg(reply->error()));
        if (status == 429 || status == 503) {
            const int seconds = retrySeconds(reply->rawHeader("Retry-After"));
            if (seconds > 0) publishCooldown(s, origin, seconds);
        }

        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty() && authenticated)
            return deliver(s, id, "tmdb_authenticated_redirect_blocked");
        if (!redirect.isEmpty() && request.redirect == "error")
            return deliver(s, id, "fetch_redirect_rejected");
        if (!redirect.isEmpty() && request.redirect != "manual") {
            const auto next = request.url.resolved(redirect);
            if (++request.hops > 8 || (request.url.scheme() == "https" && next.scheme() != "https"))
                return deliver(s, id, "network_redirect_blocked");
            if (next.authority() != request.url.authority() || next.scheme() != request.url.scheme()) {
                request.headers.remove("authorization");
                request.headers.remove("cookie");
                request.headers.remove("proxy-authorization");
            }
            if ((status == 303 && request.method != "HEAD") ||
                ((status == 301 || status == 302) && request.method == "POST")) {
                request.method = "GET";
                request.body.clear();
                request.headers.remove("content-type");
            }
            request.url = next;
            startRequest(s, id, request);
            return;
        }

        QByteArray body = reply->read(bodyLimit);
        QJsonObject headers;
        if (authenticated) {
            body.replace(s->tmdb.secret, NuvioTmdb::placeholder);
            headers.insert("content-type", "application/json");
        } else {
            for (const auto &[name, value] : reply->rawHeaderPairs()) {
                const QString lower = QString::fromLatin1(name.toLower());
                const QString current = headers.value(lower).toString();
                headers.insert(lower, current.isEmpty() ? QString::fromLatin1(value)
                                                       : current + ", " + QString::fromLatin1(value));
            }
        }
        const QString statusText = authenticated ? QString() :
            reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        deliver(s, id, {}, {
            {"status", status}, {"statusText", statusText}, {"headers", headers},
            {"url", request.url.toString(QUrl::FullyEncoded)}, {"redirected", request.hops != 0},
            {"body", QString::fromLatin1(body.toBase64())}
        });
    });
}

static void startRequest(Host *s, int id, Request request)
{
    NuvioNetwork::resolve(request.url, &s->network, [s, id, request](
                             const QHostAddress &address, const QString &error) {
        if (s->done || !s->requests.contains(id)) return;
        if (error.isEmpty())
            sendRequest(s, id, request, address);
        else
            deliver(s, id, error);
    });
}

static JSValue jsRequest(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    auto *s = state(ctx);
    if (argc != 2 || s->requests.size() >= inFlightLimit || s->requestId >= requestLimit) {
        return reject(ctx, "request limit");
    }

    Request request;
    request.url = QUrl(str(ctx, argv[0]), QUrl::StrictMode);
    request.url.setFragment({});
    const auto options = QJsonDocument::fromJson(str(ctx, argv[1]).toUtf8()).object();
    request.method = options.value("method").toString().toLatin1().toUpper();
    request.body = options.value("body").toString().toUtf8();
    request.redirect = options.value("redirect").toString();
    const QSet<QByteArray> methods{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    if (!methods.contains(request.method) || request.body.size() > bodyLimit ||
        ((request.method == "GET" || request.method == "HEAD") && !request.body.isEmpty())) {
        return reject(ctx, "unsupported request");
    }
    const auto headers = options.value("headers").toObject();
    const QSet<QByteArray> blocked{"host", "connection", "proxy-authorization", "content-length",
                                  "transfer-encoding", "upgrade", "range", "accept-encoding"};
    const QRegularExpression headerName(R"(\A[!#$%&'*+.^_{}|~0-9a-z-]+\z)");
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto name = it.key().toLatin1().toLower();
        const auto value = it.value().toString().toUtf8();
        if (!blocked.contains(name) && headerName.match(QString::fromLatin1(name)).hasMatch() &&
            !value.contains('\r') && !value.contains('\n') && !value.contains('\0'))
            request.headers.insert(name, value);
    }

    const int id = ++s->requestId;
    s->requests.insert(id, nullptr);
    QTimer::singleShot(0, &s->network, [s, id, request]() {
        if (!s->done && s->requests.contains(id)) startRequest(s, id, request);
    });
    return JS_NewInt32(ctx, id);
}

static JSValue jsCancelRequest(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t id = 0;
    if (argc && JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    auto *s = state(ctx);
    if (auto *reply = s->requests.take(id)) reply->abort();
    return JS_UNDEFINED;
}

static void socketEvent(Host *s, int id, const char *event, const QString &payload = {})
{
    if (s->done || !s->error.isEmpty() || s->clock.elapsed() >= NuvioWorkerProtocol::pluginDeadlineMs) return;
    const auto utf8 = payload.toUtf8();
    JSValue args[] = {JS_NewInt32(s->context, id), JS_NewString(s->context, event),
                      JS_NewStringLen(s->context, utf8.constData(), utf8.size())};
    callback(s, "__socketEvent", 3, args);
}

static JSValue jsOpenSocket(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    auto *s = state(ctx);
    QUrl url(argc ? str(ctx, argv[0]) : QString(), QUrl::StrictMode);
    QUrl endpoint = url;
    endpoint.setScheme("https");
    if (url.scheme() != "wss" || !NuvioNetwork::publicUrl(endpoint, true) || url.hasFragment()) {
        return reject(ctx, "WebSocket URL blocked");
    }
    if (s->sockets.size() >= 2 || s->socketId >= 8) {
        return reject(ctx, "WebSocket limit");
    }
    const int id = ++s->socketId;
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, &s->network);
    s->sockets[id] = socket;
    socket->setProxy(QNetworkProxy::NoProxy);
    socket->setMaxAllowedIncomingMessageSize(256 * 1024);
    socket->setMaxAllowedIncomingFrameSize(256 * 1024);
    socket->setReadBufferSize(256 * 1024);
    QObject::connect(socket, &QWebSocket::connected, socket, [s, id]() {
        socketEvent(s, id, "open");
    });
    QObject::connect(socket, &QWebSocket::textMessageReceived, socket,
        [s, id, socket, remaining = qsizetype(2 * 1024 * 1024)](const QString &text) mutable {
        remaining -= text.size() * qsizetype(sizeof(QChar));
        if (remaining < 0) {
            socket->abort();
            return;
        }
        socketEvent(s, id, "message", text);
    });
    QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket, [s, id, socket]() {
        socketEvent(s, id, "error");
        socket->abort();
    });
    QObject::connect(socket, &QWebSocket::errorOccurred, socket,
        [s, id, socket](QAbstractSocket::SocketError) {
        socketEvent(s, id, "error");
        socket->abort();
        s->sockets.remove(id);
        socket->deleteLater();
    });
    QObject::connect(socket, &QWebSocket::disconnected, socket, [s, id, socket]() {
        s->sockets.remove(id);
        socketEvent(s, id, "close");
        socket->deleteLater();
    });
    QTimer::singleShot(static_cast<int>(std::max<qint64>(1, NuvioWorkerProtocol::pluginDeadlineMs - s->clock.elapsed())),
                       socket, [socket]() { socket->abort(); });
    NuvioNetwork::resolve(endpoint, socket, [s, socket, id, url](
                             const QHostAddress &address, const QString &error) {
        if (s->done || !s->sockets.contains(id)) return;
        if (!error.isEmpty()) {
            s->sockets.remove(id);
            socketEvent(s, id, "error");
            socketEvent(s, id, "close");
            socket->deleteLater();
            return;
        }
        QNetworkRequest request(url);
        NuvioNetwork::pin(request, address);
        socket->open(request);
    });
    return JS_NewInt32(ctx, id);
}

static JSValue jsSendSocket(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t id = 0;
    if (argc < 2) return reject(ctx, "WebSocket send requires data");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    auto *socket = state(ctx)->sockets.value(id);
    const QString text = str(ctx, argv[1]);
    if (!socket || !socket->isValid() || text.size() > 65536 || socket->bytesToWrite() > 256 * 1024)
        return reject(ctx, "WebSocket send unavailable");
    socket->sendTextMessage(text);
    return JS_UNDEFINED;
}

static JSValue jsCloseSocket(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    int32_t id = 0;
    if (!argc) return JS_UNDEFINED;
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    auto *s = state(ctx);
    if (auto *socket = s->sockets.take(id)) {
        socket->blockSignals(true);
        socket->abort();
        socket->deleteLater();
    }
    return JS_UNDEFINED;
}


static bool evaluate(JSContext *ctx, const QByteArray &code, const char *name)
{
    JSValue value = JS_Eval(ctx, code.constData(), code.size(), name, JS_EVAL_TYPE_GLOBAL);
    const bool ok = !JS_IsException(value);
    if (!ok) captureException(state(ctx));
    JS_FreeValue(ctx, value);
    return ok;
}

static bool evaluateFile(Host *s, const QString &path, const char *name)
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 4 * 1024 * 1024) {
        s->error = "host_script_unavailable";
        return false;
    }
    return evaluate(s->context, source.readAll(), name);
}

static void install(Host *s)
{
    JSContext *ctx = s->context;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    for (const char *name : {"log", "warn", "error", "debug", "info"})
        JS_SetPropertyStr(ctx, console, name, JS_NewCFunction(ctx, jsConsole, name, 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    struct Binding { const char *name; JSCFunction *fn; int argc; };
    for (const Binding &b : {Binding{"__request", jsRequest, 2}, {"__cancelRequest", jsCancelRequest, 1},
         {"__done", jsDone, 1}, {"__fail", jsError, 1},
         {"__startTimer", jsStartTimer, 2}, {"__clearTimer", jsClearTimer, 1},
         {"__openSocket", jsOpenSocket, 1}, {"__sendSocket", jsSendSocket, 2},
         {"__closeSocket", jsCloseSocket, 1}, {"__decodeUtf8", jsDecodeUtf8, 1}, {"__random", jsRandom, 1}}) {
        JS_SetPropertyStr(ctx, global, b.name, JS_NewCFunction(ctx, b.fn, b.name, b.argc));
    }
    JS_FreeValue(ctx, global);
    if (evaluateFile(s, QStringLiteral(":/nuvio/bootstrap.js"), "host-bootstrap"))
        evaluateFile(s, QStringLiteral(":/nuvio/vendor.js"), "vendor-modules");
}


static QJsonArray publicStreams(const QJsonArray &rows, int remainingMs)
{
    if (remainingMs <= 0)
        return {};

    QEventLoop loop;
    QHash<QString, bool> publicHosts;
    QList<QPair<QString, QJsonObject>> candidates;
    int pending = 0;
    for (const auto &value : rows) {
        if (candidates.size() >= NuvioWorkerProtocol::maxStreams)
            break;
        const auto row = value.toObject();
        const QUrl url(row.value("url").toString(), QUrl::StrictMode);
        if (!row.value("infoHash").toString().isEmpty() || !NuvioNetwork::publicUrl(url, true))
            continue;

        const QString host = url.host();
        if (!publicHosts.contains(host)) {
            // Bound DNS fan-out even when an extension returns many distinct origins.
            if (publicHosts.size() >= 16)
                continue;
            publicHosts.insert(host, false);
            ++pending;
            NuvioNetwork::resolve(url, &loop, [&loop, &publicHosts, &pending, host](
                                      const QHostAddress &, const QString &error) {
                publicHosts[host] = error.isEmpty();
                if (--pending == 0)
                    loop.quit();
            });
        }
        candidates.append({host, row});
    }
    if (pending) {
        QTimer::singleShot(std::min(remainingMs, 3000), &loop, &QEventLoop::quit);
        loop.exec();
    }

    QJsonArray eligible;
    for (const auto &[host, row] : candidates) {
        if (publicHosts.value(host))
            eligible.append(row);
    }
    return eligible;
}

static int writeResult(const QJsonArray &results, const QString &error)
{
    const QJsonObject report = error.isEmpty()
        ? QJsonObject{{"status", "OK"}, {"streams", results}}
        : QJsonObject{{"status", "ERROR"}, {"error", error}};
    const auto out = QJsonDocument(report).toJson(QJsonDocument::Compact);
    if (out.size() > NuvioWorkerProtocol::maxOutputBytes)
        return writeResult({}, QStringLiteral("result_limit"));
    std::fwrite(out.constData(), 1, out.size(), stdout);
    std::fputc('\n', stdout);
    return error.isEmpty() ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    const auto args = app.arguments();
    const bool lookup = args.size() == 5 && args[1] == QLatin1String("--lookup");
    const bool plugin = args.size() == 9 && args[1] == QLatin1String("--plugin");
    if (!lookup && !plugin)
        return writeResult({}, QStringLiteral("invalid_invocation"));

    const QString mediaId = args[lookup ? 2 : 4];
    const QString mediaType = args[lookup ? 3 : 5];
    const QRegularExpression idPattern(lookup ? QStringLiteral(R"(\Att[0-9]{6,12}\z)")
                                              : QStringLiteral(R"(\A[1-9][0-9]{0,9}\z)"));
    if (!idPattern.match(mediaId).hasMatch() || (mediaType != "movie" && mediaType != "tv"))
        return writeResult({}, QStringLiteral("invalid_media"));

    int season = 1;
    int episode = 1;
    QByteArray code;
    if (plugin) {
        bool seasonOk = false, episodeOk = false;
        season = args[6].toInt(&seasonOk);
        episode = args[7].toInt(&episodeOk);
        if (!seasonOk || season < 0 || !episodeOk || episode < 1)
            return writeResult({}, QStringLiteral("invalid_episode"));

        QFile source(args[2]);
        if (!source.open(QIODevice::ReadOnly) || source.size() > NuvioWorkerProtocol::maxScriptBytes)
            return writeResult({}, QStringLiteral("script_unavailable"));
        code = source.read(NuvioWorkerProtocol::maxScriptBytes + 1);
        if (code.isEmpty() || code.size() > NuvioWorkerProtocol::maxScriptBytes
            || QCryptographicHash::hash(code, QCryptographicHash::Sha256).toHex()
                != args[3].toLatin1()) {
            return writeResult({}, QStringLiteral("script_changed"));
        }
    } else {
        code = R"JS(
module.exports.getStreams = async (imdb, type) => {
    const response = await fetch("https://api.themoviedb.org/3/find/" + imdb
        + "?external_source=imdb_id&api_key=" + TMDB_API_KEY);
    if (!response.ok) throw new Error("TMDB lookup failed");
    const result = await response.json();
    const first = (type === "movie" ? result.movie_results : result.tv_results)?.[0];
    return first ? [{tmdbId: first.id}] : [];
};
)JS";
    }

    Host s;
    s.retryDirectory = args.last();
    s.network.setProxy(QNetworkProxy::NoProxy);
    s.clock.start();
    s.runtime = JS_NewRuntime();
    if (!s.runtime)
        return writeResult({}, QStringLiteral("runtime_unavailable"));
    JS_SetMemoryLimit(s.runtime, 32 * 1024 * 1024);
    JS_SetMaxStackSize(s.runtime, 512 * 1024);
    JS_SetInterruptHandler(s.runtime, jsInterrupt, &s);
    s.context = JS_NewContext(s.runtime);
    if (!s.context) {
        JS_FreeRuntime(s.runtime);
        return writeResult({}, QStringLiteral("runtime_unavailable"));
    }
    JS_SetContextOpaque(s.context, &s);
    install(&s);
    if (s.error.isEmpty())
        evaluate(s.context, code, "plugin");
    if (s.error.isEmpty()) {
        JSValue global = JS_GetGlobalObject(s.context);
        JS_SetPropertyStr(s.context, global, "__tmdb", JS_NewString(s.context, mediaId.toLatin1().constData()));
        JS_SetPropertyStr(s.context, global, "__type", JS_NewString(s.context, mediaType.toLatin1().constData()));
        JS_SetPropertyStr(s.context, global, "__season", JS_NewInt32(s.context, season));
        JS_SetPropertyStr(s.context, global, "__episode", JS_NewInt32(s.context, episode));
        JS_FreeValue(s.context, global);
        const QByteArray invoke = R"JS(
Promise.resolve().then(() => {
    const fn = module.exports.getStreams || globalThis.getStreams;
    if (typeof fn !== "function") throw new Error("getStreams missing");
    return fn(__tmdb, __type, __season, __episode);
}).then(__done, __fail);
)JS";
        evaluate(s.context, invoke, "host-invoke");
        int jobs = 0;
        while (s.error.isEmpty() && !s.done && s.clock.elapsed() < NuvioWorkerProtocol::pluginDeadlineMs) {
            JSContext *jobContext = nullptr;
            const int job = JS_ExecutePendingJob(s.runtime, &jobContext);
            if (job < 0) {
                captureException(&s);
                break;
            }
            if (job == 0 || ++jobs % 64 == 0)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            if (job == 0)
                QThread::msleep(1);
        }
        if (!s.done && s.error.isEmpty())
            s.error = QStringLiteral("deadline");
    }
    s.done = true;
    JS_FreeContext(s.context);
    JS_FreeRuntime(s.runtime);
    const int remaining = NuvioWorkerProtocol::pluginDeadlineMs - s.clock.elapsed();
    if (remaining <= 0)
        s.error = QStringLiteral("deadline");
    if (!s.error.isEmpty())
        return writeResult({}, s.error);
    const auto results = plugin ? publicStreams(s.results, remaining) : s.results;
    if (s.clock.elapsed() >= NuvioWorkerProtocol::pluginDeadlineMs)
        return writeResult({}, QStringLiteral("deadline"));
    return writeResult(results, {});
}
