#include "HttpRouter.h"

#include <QReadLocker>
#include <QUrl>
#include <QWriteLocker>

#include <utility>

namespace colosseum::server {
namespace {

QString decodeComponent(QByteArray value, bool formStyle = true)
{
    if (formStyle)
        value.replace('+', ' ');
    return QUrl::fromPercentEncoding(value);
}

QString regexEscape(const QString &value)
{
    return QRegularExpression::escape(value);
}

struct CompiledPattern
{
    QRegularExpression expression;
    QStringList parameterNames;
};
QString compileEmbeddedSegment(const QString &segment, QStringList &parameterNames)
{
    QString out;
    qsizetype cursor = 0;
    static const QRegularExpression paramExpression(QStringLiteral(":([A-Za-z_][A-Za-z0-9_]*)"));
    auto match = paramExpression.match(segment, cursor);
    while (match.hasMatch()) {
        out += regexEscape(segment.mid(cursor, match.capturedStart() - cursor));
        parameterNames.push_back(match.captured(1));
        out += QStringLiteral("([^/]+)");
        cursor = match.capturedEnd();
        match = paramExpression.match(segment, cursor);
    }
    out += regexEscape(segment.mid(cursor));
    return out;
}

CompiledPattern compilePattern(const QString &pattern)
{
    CompiledPattern result;
    QString expression = QStringLiteral("^");
    if (pattern == QStringLiteral("/")) {
        result.expression = QRegularExpression(QStringLiteral("^/?$"));
        return result;
    }

    const QStringList segments = pattern.split('/', Qt::SkipEmptyParts);
    static const QRegularExpression wholeParam(
        QStringLiteral("^:([A-Za-z_][A-Za-z0-9_]*)(\\(\\*\\))?(\\?)?$"));
    for (const QString &segment : segments) {
        const auto match = wholeParam.match(segment);
        if (match.hasMatch()) {
            result.parameterNames.push_back(match.captured(1));
            const bool wildcard = !match.captured(2).isEmpty();
            const bool optional = !match.captured(3).isEmpty();
            const QString capture = wildcard ? QStringLiteral("(.*)") : QStringLiteral("([^/]+)");
            if (optional)
                expression += QStringLiteral("(?:/") + capture + QStringLiteral(")?");
            else
                expression += QStringLiteral("/") + capture;
            continue;
        }

        expression += QStringLiteral("/");
        if (segment == QStringLiteral("*"))
            expression += QStringLiteral(".*");
        else
            expression += compileEmbeddedSegment(segment, result.parameterNames);
    }
    expression += QStringLiteral("/?$");
    result.expression = QRegularExpression(expression);
    return result;
}
bool prefixMatches(const QString &path, const QString &prefix)
{
    if (prefix.isEmpty() || prefix == QStringLiteral("/"))
        return true;
    if (path == prefix)
        return true;
    return path.startsWith(prefix + QStringLiteral("/"));
}

} // namespace

QByteArray HttpRequest::header(const QByteArray &name) const
{
    return headers.value(name.trimmed().toLower());
}

QString HttpRequest::parameter(const QString &name) const
{
    return params.value(name);
}

QStringList HttpRequest::queryValues(const QString &name) const
{
    return query.value(name);
}

QStringList HttpRequest::formValues(const QString &name) const
{
    return formBody.value(name);
}
void HttpRouter::use(const QString &prefix, Handler handler)
{
    Route route;
    route.prefix = prefix;
    route.handler = std::move(handler);
    route.middleware = true;
    QWriteLocker locker(&m_lock);
    m_routes.push_back(std::move(route));
}

void HttpRouter::get(const QString &pattern, Handler handler)
{
    addRoute(QStringLiteral("GET"), pattern, std::move(handler));
}

void HttpRouter::post(const QString &pattern, Handler handler)
{
    addRoute(QStringLiteral("POST"), pattern, std::move(handler));
}

void HttpRouter::all(const QString &pattern, Handler handler)
{
    addRoute(QStringLiteral("*"), pattern, std::move(handler));
}

void HttpRouter::addRoute(const QString &method, const QString &pattern, Handler handler)
{
    const CompiledPattern compiled = compilePattern(pattern);
    Route route;
    route.method = method;
    route.expression = compiled.expression;
    route.parameterNames = compiled.parameterNames;
    route.handler = std::move(handler);
    QWriteLocker locker(&m_lock);
    m_routes.push_back(std::move(route));
}

bool HttpRouter::dispatch(HttpRequest &request, HttpResponse response) const
{
    QVector<Route> routes;
    {
        QReadLocker locker(&m_lock);
        routes = m_routes;
    }

    for (const Route &route : routes) {
        if (route.middleware) {
            if (!prefixMatches(request.path, route.prefix))
                continue;
            if (route.handler(request, response) || response.isFinished())
                return true;
            continue;
        }

        const bool methodMatches = route.method == QStringLiteral("*")
            || route.method == request.method
            || (request.method == QStringLiteral("HEAD") && route.method == QStringLiteral("GET"));
        if (!methodMatches)
            continue;

        const QRegularExpressionMatch match = route.expression.match(request.path);
        if (!match.hasMatch())
            continue;

        request.params.clear();
        for (qsizetype i = 0; i < route.parameterNames.size(); ++i) {
            const QString value = match.captured(static_cast<int>(i + 1));
            request.params.insert(route.parameterNames.at(i), decodeComponent(value.toUtf8(), false));
        }

        if (route.handler(request, response) || response.isFinished())
            return true;
    }
    return false;
}

bool applyCorsHeaders(HttpRequest &request, HttpResponse response)
{
    // Stremio Server 4.20.17 module 172 sendCORSHeaders().
    const QByteArray origin = request.header("origin");
    if (request.method == QStringLiteral("OPTIONS") && !origin.isEmpty()) {
        response.setHeader("Access-Control-Allow-Origin", "*");
        response.setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        response.setHeader("Access-Control-Allow-Headers",
                           request.header("access-control-request-headers").isEmpty()
                               ? QByteArray("Range") : request.header("access-control-request-headers"));
        response.setHeader("Access-Control-Max-Age", "1728000");
        response.end();
        return true;
    }
    if (!origin.isEmpty())
        response.setHeader("Access-Control-Allow-Origin", "*");
    return false;
}

void applyDlnaHeaders(HttpResponse response)
{
    // Stremio Server 4.20.17 module 172 sendDLNAHeaders().
    response.setHeader("transferMode.dlna.org", "Streaming");
    response.setHeader(
        "contentFeatures.dlna.org",
        "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=017000 00000000000000000000000000");
}

} // namespace colosseum::server

