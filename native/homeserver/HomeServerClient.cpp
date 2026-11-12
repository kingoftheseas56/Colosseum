#include "HomeServerClient.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
namespace {
QString quoted(QString value) { value.replace('\\', "\\\\"); value.replace('"', "\\\""); return '"' + value + '"'; }
bool known(const QString &provider) { return provider == "jellyfin" || provider == "emby" || provider == "plex"; }
bool validBase(const QUrl &u) { return u.isValid() && !u.host().isEmpty() && (u.scheme() == "http" || u.scheme() == "https") && u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment(); }
HomeServerClient::Response invalid(HomeServerClient::Response r, const QString &error) { r.body = {}; r.error = error; return r; }
}
HomeServerClient::HomeServerClient(QObject *parent, QString directory, QUrl plexOrigin)
    : QObject(parent), m_identity(std::move(directory)) {
    const QUrl canonical("https://plex.tv");
    // This constructor-only seam cannot redirect production sign-in to arbitrary hosts.
    const QHostAddress testAddress(plexOrigin.host());
    if (plexOrigin == canonical) {
        m_plexOrigin = canonical;
        m_plexResourcesOrigin = QUrl("https://clients.plex.tv");
    } else if (testAddress.isLoopback() && validBase(plexOrigin)) {
        m_plexOrigin = m_plexResourcesOrigin = plexOrigin;
    }
}
QUrl HomeServerClient::normalizeAddress(QString address) {
    address = address.trimmed();
    if (address.isEmpty() || address.contains(QRegularExpression("[\\x00-\\x20\\x7f\\\\]"))) return {};
    if (!address.contains("://")) address.prepend("http://");
    QUrl url(address, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https") || url.port() == 0) return {};
    url.setUserInfo({}); url.setQuery(QString()); url.setFragment(QString());
    QString path = url.path(QUrl::FullyEncoded);
    while (path.endsWith('/')) path.chop(1);
    for (const auto &segment : url.path(QUrl::FullyDecoded).split('/')) if (segment == ".." || segment == ".") return {};
    url.setPath(path, QUrl::StrictMode);
    return url;
}
QUrl HomeServerClient::relativeUrl(QUrl origin, QString path) {
    if (!validBase(origin) || path.contains('\\')) return {};
    while (path.startsWith('/')) path.remove(0, 1);
    QUrl relative(path, QUrl::StrictMode);
    if (!relative.isValid() || !relative.isRelative() || relative.hasFragment() || !relative.host().isEmpty()) return {};
    const QString decoded = relative.path(QUrl::FullyDecoded);
    for (const auto &segment : decoded.split('/')) if (segment == ".." || segment == ".") return {};
    if (decoded.contains('\\')) return {};
    QString base = origin.path(QUrl::FullyEncoded);
    while (base.endsWith('/')) base.chop(1);
    origin.setPath(base + '/' + relative.path(QUrl::FullyEncoded), QUrl::StrictMode);
    origin.setQuery(relative.query(QUrl::FullyEncoded));
    return origin;
}
QMap<QByteArray,QByteArray> HomeServerClient::headers(QString provider, QString token) const {
    QMap<QByteArray,QByteArray> result{{"Accept", "application/json"}, {"Content-Type", "application/json"}};
    if (!isReady() || !known(provider)) return {};
    if (provider == "plex") {
        result.insert("X-Plex-Product", "Colosseum"); result.insert("X-Plex-Version", "1.4.0");
        result.insert("X-Plex-Device", HomeServerDeviceIdentity::deviceName().toUtf8());
        result.insert("X-Plex-Platform", QSysInfo::productType().toUtf8());
        result.insert("X-Plex-Client-Identifier", installationId().toUtf8());
        if (!token.isEmpty()) result.insert("X-Plex-Token", token.toUtf8());
    } else {
        QString auth = (provider == "emby" ? "Emby Client=\"Colosseum\", Device=" : "MediaBrowser Client=\"Colosseum\", Device=") + quoted(HomeServerDeviceIdentity::deviceName())
            + ", DeviceId=" + quoted(installationId()) + ", Version=\"1.4.0\"";
        if (!token.isEmpty() && provider == "jellyfin") auth += ", Token=" + quoted(token);
        result.insert("Authorization", auth.toUtf8());
        if (!token.isEmpty() && provider == "emby") result.insert("X-Emby-Token", token.toUtf8());
    }
    return result;
}
void HomeServerClient::fail(Callback cb, QString error) {
    QTimer::singleShot(0, this, [cb = std::move(cb), error = std::move(error)] { if (cb) cb({0, {}, error}); });
}
void HomeServerClient::request(QString provider, QUrl origin, QString path, QByteArray method,
                                QString token, QJsonValue body, Callback cb) {
    if (!isReady()) { fail(std::move(cb), identityError()); return; }
    const auto url = relativeUrl(origin, path);
    if (!known(provider) || url.isEmpty() || (!body.isNull() && !body.isUndefined() && !body.isArray() && !body.isObject())) {
        fail(std::move(cb), "Invalid server request"); return;
    }
    QByteArray payload;
    if (body.isObject()) payload = QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact);
    else if (body.isArray()) payload = QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact);
    m_transport.send(url, method, headers(provider, token), payload, std::move(cb));
}
void HomeServerClient::authenticate(QString provider, QString address, QString user, QString password,
                                     bool allowHttp, Callback cb) {
    auto origin = normalizeAddress(address);
    if (provider != "jellyfin" && provider != "emby") { fail(std::move(cb), "Unsupported sign-in provider"); return; }
    if (origin.isEmpty()) { fail(std::move(cb), "Invalid server address"); return; }
    if (origin.scheme() == "http" && !allowHttp) { fail(std::move(cb), "HTTP consent required"); return; }
    request(provider, origin, "/Users/AuthenticateByName", "POST", {},
        QJsonObject{{"Username", user}, {"Pw", password}}, [cb = std::move(cb)](Response response) {
        if (response.ok()) {
            const auto object = response.body.toObject();
            if (object.value("AccessToken").toString().isEmpty() || object.value("User").toObject().value("Id").toString().isEmpty())
                response = invalid(std::move(response), "Invalid authentication response");
        }
        if (cb) cb(std::move(response));
    });
}
void HomeServerClient::createPlexPin(Callback cb) {
    request("plex", m_plexOrigin, "/api/v2/pins?strong=false", "POST", {}, {}, [cb = std::move(cb)](Response r) {
        if (r.ok()) {
            const auto pin = r.body.toObject();
            if (pin.value("id").toInteger() <= 0 || !QRegularExpression("^[A-Za-z0-9]{4}$").match(pin.value("code").toString()).hasMatch())
                r = invalid(std::move(r), "Invalid Plex PIN response");
        }
        if (cb) cb(std::move(r));
    });
}
void HomeServerClient::pollPlexPin(qint64 id, QString code, Callback cb) {
    if (id <= 0 || !QRegularExpression("^[A-Za-z0-9]{4}$").match(code).hasMatch()) { fail(std::move(cb), "Invalid Plex PIN"); return; }
    QUrlQuery query; query.addQueryItem("code", code);
    request("plex", m_plexOrigin, "/api/v2/pins/" + QString::number(id) + '?' + query.toString(QUrl::FullyEncoded), "GET", {}, {}, std::move(cb));
}
void HomeServerClient::discoverPlexServers(QString token, Callback cb) {
    if (token.isEmpty()) { fail(std::move(cb), "Plex sign-in required"); return; }
    request("plex", m_plexResourcesOrigin, "/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=1", "GET", token, {}, [cb = std::move(cb)](Response r) {
        if (r.ok()) {
            if (!r.body.isArray()) r = invalid(std::move(r), "Invalid Plex resources response");
            else {
                QJsonArray servers;
                for (const auto &entry : r.body.toArray()) {
                    const auto resource = entry.toObject();
                    const QString serverToken = resource.value("accessToken").toString();
                    if (!resource.value("provides").toString().split(',').contains("server") || serverToken.isEmpty()) continue;
                    QList<QJsonObject> endpoints;
                    for (const auto &connection : resource.value("connections").toArray()) {
                        const auto endpoint = connection.toObject();
                        QString address = endpoint.value("uri").toString();
                        if (address.isEmpty() && !endpoint.value("address").toString().isEmpty() && endpoint.value("port").toInt() > 0) {
                            QUrl assembled; assembled.setScheme(endpoint.value("protocol").toString("http"));
                            assembled.setHost(endpoint.value("address").toString()); assembled.setPort(endpoint.value("port").toInt());
                            address = assembled.toString();
                        }
                        const QUrl raw(address);
                        const QUrl origin = normalizeAddress(address);
                        if (origin.isEmpty() || raw.isRelative() || !raw.userInfo().isEmpty()) continue;
                        endpoints.append(QJsonObject{{"origin", origin.toString()}, {"local", endpoint.value("local").toBool()},
                            {"relay", endpoint.value("relay").toBool()}, {"secure", origin.scheme() == "https"}});
                    }
                    std::stable_sort(endpoints.begin(), endpoints.end(), [](const QJsonObject &a, const QJsonObject &b) {
                        auto rank = [](const QJsonObject &o) { return (o.value("relay").toBool() ? 4 : 0) + (o.value("secure").toBool() ? 0 : 2) + (o.value("local").toBool() ? 0 : 1); };
                        return rank(a) < rank(b);
                    });
                    if (endpoints.isEmpty()) continue;
                    QJsonArray urls; for (const auto &endpoint : endpoints) urls.append(endpoint);
                    const QString id = resource.value("clientIdentifier").toString();
                    if (id.isEmpty()) continue;
                    servers.append(QJsonObject{{"id", id}, {"name", resource.value("name").toString("Plex")},
                        {"owned", resource.value("owned").toBool(true)}, {"available", resource.value("presence").toBool(true)},
                        {"token", serverToken}, {"endpoints", urls}});
                }
                r.body = servers;
            }
        }
        if (cb) cb(std::move(r));
    });
}
void HomeServerClient::verifyPlexServer(QUrl origin, QString token, bool allowHttp, Callback cb) {
    if (origin.scheme() == "http" && !allowHttp) { fail(std::move(cb), "HTTP consent required"); return; }
    if (token.isEmpty()) { fail(std::move(cb), "Plex sign-in required"); return; }
    request("plex", origin, "/identity", "GET", token, {}, [cb = std::move(cb)](Response r) {
        if (r.ok()) {
            const auto body = r.body.toObject();
            const auto container = body.value("MediaContainer").toObject();
            const QString id = container.value("machineIdentifier").toString(body.value("machineIdentifier").toString());
            if (id.isEmpty()) r = invalid(std::move(r), "Invalid Plex identity response");
            else r.body = QJsonObject{{"id", id}, {"available", true}};
        }
        if (cb) cb(std::move(r));
    });
}
