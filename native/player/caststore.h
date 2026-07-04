#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class CastStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantMap device READ device NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString localServerUrl READ localServerUrl NOTIFY changed)

public:
    explicit CastStore(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    bool active() const { return m_active; }
    bool pending() const { return m_pending; }
    bool playing() const { return m_playing; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QVariantList devices() const { return m_devices; }
    QVariantMap device() const { return m_device; }
    QString error() const { return m_error; }
    QString localServerUrl() const { return m_localServerUrl; }

    Q_INVOKABLE void discoverDevices();
    Q_INVOKABLE void startCasting(const QVariantMap &device, const QVariantMap &media);
    Q_INVOKABLE void stopCasting();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(double seconds);

signals:
    void changed();

private:
    QVariantMap manualReceiver() const;
    QString machineHost() const;

    bool m_scanning = false;
    bool m_active = false;
    bool m_pending = false;
    bool m_playing = false;
    double m_position = 0;
    double m_duration = 0;
    QVariantList m_devices;
    QVariantMap m_device;
    QString m_error;
    QString m_localServerUrl;
};
