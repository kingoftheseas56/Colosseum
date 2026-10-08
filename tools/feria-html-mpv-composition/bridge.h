#pragma once
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonArray>
#include "sessiongate.h"
class MpvItem;
class QQuickWindow;
class Bridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF aperture READ aperture NOTIFY apertureChanged)
    Q_PROPERTY(bool surfaceVisible READ surfaceVisible NOTIFY apertureChanged)
public:
    explicit Bridge(QString fixtures, bool selfTest, QObject *parent = nullptr);
    void bind(MpvItem *player);
    Q_INVOKABLE void bindSurface(QObject *player);
    QRectF aperture() const { return m_aperture; }
    bool surfaceVisible() const { return m_visible && !m_gate.session().isEmpty(); }
    QVariantMap snapshot() const;
    Q_INVOKABLE void navigate(); // Native QML lifecycle only; facade proxy excludes this method.
    QVariantMap bootstrap() const;
    QVariantMap load(const QString &document, const QString &session, const QString &fixture);
    QVariantMap command(const QString &document, const QString &session, const QString &action, double value);
    QVariantMap geometry(const QString &document, const QVariantMap &rect);
    QVariantMap diagnostics();
    QString fixturePath(const QString &id) const;
    void setZoom(double zoom) { m_zoom = zoom; }
    QQuickWindow *window = nullptr;
    int stateNotifications = 0;
    int positionNotifications = 0;
    int renderedFrames = 0;
    QJsonArray eventLog;
    QElapsedTimer elapsed;
signals:
    void state(QVariantMap state);
    void apertureChanged();
    void fullscreenRequested(bool fullscreen);
    void surfaceRequested(bool enabled);
private:
    void publish();
    void startPending();
    QVariantMap reject(const QString &reason) const;
    QPointer<MpvItem> m_player;
    SessionGate m_gate;
    QString m_fixtures, m_fixture, m_pending, m_error, m_activeSession;
    QRectF m_aperture;
    QTimer m_tick;
    bool m_visible = false, m_loaded = false, m_active = false, m_stopping = false, m_ended = false, m_selfTest, m_rendererReady = false;
    double m_zoom = 1;
};
// Only this narrow object enters WebChannel. No player pointer, raw commands,
// arbitrary URLs, inherited MpvItem API, persistence or provider credentials.
class PlayerFacade final : public QObject {
    Q_OBJECT
public:
    explicit PlayerFacade(Bridge *bridge) : b(bridge) { connect(b, &Bridge::state, this, &PlayerFacade::state); }
    Q_INVOKABLE QVariantMap bootstrap() const { return b->bootstrap(); }
    Q_INVOKABLE QVariantMap loadFixture(QString document, QString session, QString fixture) { return b->load(document, session, fixture); }
    Q_INVOKABLE QVariantMap act(QString document, QString session, QString action, double value) { return b->command(document, session, action, value); }
    Q_INVOKABLE QVariantMap layout(QString document, QVariantMap rect) { return b->geometry(document, rect); }
    Q_INVOKABLE QVariantMap snapshot() const { return b->snapshot(); }
signals:
    void state(QVariantMap state);
private:
    Bridge *b;
};
class TestProbe final : public QObject {
    Q_OBJECT
public:
    TestProbe(Bridge *bridge, QString evidence);
    Q_INVOKABLE QVariantMap check(QString name, QVariantMap nativeExpectations, QVariantMap htmlEvidence);
    Q_INVOKABLE QVariantMap capture(QString name, QVariantMap htmlEvidence);
    Q_INVOKABLE void result(QString name, bool passed, QVariantMap detail);
    Q_INVOKABLE QVariantMap diagnostics();
    Q_INVOKABLE void resize(int width, int height);
    Q_INVOKABLE void click(double cssX, double cssY);
    Q_INVOKABLE void zoom(double factor);
    Q_INVOKABLE void finish();
    Q_INVOKABLE void webOpacity(double value) { emit webOpacityRequested(value == 0 ? 0 : 1); }
    void write();
    void fail(QString reason);
signals:
    void zoomRequested(double factor);
    void webOpacityRequested(double value);
private:
    Bridge *b;
    QString directory;
    QJsonArray checks, captures, stats;
    bool failed = false;
};
