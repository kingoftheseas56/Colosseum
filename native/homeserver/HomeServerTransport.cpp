#include "HomeServerTransport.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr qsizetype MaxBytes = 32 * 1024 * 1024;
bool sameOrigin(const QUrl &a, const QUrl &b) {
    return a.scheme() == b.scheme() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
        && a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80);
}
bool validUrl(const QUrl &u) {
    return u.isValid() && !u.host().isEmpty() && (u.scheme() == "https" || u.scheme() == "http")
        && u.userInfo().isEmpty() && !u.hasFragment();
}
class Exchange final : public QObject {
public:
    Exchange(HomeServerTransport *owner, QNetworkAccessManager *manager): QObject(owner), manager(manager) {
        setObjectName("home-server-exchange");
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this] { finish("Request timed out"); });
    }
    ~Exchange() override {
        if (reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    }
    QNetworkAccessManager *manager;
    QPointer<QNetworkReply> reply;
    QTimer timer;
    QUrl original, url;
    QByteArray method, payload, bytes;
    QMap<QByteArray,QByteArray> headers;
    HomeServerTransport::Callback callback;
    int redirects = 0;
    bool done = false;
    void finish(QString error = {}) {
        if (done) return;
        done = true;
        timer.stop();
        HomeServerTransport::Response result;
        if (reply) result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.error = error;
        if (error.isEmpty() && result.status >= 200 && result.status < 300 && !bytes.isEmpty()) {
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(bytes, &parse);
            if (parse.error != QJsonParseError::NoError) result.error = "Invalid server response";
            else result.body = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        }
        if (reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); reply = nullptr; }
        bytes.clear(); payload.clear(); headers.clear();
        auto cb = std::move(callback);
        deleteLater();
        if (cb) cb(std::move(result));
    }
    void start() {
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        for (auto it = headers.cbegin(); it != headers.cend(); ++it) request.setRawHeader(it.key(), it.value());
        reply = manager->sendCustomRequest(request, method, payload);
        reply->setReadBufferSize(MaxBytes + 1);
        connect(reply, &QNetworkReply::readyRead, this, [this] {
            if (reply->bytesAvailable() > MaxBytes - bytes.size()) { finish("Server response too large"); return; }
            bytes += reply->readAll();
        });
        connect(reply, &QNetworkReply::metaDataChanged, this, [this] {
            if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxBytes) finish("Server response too large");
        });
        connect(reply, &QNetworkReply::finished, this, [this] {
            if (reply->bytesAvailable() > MaxBytes - bytes.size()) { finish("Server response too large"); return; }
            bytes += reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400) {
                const QUrl next = url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                const bool replayable = status == 307 || status == 308
                    || (method == "GET" && (status == 301 || status == 302 || status == 303));
                if (++redirects > 5 || !replayable || !validUrl(next) || !sameOrigin(original, next)) {
                    finish("Unsafe server redirect"); return;
                }
                reply->deleteLater(); reply = nullptr; bytes.clear(); url = next; start(); return;
            }
            if (status < 200 || status >= 300) finish(status ? "Server request failed" : "Network request failed");
            else if (reply->error() != QNetworkReply::NoError) finish("Network request failed");
            else finish();
        });
    }
};
}
HomeServerTransport::HomeServerTransport(QObject *parent): QObject(parent) {}
HomeServerTransport::~HomeServerTransport() {
    const auto exchanges = findChildren<QObject*>("home-server-exchange", Qt::FindDirectChildrenOnly);
    for (auto *exchange : exchanges) delete exchange;
}
void HomeServerTransport::cancelAll() {
    QPointer<HomeServerTransport> guard(this);
    const auto exchanges = findChildren<QObject*>("home-server-exchange", Qt::FindDirectChildrenOnly);
    for (auto *exchange : exchanges) {
        static_cast<Exchange*>(exchange)->finish("Request cancelled");
        if (!guard) return;
    }
}
void HomeServerTransport::send(QUrl url, QByteArray method, QMap<QByteArray,QByteArray> headers,
                               QByteArray body, Callback cb, int timeoutMs) {
    bool valid = validUrl(url) && timeoutMs > 0 && body.size() <= MaxBytes
        && QRegularExpression("^[A-Z]+$").match(QString::fromLatin1(method)).hasMatch();
    const QRegularExpression headerName("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$");
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        valid = valid && headerName.match(QString::fromLatin1(it.key())).hasMatch();
        for (unsigned char c : it.value()) if (c < 32 || c == 127) valid = false;
        const auto lower = it.key().toLower();
        if (lower == "host" || lower == "content-length" || lower == "transfer-encoding" || lower == "connection") valid = false;
    }
    if (!valid) {
        QTimer::singleShot(0, this, [cb = std::move(cb)] { if (cb) cb({0, {}, "Invalid server request"}); }); return;
    }
    auto *exchange = new Exchange(this, &m_manager);
    exchange->original = exchange->url = url;
    exchange->method = std::move(method); exchange->payload = std::move(body);
    exchange->headers = std::move(headers); exchange->callback = std::move(cb);
    exchange->timer.start(timeoutMs); exchange->start();
}
