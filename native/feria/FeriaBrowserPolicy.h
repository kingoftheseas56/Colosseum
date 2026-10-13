#pragma once

#include <QObject>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QStringList>
#include <QSet>
#include <QRect>
#include <QMargins>
#include "FeriaBrowserRuntime.h"

// Shared by both browser hosts, including authentication popups.
class FeriaBrowserPolicy final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageRoot READ storageRoot NOTIFY storageRootChanged)
    Q_PROPERTY(bool webView2Available READ webView2Available CONSTANT)
    Q_PROPERTY(int runtimeProfileCount READ runtimeProfileCount NOTIFY runtimeProfileCountChanged)
    Q_PROPERTY(int runtimeViewCount READ runtimeViewCount NOTIFY runtimeViewCountChanged)
    // Engine instance lifecycle, outstanding work and the trusted command door.
    Q_PROPERTY(FeriaBrowserRuntime *runtime READ runtime CONSTANT)
public:
    explicit FeriaBrowserPolicy(QObject *parent = nullptr);
    FeriaBrowserRuntime *runtime() const { return m_runtime; }

    QString storageRoot() const
    {
        return m_storageRoot;
    }
    void setStorageRoot(const QString &path) {
        if (path == m_storageRoot) return;
        ++m_storageGeneration;
        m_storageRoot = path;
        emit storageRootChanged();
    }

    // The runtime is the one tracker of live engine views and profiles; these
    // remain as the names existing callers use.
    int runtimeViewCount() const { return m_runtime->viewCount(); }
    // This marker is supplied by our patched Qt constructor, not by a version
    // string. Stock 6.11.1 must retain its creation-view lifetime guard.
    Q_INVOKABLE bool independentAdoptionAvailable(QObject *view) const {
        if (!view || !m_runtime->hasView(view)) return false;
        const QVariant marker = view->property("_colosseumAdoptionInputRebound");
        return marker.metaType().id() == QMetaType::Bool && marker.toBool();
    }
    Q_INVOKABLE int runtimeViewsForProfile(QObject *profile) const {
        return m_runtime->viewsForProfile(profile);
    }
    Q_INVOKABLE void registerRuntimeView(QObject *view) { m_runtime->registerView(view); }
    int runtimeProfileCount() const { return m_runtime->profileCount(); }
    Q_INVOKABLE void registerRuntimeProfile(QObject *profile) { m_runtime->registerProfile(profile); }
    bool webView2Available() const;
    static void registerTypes();
    Q_INVOKABLE void rememberOrigin(const QString &provider, const QUrl &url);
    Q_INVOKABLE QVariantMap sessionScope(const QString &provider) const;
    Q_INVOKABLE int clearQtCookies(const QString &profilePath, const QStringList &domains);
    static bool cookieInScope(QString domain, const QStringList &domains) {
        while (domain.startsWith('.')) domain.remove(0, 1);
        domain = domain.toLower();
        if (domain.isEmpty()) return false;
        for (QString host : domains) {
            host = host.toLower();
            if (!host.isEmpty() && (domain == host || domain.endsWith('.' + host) || host.endsWith('.' + domain))) return true;
        }
        return false;
    }

    // All geometry is in Qt logical pixels, matching QScreen/QWindow APIs.
    Q_INVOKABLE QRect windowWorkArea(QObject *window = nullptr) const;
    Q_INVOKABLE QRect boundedWindowGeometry(const QRect &requested, QObject *window = nullptr) const;
    Q_INVOKABLE QRect clampWindowGeometry(const QRect &requested, const QRect &workArea,
                                          const QMargins &frameMargins) const;
    Q_INVOKABLE bool allowsNavigation(const QUrl &url) const { return allows(url); }
    Q_INVOKABLE bool sameWebOrigin(const QUrl &claimed, const QUrl &engineUrl) const {
        // A blob document inherits its HTTP(S) creator origin. Accept that
        // single inner origin on either side, never opaque or nested blobs.
        const auto webUrl = [](const QUrl &input) {
            if (!input.isValid() || input.isEmpty()) return QUrl();
            const QUrl url = input.scheme().compare(QStringLiteral("blob"), Qt::CaseInsensitive) == 0
                ? QUrl(input.toString(QUrl::FullyEncoded).mid(5), QUrl::StrictMode) : input;
            const QString scheme = url.scheme().toLower();
            if (!url.isValid() || url.host().isEmpty()
                || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) return QUrl();
            return url;
        };
        const QUrl left = webUrl(claimed), right = webUrl(engineUrl);
        if (left.isEmpty() || right.isEmpty()) return false;
        const QString scheme = left.scheme().toLower();
        const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
        // QUrl canonicalizes case, IDNs and IP literals. Authority userinfo,
        // document paths, queries and fragments are not origin components.
        return scheme == right.scheme().toLower()
            && left.host(QUrl::FullyEncoded) == right.host(QUrl::FullyEncoded)
            && left.port(defaultPort) == right.port(defaultPort);
    }
    Q_INVOKABLE QString displayOrigin(const QUrl &url) const {
        if (!url.isValid()) return {};
        if (url == QUrl(QStringLiteral("about:blank"))) return QStringLiteral("about:blank");
        const QUrl origin = url.scheme() == QStringLiteral("blob")
            ? QUrl(url.toString().mid(5)) : url;
        const QString scheme = origin.scheme().toLower();
        if (!origin.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
            || origin.host().isEmpty()) return {};
        // Never display userinfo, paths or query credentials as site identity.
        // ASCII IDNs avoid presenting visually confusable Unicode hostnames.
        const QString host = origin.host().contains(QLatin1Char(':'))
            ? QLatin1Char('[') + origin.host().toLower() + QLatin1Char(']')
            : QString::fromLatin1(QUrl::toAce(origin.host())).toLower();
        if (host.isEmpty()) return {};
        const QString port = origin.port() < 0 ? QString() : QLatin1Char(':') + QString::number(origin.port());
        return scheme + QStringLiteral("://") + host + port;
    }
    Q_INVOKABLE QUrl navigationUrl(const QString &text) const;
    Q_INVOKABLE QUrl omniboxUrl(const QString &text, const QString &searchEngine = QStringLiteral("duckduckgo")) const;
    static bool allows(const QUrl &url)
    {
        if (!url.isValid())
            return false;
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
            return !url.host().isEmpty();
        if (url.toString() == QStringLiteral("about:blank"))
            return true;
        if (scheme == QStringLiteral("blob")) {
            const QUrl origin(url.toString().mid(5));
            return (origin.scheme() == QStringLiteral("http")
                    || origin.scheme() == QStringLiteral("https"))
                && !origin.host().isEmpty();
        }
        return false;
    }
signals:
    void storageRootChanged();
    void runtimeProfileCountChanged();
    void runtimeViewCountChanged();
    void cookiesCleared(int request, bool success);
private:
    int m_cookieRequest = 0;
    quint64 m_storageGeneration = 0;
    FeriaBrowserRuntime *m_runtime = nullptr;
    QString m_storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/feria/browser");
};
