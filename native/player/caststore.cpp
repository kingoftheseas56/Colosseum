#include "caststore.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkInterface>

CastStore::CastStore(QObject *parent)
    : QObject(parent),
      m_localServerUrl(QStringLiteral("http://%1:11470").arg(machineHost())) {}

void CastStore::discoverDevices() {
    m_scanning = true;
    m_error.clear();
    emit changed();

    m_devices.clear();
    m_devices.append(manualReceiver());

    m_scanning = false;
    emit changed();
}

void CastStore::startCasting(const QVariantMap &nextDevice, const QVariantMap &media) {
    if (nextDevice.isEmpty()) {
        m_error = QStringLiteral("Pick a device before casting.");
        emit changed();
        return;
    }
    m_pending = true;
    m_error.clear();
    emit changed();

    m_device = nextDevice;
    m_device.insert(QStringLiteral("mediaTitle"), media.value(QStringLiteral("title")).toString());
    m_device.insert(QStringLiteral("mediaUrl"), media.value(QStringLiteral("url")).toString());
    m_device.insert(QStringLiteral("poster"), media.value(QStringLiteral("poster")).toString());
    m_position = media.value(QStringLiteral("position")).toDouble();
    m_duration = media.value(QStringLiteral("duration")).toDouble();
    m_playing = media.value(QStringLiteral("playing")).toBool();
    m_active = true;
    m_pending = false;
    emit changed();
}

void CastStore::stopCasting() {
    if (!m_active && !m_pending)
        return;
    m_active = false;
    m_pending = false;
    m_playing = false;
    m_position = 0;
    m_duration = 0;
    m_device.clear();
    emit changed();
}

void CastStore::play() {
    if (!m_active)
        return;
    m_playing = true;
    emit changed();
}

void CastStore::pause() {
    if (!m_active)
        return;
    m_playing = false;
    emit changed();
}

void CastStore::seek(double seconds) {
    if (!m_active)
        return;
    if (seconds < 0)
        seconds = 0;
    if (m_duration > 0 && seconds > m_duration)
        seconds = m_duration;
    m_position = seconds;
    emit changed();
}

QVariantMap CastStore::manualReceiver() const {
    QVariantMap device;
    device.insert(QStringLiteral("id"), QStringLiteral("manual-local"));
    device.insert(QStringLiteral("name"), QStringLiteral("Manual receiver"));
    device.insert(QStringLiteral("host"), machineHost());
    device.insert(QStringLiteral("port"), 11470);
    device.insert(QStringLiteral("model"), QStringLiteral("Paste this stream URL on another device"));
    device.insert(QStringLiteral("kind"), QStringLiteral("chromecast"));
    device.insert(QStringLiteral("controlUrl"), QStringLiteral("manual"));
    device.insert(QStringLiteral("manual"), true);
    device.insert(QStringLiteral("audioOnly"), false);
    device.insert(QStringLiteral("serverUrl"), m_localServerUrl);
    return device;
}

QString CastStore::machineHost() const {
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            return address.toString();
    }
    const QString host = QHostInfo::localHostName();
    return host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
}
