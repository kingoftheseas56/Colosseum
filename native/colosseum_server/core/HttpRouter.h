#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace colosseum::server {

class HttpConnection;

class CancellationToken final
{
public:
    bool isCancelled() const noexcept
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

private:
    friend class HttpConnection;
    void cancel() noexcept
    {
        m_cancelled.store(true, std::memory_order_release);
    }

    std::atomic_bool m_cancelled{false};
};

struct HttpRequest
{
    QString method;
    QByteArray rawTarget;
    QByteArray rawPath;
    QString path;
    QHash<QByteArray, QByteArray> headers;
    QHash<QString, QStringList> query;
    QHash<QString, QString> params;
    QByteArray body;
    QJsonDocument jsonBody;
    bool hasJsonBody = false;
    QHash<QString, QStringList> formBody;
    std::shared_ptr<CancellationToken> cancellation;

    QByteArray header(const QByteArray &name) const;
    QString parameter(const QString &name) const;
    QStringList queryValues(const QString &name) const;
    QStringList formValues(const QString &name) const;
};
class HttpResponse
{
public:
    HttpResponse() = default;

    void setHeader(const QByteArray &name, const QByteArray &value);
    QByteArray header(const QByteArray &name) const;
    void removeHeader(const QByteArray &name);
    void writeHead(int status);
    void writeHead(int status, const QHash<QByteArray, QByteArray> &headers);
    void setStatusCode(int status);
    int statusCode() const;
    bool write(const QByteArray &data);
    void end(const QByteArray &data = {});
    bool isFinished() const;

private:
    struct State
    {
        QMutex mutex;
        int status = 200;
        QHash<QByteArray, QByteArray> headers;
        bool started = false;
        bool streaming = false;
        bool chunked = false;
        bool suppressBody = false;
        std::atomic_bool finished{false};
        std::function<void(int, const QHash<QByteArray, QByteArray> &, bool)> onHead;
        std::function<void(const QByteArray &, bool)> onData;
        std::function<void(bool)> onEnd;
    };

    explicit HttpResponse(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;

    friend class HttpConnection;
};

class HttpRouter
{
public:
    using Handler = std::function<bool(HttpRequest &, HttpResponse)>;

    void use(const QString &prefix, Handler handler);
    void get(const QString &pattern, Handler handler);
    void post(const QString &pattern, Handler handler);
    void all(const QString &pattern, Handler handler);
    bool dispatch(HttpRequest &request, HttpResponse response) const;

private:
    struct Route
    {
        QString method;
        QString prefix;
        QRegularExpression expression;
        QStringList parameterNames;
        Handler handler;
        bool middleware = false;
    };

    void addRoute(const QString &method, const QString &pattern, Handler handler);

    mutable QReadWriteLock m_lock;
    QVector<Route> m_routes;
};

bool applyCorsHeaders(HttpRequest &request, HttpResponse response);
void applyDlnaHeaders(HttpResponse response);

} // namespace colosseum::server
