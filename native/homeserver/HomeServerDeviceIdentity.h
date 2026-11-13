#pragma once
#include <QString>
class HomeServerDeviceIdentity {
public:
    explicit HomeServerDeviceIdentity(QString directory = {});
    bool isReady() const { return !m_id.isEmpty(); }
    QString installationId() const { return m_id; }
    QString error() const { return m_error; }
    static QString defaultDirectory();
    static QString deviceName();
private:
    QString m_id;
    QString m_error;
};
