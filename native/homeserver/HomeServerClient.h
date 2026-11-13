#pragma once
#include "HomeServerTransport.h"
#include "HomeServerDeviceIdentity.h"
class HomeServerClient : public QObject {
    Q_OBJECT
public:
    using Response = HomeServerTransport::Response;
    using Callback = HomeServerTransport::Callback;
    explicit HomeServerClient(QObject *parent = nullptr, QString identityDirectory = {},
                              QUrl plexOrigin = QUrl(QStringLiteral("https://plex.tv")));
    static QUrl normalizeAddress(QString address);
    static QUrl relativeUrl(QUrl origin, QString path);
    QMap<QByteArray,QByteArray> headers(QString provider, QString token = {}) const;
    bool isReady() const { return m_identity.isReady(); }
    QString identityError() const { return m_identity.error(); }
    QString installationId() const { return m_identity.installationId(); }
    void cancelAll() { m_transport.cancelAll(); }
    void request(QString provider, QUrl origin, QString path, QByteArray method,
                 QString token, QJsonValue body, Callback callback);
    void authenticate(QString provider, QString address, QString user, QString password,
                      bool allowHttp, Callback callback);
    void createPlexPin(Callback callback);
    void pollPlexPin(qint64 id, QString code, Callback callback);
    void discoverPlexServers(QString token, Callback callback);
    void verifyPlexServer(QUrl origin, QString token, bool allowHttp, Callback callback);
private:
    void fail(Callback callback, QString error);
    HomeServerTransport m_transport;
    HomeServerDeviceIdentity m_identity;
    QUrl m_plexOrigin;
    QUrl m_plexResourcesOrigin;
};
