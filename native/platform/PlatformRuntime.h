#pragma once

#include "PlatformKind.h"

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <memory>

class QEvent;
class QWindow;

namespace Colosseum::Platform {
struct AndroidRedrawState;

class Runtime final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)
    Q_PROPERTY(bool androidTelevision READ androidTelevision CONSTANT)
    Q_PROPERTY(QVariantMap videoDecodeCapabilities READ videoDecodeCapabilities CONSTANT)
    Q_PROPERTY(QString applicationState READ applicationState NOTIFY applicationStateChanged)
    Q_PROPERTY(bool foreground READ foreground NOTIFY applicationStateChanged)
    Q_PROPERTY(bool keyboardVisible READ keyboardVisible NOTIFY keyboardVisibleChanged)
    Q_PROPERTY(bool backGestureActive READ backGestureActive NOTIFY backGestureChanged)
    Q_PROPERTY(qreal backGestureProgress READ backGestureProgress NOTIFY backGestureChanged)
    Q_PROPERTY(int backGestureEdge READ backGestureEdge NOTIFY backGestureChanged)
    Q_PROPERTY(qreal playbackBrightness READ playbackBrightness NOTIFY playbackBrightnessChanged)
    Q_PROPERTY(qreal fontScale READ fontScale NOTIFY fontScaleChanged)
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
    bool androidTelevision() const;
    QVariantMap videoDecodeCapabilities() const;
    QString applicationState() const;
    bool foreground() const;
    bool keyboardVisible() const;
    bool backGestureActive() const { return m_backGestureActive; }
    qreal backGestureProgress() const { return m_backGestureProgress; }
    int backGestureEdge() const { return m_backGestureEdge; }
    // Platform callback entry, executed on the Qt thread. Progress/cancellation never navigate.
    void acceptBackGesture(int phase, qreal progress, int edge);
    qreal playbackBrightness() const { return m_playbackBrightness; }
    void acceptPlaybackBrightness(qreal value);
    qreal fontScale() const { return m_fontScale; }
    void acceptFontScale(qreal value);
    void requestSurfaceRedraw(qint64 token, int width, int height);
    Q_INVOKABLE void beginPlaybackBrightness();
    Q_INVOKABLE void setPlaybackBrightness(qreal value);
    Q_INVOKABLE void restorePlaybackBrightness();
    QMargins safeAreaMargins() const;
    bool surfaceAvailable() const;
    bool storageAccessFrameworkAvailable() const;
    bool backgroundDownloadNotificationsAvailable() const;
    bool desktopUpdaterAvailable() const;
    bool desktopWindowChromeAvailable() const;

    Q_INVOKABLE void attachWindow(QWindow *window);
    Q_INVOKABLE void setPlayerLandscape(bool enabled);
    Q_INVOKABLE void dismissKeyboard();
    Q_INVOKABLE void dispatchSystemBack();
    Q_INVOKABLE void backgroundApplication();
    Q_INVOKABLE bool requestStorageAccess(const QString &purpose = QString());
    Q_INVOKABLE bool requestPermission(const QString &permission);

signals:
    void applicationStateChanged();
    void keyboardVisibleChanged();
    void backGestureChanged();
    void playbackBrightnessChanged();
    void fontScaleChanged();
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
    bool m_androidTelevision = false;
    bool m_backGestureActive = false;
    qreal m_backGestureProgress = 0;
    int m_backGestureEdge = 0;
    qint64 m_backBridgeHandle = 0;
    qreal m_playbackBrightness = 0.5;
    qreal m_fontScale = 1;
    mutable QVariantMap m_videoDecodeCapabilities;
    std::shared_ptr<AndroidRedrawState> m_androidRedraw;
};

} // namespace Colosseum::Platform
