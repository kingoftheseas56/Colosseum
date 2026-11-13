#pragma once
#include <QObject>
#include <QJsonValue>
#include <QMap>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>
class HomeServerTransport : public QObject {
    Q_OBJECT
public:
    struct Response {
        int status = 0;
        QJsonValue body;
        QString error;
        bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
        bool expired() const { return status == 401; }
    };
    using Callback = std::function<void(Response)>;
    explicit HomeServerTransport(QObject *parent = nullptr);
    ~HomeServerTransport() override;
    void send(QUrl url, QByteArray method, QMap<QByteArray,QByteArray> headers,
              QByteArray body, Callback callback, int timeoutMs = 15000);
    void cancelAll();
private:
    QNetworkAccessManager m_manager;
};
