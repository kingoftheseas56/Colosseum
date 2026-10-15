// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountFixtureTransport.h"

#include <QByteArray>

namespace {
AccountTransportReply offlineReply() {
    AccountTransportReply reply;
    reply.statusCode = 0;
    reply.errorCode = QStringLiteral("offline");
    reply.errorMessage = QStringLiteral("The fixture transport is offline.");
    reply.networkError = true;
    return reply;
}

AccountTransportReply missingRouteReply() {
    AccountTransportReply reply;
    reply.statusCode = 404;
    reply.errorCode = QStringLiteral("fixture_route_missing");
    reply.errorMessage = QStringLiteral("No deterministic fixture reply is registered for this route.");
    return reply;
}
}

std::unique_ptr<AccountFixtureTransport> AccountFixtureTransport::create() {
    if (!testModeAllowed())
        return nullptr;
    return std::unique_ptr<AccountFixtureTransport>(new AccountFixtureTransport());
}

bool AccountFixtureTransport::testModeAllowed() {
    const QByteArray tag = qgetenv("COLOSSEUM_APPDATA_TAG").trimmed();
    return !tag.isEmpty();
}

void AccountFixtureTransport::setOnline(bool online) {
    m_online = online;
}

bool AccountFixtureTransport::online() const {
    return m_online;
}

void AccountFixtureTransport::enqueueReply(const QByteArray &method,
                                           const QString &path,
                                           const AccountTransportReply &reply) {
    m_replies[routeKey(method, path)].append(reply);
}

int AccountFixtureTransport::pendingReplyCount(const QByteArray &method, const QString &path) const {
    return m_replies.value(routeKey(method, path)).size();
}

void AccountFixtureTransport::send(quint64 requestId, const AccountTransportRequest &request) {
    if (!m_online) {
        emit finished(requestId, offlineReply());
        return;
    }

    const QString key = routeKey(request.method, request.path);
    auto it = m_replies.find(key);
    if (it == m_replies.end() || it->isEmpty()) {
        emit finished(requestId, missingRouteReply());
        return;
    }

    const AccountTransportReply reply = it->takeFirst();
    if (it->isEmpty())
        m_replies.erase(it);
    emit finished(requestId, reply);
}

AccountFixtureTransport::AccountFixtureTransport()
    : AccountTransport(nullptr) {}

QString AccountFixtureTransport::routeKey(const QByteArray &method, const QString &path) {
    return QString::fromLatin1(method.trimmed().toUpper())
        + QLatin1Char(' ')
        + path.trimmed();
}
