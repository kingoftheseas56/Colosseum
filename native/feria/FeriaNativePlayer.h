#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
class MpvItem;

// One transient playback owner per retained browser view. Canonical progress
// remains in FeriaMediaSession/FeriaAccountStore; resolved CDN URLs stay transient.
class FeriaNativePlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap currentState READ snapshot NOTIFY stateChanged)
public:
    explicit FeriaNativePlayer(QObject *parent=nullptr);
    bool active() const { return !m_session.isEmpty(); }
    static bool allowsSource(const QUrl &source);
    Q_INVOKABLE bool openSource(const QUrl &source, const QString &document,
        double position, bool paused, int volume, double rate, const QVariantMap &headers, const QUrl &audioSource=QUrl());
    Q_INVOKABLE void bindSurface(QObject *surface);
    Q_INVOKABLE void close();
    Q_INVOKABLE void setSuspended(bool suspended);
    Q_INVOKABLE QVariantMap bootstrap() const;
    Q_INVOKABLE QVariantMap act(const QString &document, const QString &session,
        const QString &action, double value=0);
    QVariantMap snapshot() const;
signals:
    void activeChanged();
    void stateChanged();
    void state(const QVariantMap &sample);
    void closeRequested();
    void fullscreenRequested();
private:
    void publish();
    QPointer<MpvItem> m_player;
    QTimer m_tick, m_deadline;
    QString m_document, m_session, m_error;
    QUrl m_source, m_audioSource;
    QVariantMap m_headers;
    double m_start=0, m_rate=1;
    int m_volume=100;
    bool m_loaded=false, m_ready=false, m_paused=false, m_ended=false;
    bool m_suspended=false, m_resumeOwned=false;
};
