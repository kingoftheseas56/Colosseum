#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountTransport.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class AccountFixtureTransport final : public AccountTransport {
    Q_OBJECT

public:
    static std::unique_ptr<AccountFixtureTransport> create();
    static bool testModeAllowed();

    void setOnline(bool online);
    bool online() const;

    void enqueueReply(const QByteArray &method,
                      const QString &path,
                      const AccountTransportReply &reply);
    int pendingReplyCount(const QByteArray &method, const QString &path) const;

    void send(quint64 requestId, const AccountTransportRequest &request) override;

private:
    AccountFixtureTransport();

    static QString routeKey(const QByteArray &method, const QString &path);

    bool m_online = true;
    QHash<QString, QList<AccountTransportReply>> m_replies;
};