namespace colosseum::server {

HttpResponse::HttpResponse(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}

void HttpResponse::setHeader(const QByteArray &name, const QByteArray &value)
{
    if (!m_state || m_state->finished.load(std::memory_order_acquire))
        return;
    QMutexLocker locker(&m_state->mutex);
    if (!m_state->started)
        m_state->headers.insert(name.trimmed().toLower(), value);
}

QByteArray HttpResponse::header(const QByteArray &name) const
{
    if (!m_state)
        return {};
    QMutexLocker locker(&m_state->mutex);
    return m_state->headers.value(name.trimmed().toLower());
}

void HttpResponse::removeHeader(const QByteArray &name)
{
    if (!m_state)
        return;
    QMutexLocker locker(&m_state->mutex);
    if (!m_state->started)
        m_state->headers.remove(name.trimmed().toLower());
}

void HttpResponse::writeHead(int status)
{
    setStatusCode(status);
}

void HttpResponse::writeHead(int status, const QHash<QByteArray, QByteArray> &headers)
{
    if (!m_state || m_state->finished.load(std::memory_order_acquire))
        return;
    QMutexLocker locker(&m_state->mutex);
    if (m_state->started)
        return;
    m_state->status = status;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        m_state->headers.insert(it.key().trimmed().toLower(), it.value());
}

void HttpResponse::setStatusCode(int status)
{
    if (!m_state || m_state->finished.load(std::memory_order_acquire))
        return;
    QMutexLocker locker(&m_state->mutex);
    if (!m_state->started)
        m_state->status = status;
}
int HttpResponse::statusCode() const
{
    if (!m_state)
        return 0;
    QMutexLocker locker(&m_state->mutex);
    return m_state->status;
}

bool HttpResponse::write(const QByteArray &data)
{
    if (!m_state || m_state->finished.load(std::memory_order_acquire))
        return false;

    bool sendHead = false;
    bool chunked = false;
    int status = 200;
    QHash<QByteArray, QByteArray> headers;
    std::function<void(int, const QHash<QByteArray, QByteArray> &, bool)> onHead;
    std::function<void(const QByteArray &, bool)> onData;
    {
        QMutexLocker locker(&m_state->mutex);
        if (!m_state->started) {
            m_state->started = true;
            m_state->streaming = true;
            m_state->chunked = !m_state->headers.contains("content-length");
            sendHead = true;
        }
        chunked = m_state->chunked;
        status = m_state->status;
        headers = m_state->headers;
        onHead = m_state->onHead;
        onData = m_state->onData;
    }
    if (sendHead && onHead)
        onHead(status, headers, chunked);
    if (onData && !data.isEmpty())
        onData(data, chunked);
    return true;
}

void HttpResponse::end(const QByteArray &data)
{
    if (!m_state || m_state->finished.exchange(true, std::memory_order_acq_rel))
        return;

    bool sendHead = false;
    bool chunked = false;
    int status = 200;
    QHash<QByteArray, QByteArray> headers;
    std::function<void(int, const QHash<QByteArray, QByteArray> &, bool)> onHead;
    std::function<void(const QByteArray &, bool)> onData;
    std::function<void(bool)> onEnd;
    {
        QMutexLocker locker(&m_state->mutex);
        if (!m_state->started) {
            m_state->started = true;
            sendHead = true;
            m_state->chunked = false;
            if (!data.isEmpty() && !m_state->headers.contains("content-length"))
                m_state->headers.insert("content-length", QByteArray::number(data.size()));
        }
        chunked = m_state->chunked;
        status = m_state->status;
        headers = m_state->headers;
        onHead = m_state->onHead;
        onData = m_state->onData;
        onEnd = m_state->onEnd;
    }

    if (sendHead && onHead)
        onHead(status, headers, chunked);
    if (onData && !data.isEmpty())
        onData(data, chunked);
    if (onEnd)
        onEnd(chunked);
}

bool HttpResponse::isFinished() const
{
    return !m_state || m_state->finished.load(std::memory_order_acquire);
}

} // namespace colosseum::server
