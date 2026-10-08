#pragma once

#include <QObject>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QStringList>
#include <QSet>
#include <QRect>
#include <QMargins>

// Shared by both browser hosts, including authentication popups.
class FeriaBrowserPolicy final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageRoot READ storageRoot NOTIFY storageRootChanged)
    Q_PROPERTY(bool webView2Available READ webView2Available CONSTANT)
    Q_PROPERTY(int runtimeProfileCount READ runtimeProfileCount NOTIFY runtimeProfileCountChanged)
    Q_PROPERTY(int runtimeViewCount READ runtimeViewCount NOTIFY runtimeViewCountChanged)
public:
    using QObject::QObject;

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

    int runtimeViewCount() const { return m_runtimeViews.size(); }
    // This marker is supplied by our patched Qt constructor, not by a version
    // string. Stock 6.11.1 must retain its creation-view lifetime guard.
    Q_INVOKABLE bool independentAdoptionAvailable(QObject *view) const {
        if (!view || !m_runtimeViews.contains(view)) return false;
        const QVariant marker = view->property("_colosseumAdoptionInputRebound");
        return marker.metaType().id() == QMetaType::Bool && marker.toBool();
    }
    Q_INVOKABLE int runtimeViewsForProfile(QObject *profile) const {
        int count = 0;
        for (QObject *view : m_runtimeViews)
            if (view->property("profile").value<QObject *>() == profile) ++count;
        return count;
    }
    Q_INVOKABLE void registerRuntimeView(QObject *view) {
        if (!view || m_runtimeViews.contains(view)) return;
        m_runtimeViews.insert(view);
        connect(view, &QObject::destroyed, this, [this, view] {
            m_runtimeViews.remove(view);
            emit runtimeViewCountChanged();
        });
        emit runtimeViewCountChanged();
    }
    int runtimeProfileCount() const { return m_runtimeProfiles.size(); }
    Q_INVOKABLE void registerRuntimeProfile(QObject *profile) {
        if (!profile || m_runtimeProfiles.contains(profile)) return;
        m_runtimeProfiles.insert(profile);
        connect(profile, &QObject::destroyed, this, [this, profile] {
            m_runtimeProfiles.remove(profile);
            emit runtimeProfileCountChanged();
        });
        emit runtimeProfileCountChanged();
    }
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
    QSet<QObject *> m_runtimeProfiles;
    QSet<QObject *> m_runtimeViews;
    QString m_storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/feria/browser");
};
