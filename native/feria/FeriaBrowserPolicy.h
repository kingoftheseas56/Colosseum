#pragma once

#include <QObject>
#include <QStandardPaths>
#include <QUrl>

// Shared by both browser hosts, including authentication popups.
class FeriaBrowserPolicy final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageRoot READ storageRoot CONSTANT)
    Q_PROPERTY(bool webView2Available READ webView2Available CONSTANT)
public:
    using QObject::QObject;

    QString storageRoot() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/feria/browser");
    }

    bool webView2Available() const;
    static void registerTypes();

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
};
