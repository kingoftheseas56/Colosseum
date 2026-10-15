#pragma once

#include <QObject>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QStringList>

// Shared by both browser hosts, including authentication popups.
class FeriaBrowserPolicy final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageRoot READ storageRoot NOTIFY storageRootChanged)
    Q_PROPERTY(bool webView2Available READ webView2Available CONSTANT)
public:
    using QObject::QObject;

    QString storageRoot() const
    {
        return m_storageRoot;
    }
    void setStorageRoot(const QString &path) {
        if (path == m_storageRoot) return;
        m_storageRoot = path;
        emit storageRootChanged();
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

    Q_INVOKABLE bool allowsNavigation(const QUrl &url) const { return allows(url); }
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
    void cookiesCleared(int request, bool success);
private:
    int m_cookieRequest = 0;
    QString m_storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/feria/browser");
};
