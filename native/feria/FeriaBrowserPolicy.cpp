#include "FeriaBrowserPolicy.h"
#ifdef FERIA_HAS_WEBVIEW2
#include "provider-host/FeriaHostItem.h"
#include <qqml.h>
#endif

bool FeriaBrowserPolicy::webView2Available() const
{
#ifdef FERIA_HAS_WEBVIEW2
    return true;
#else
    return false;
#endif
}

void FeriaBrowserPolicy::registerTypes()
{
#ifdef FERIA_HAS_WEBVIEW2
    qmlRegisterType<FeriaHostItem>("Colosseum.FeriaHost", 1, 0, "FeriaHostItem");
#endif
}

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <memory>

namespace {
const QMap<QString, QStringList> sessionDomains{
    {"netflix", {"netflix.com"}}, {"prime", {"primevideo.com", "amazon.com", "amazon.in"}},
    {"hbomax", {"hbomax.com", "max.com"}}, {"disney", {"disneyplus.com", "disney.com", "bamgrid.com"}},
    {"appletv", {"apple.com"}}, {"applemusic", {"apple.com"}},
    {"crunchyroll", {"crunchyroll.com"}}, {"youtube", {"youtube.com", "google.com"}},
    {"ytmusic", {"youtube.com", "google.com"}}, {"hulu", {"hulu.com"}}, {"mubi", {"mubi.com"}},
    {"spotify", {"spotify.com"}}, {"kindle", {"amazon.com", "amazon.in", "primevideo.com"}},
    {"playbooks", {"google.com", "youtube.com"}}, {"mangaplus", {"mangaplus.shueisha.co.jp"}},
    {"viz", {"viz.com"}}, {"webtoon", {"webtoons.com"}},
    {"dcui", {"dcuniverseinfinite.com"}}, {"marvel", {"marvel.com"}}
};
QVariantMap originHistory(const QString &root) {
    QFile file(root + "/origins.json");
    if (root.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}
}


void FeriaBrowserPolicy::rememberOrigin(const QString &provider, const QUrl &url) {
    if (m_storageRoot.isEmpty() || !sessionDomains.contains(provider) || !allows(url)
        || (url.scheme() != "http" && url.scheme() != "https")) return;
    const QString origin = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString();
    auto history = originHistory(m_storageRoot);
    auto origins = history.value(provider).toStringList();
    if (origins.contains(origin)) return;
    origins.append(origin);
    while (origins.size() > 64) origins.removeFirst();
    history.insert(provider, origins);
    QDir().mkpath(m_storageRoot);
    QSaveFile file(m_storageRoot + "/origins.json");
    const QByteArray data = QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
}

QVariantMap FeriaBrowserPolicy::sessionScope(const QString &provider) const {
    if (m_storageRoot.isEmpty() || !sessionDomains.contains(provider)) return {};
    QStringList domains = sessionDomains.value(provider), origins;
    for (const auto &domain : domains) {
        origins << "https://" + domain << "https://www." + domain;
    }
    // Common sign-in and player origins, including shared provider SSO.
    for (const QString &origin : {"https://accounts.google.com", "https://play.google.com", "https://music.youtube.com",
         "https://read.amazon.com", "https://tv.apple.com", "https://music.apple.com", "https://appleid.apple.com",
         "https://idmsa.apple.com", "https://open.spotify.com", "https://accounts.spotify.com", "https://play.hbomax.com", "https://play.max.com", "https://read.marvel.com"})
        if (cookieInScope(QUrl(origin).host(), domains)) origins.append(origin);
    const auto history = originHistory(m_storageRoot);
    for (const auto &origin : history.value(provider).toStringList()) {
        const QUrl url(origin);
        if (url.scheme() != "http" && url.scheme() != "https") continue;
        origins.append(origin); domains.append(url.host());
    }
    // Clear visited subdomains of the same SSO family as well.
    for (const auto &value : history)
        for (const auto &origin : value.toStringList())
            if (cookieInScope(QUrl(origin).host(), domains)) origins.append(origin);
    domains.removeDuplicates(); origins.removeDuplicates();
    return {{"domains", domains}, {"origins", origins}};
}

int FeriaBrowserPolicy::clearQtCookies(const QString &profilePath, const QStringList &domains) {
    const int request = ++m_cookieRequest;
    // Called before a Qt browser context is created, after the provider view has
    // closed. Qt 6.11's public enumeration drops its callback, so remove only
    // matching host records from this app-owned, closed profile transactionally.
    // No cookie values or encrypted credentials are selected or exposed.
    auto *retry = new QTimer(this);
    retry->setInterval(150);
    auto attempts = std::make_shared<int>(0);
    const auto attempt = [this, retry, attempts, request, profilePath, domains] {
        bool ok = !profilePath.isEmpty() && !domains.isEmpty();
        QString cookieFile;
        for (const auto &relative : {"/qtwebengine/Cookies", "/qtwebengine/Network/Cookies"}) {
            const auto candidate = profilePath + relative;
            if (QFile::exists(candidate)) { cookieFile = candidate; break; }
        }
        if (ok && !cookieFile.isEmpty()) {
            const QString connection = "feria-signout-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(cookieFile); db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
                ok = db.open() && db.transaction();
                if (ok) {
                    QStringList hosts;
                    {
                        QSqlQuery query(db);
                        ok = query.exec("SELECT DISTINCT host_key FROM cookies");
                        while (ok && query.next())
                            if (cookieInScope(query.value(0).toString(), domains)) hosts.append(query.value(0).toString());
                    }
                    if (ok) {
                        QSqlQuery query(db);
                        ok = query.prepare("DELETE FROM cookies WHERE host_key = :host");
                        for (const auto &host : hosts) {
                            if (!ok) break;
                            query.bindValue(":host", host); ok = query.exec();
                        }
                    }
                    if (ok) ok = db.commit();
                    else db.rollback();
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(connection);
        }
        if (ok || ++*attempts >= 60 || profilePath.isEmpty() || domains.isEmpty()) {
            retry->stop(); retry->deleteLater(); emit cookiesCleared(request, ok);
        }
    };
    connect(retry, &QTimer::timeout, retry, attempt);
    retry->start();
    return request;
}
