#pragma once

#include "PlatformKind.h"

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QString>

class QEvent;
class QWindow;

namespace Colosseum::Platform {

class Runtime final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)
    Q_PROPERTY(QString applicationState READ applicationState NOTIFY applicationStateChanged)
    Q_PROPERTY(bool foreground READ foreground NOTIFY applicationStateChanged)
    Q_PROPERTY(bool keyboardVisible READ keyboardVisible NOTIFY keyboardVisibleChanged)
    Q_PROPERTY(QMargins safeAreaMargins READ safeAreaMargins NOTIFY safeAreaMarginsChanged)
    Q_PROPERTY(bool surfaceAvailable READ surfaceAvailable NOTIFY surfaceAvailableChanged)
    Q_PROPERTY(bool storageAccessFrameworkAvailable READ storageAccessFrameworkAvailable CONSTANT)
    Q_PROPERTY(bool backgroundDownloadNotificationsAvailable READ backgroundDownloadNotificationsAvailable CONSTANT)
    Q_PROPERTY(bool desktopUpdaterAvailable READ desktopUpdaterAvailable CONSTANT)
    Q_PROPERTY(bool desktopWindowChromeAvailable READ desktopWindowChromeAvailable CONSTANT)

public:
    explicit Runtime(QObject *parent = nullptr, Kind kind = currentKind());
    ~Runtime() override;

    QString kind() const;
    bool android() const;
    QString applicationState() const;
    bool foreground() const;
    bool keyboardVisible() const;
    QMargins safeAreaMargins() const;
    bool surfaceAvailable() const;
    bool storageAccessFrameworkAvailable() const;
    bool backgroundDownloadNotificationsAvailable() const;
    bool desktopUpdaterAvailable() const;
    bool desktopWindowChromeAvailable() const;

    Q_INVOKABLE void attachWindow(QWindow *window);
    Q_INVOKABLE void dismissKeyboard();
    Q_INVOKABLE void dispatchSystemBack();
    Q_INVOKABLE bool requestStorageAccess(const QString &purpose = QString());
    Q_INVOKABLE bool requestPermission(const QString &permission);

signals:
    void applicationStateChanged();
    void keyboardVisibleChanged();
    void safeAreaMarginsChanged();
    void surfaceAvailableChanged();
    void backRequested();
    void storageAccessRequested(const QString &purpose);
    void permissionRequested(const QString &permission);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshSafeArea();
    void setSurfaceAvailable(bool available);

    Kind m_kind = Kind::Other;
    Capabilities m_capabilities;
    QPointer<QWindow> m_window;
    QMargins m_safeAreaMargins;
    bool m_surfaceAvailable = false;
};

} // namespace Colosseum::Platform
