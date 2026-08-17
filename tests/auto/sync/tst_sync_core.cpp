// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountFixtureTransport.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

namespace {
class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name)) {}

    ~ScopedEnvironmentVariable() {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

AccountTransportReply replyWithSequence(int sequence) {
    AccountTransportReply reply;
    reply.statusCode = 200;
    reply.body.insert(QStringLiteral("sequence"), sequence);
    return reply;
}
}

class tst_sync_core : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void fixtureRepliesRemainFIFO();
    void missingRouteIsDeterministic();
    void offlineRetryPreservesQueuedReply();
    void missingRouteNeverReflectsRequestSecrets();
};

void tst_sync_core::initTestCase() {
    qRegisterMetaType<AccountTransportReply>();
}

void tst_sync_core::fixtureRepliesRemainFIFO() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("sync-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);

    transport->enqueueReply(QByteArrayLiteral("POST"),
                            QStringLiteral("/v1/sync/push"),
                            replyWithSequence(11));
    transport->enqueueReply(QByteArrayLiteral("POST"),
                            QStringLiteral("/v1/sync/push"),
                            replyWithSequence(12));

    QSignalSpy spy(transport.get(), &AccountTransport::finished);

    AccountTransportRequest request;
    request.method = QByteArrayLiteral("POST");
    request.path = QStringLiteral("/v1/sync/push");

    transport->send(1, request);
    transport->send(2, request);

    QCOMPARE(spy.count(), 2);

    QList<QVariant> first = spy.takeFirst();
    QList<QVariant> second = spy.takeFirst();

    QCOMPARE(first.at(0).toULongLong(), quint64(1));
    QCOMPARE(first.at(1).value<AccountTransportReply>()
                 .body.value(QStringLiteral("sequence")).toInt(),
             11);
    QCOMPARE(second.at(0).toULongLong(), quint64(2));
    QCOMPARE(second.at(1).value<AccountTransportReply>()
                 .body.value(QStringLiteral("sequence")).toInt(),
             12);
}

void tst_sync_core::missingRouteIsDeterministic() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("sync-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);
    QSignalSpy spy(transport.get(), &AccountTransport::finished);

    AccountTransportRequest request;
    request.method = QByteArrayLiteral("GET");
    request.path = QStringLiteral("/v1/sync/pull?after=0");
    transport->send(3, request);

    QCOMPARE(spy.count(), 1);
    const AccountTransportReply reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    QCOMPARE(reply.statusCode, 404);
    QCOMPARE(reply.errorCode, QStringLiteral("fixture_route_missing"));
    QVERIFY(!reply.networkError);
}

void tst_sync_core::offlineRetryPreservesQueuedReply() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("sync-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);
    transport->enqueueReply(QByteArrayLiteral("GET"),
                            QStringLiteral("/v1/sync/pull?after=0"),
                            replyWithSequence(20));

    QSignalSpy spy(transport.get(), &AccountTransport::finished);
    AccountTransportRequest request;
    request.method = QByteArrayLiteral("GET");
    request.path = QStringLiteral("/v1/sync/pull?after=0");

    transport->setOnline(false);
    transport->send(4, request);

    QCOMPARE(spy.count(), 1);
    AccountTransportReply reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    QVERIFY(reply.networkError);
    QCOMPARE(transport->pendingReplyCount(request.method, request.path), 1);

    transport->setOnline(true);
    transport->send(5, request);

    QCOMPARE(spy.count(), 1);
    reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    QCOMPARE(reply.statusCode, 200);
    QCOMPARE(reply.body.value(QStringLiteral("sequence")).toInt(), 20);
    QCOMPARE(transport->pendingReplyCount(request.method, request.path), 0);
}

void tst_sync_core::missingRouteNeverReflectsRequestSecrets() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("sync-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);
    QSignalSpy spy(transport.get(), &AccountTransport::finished);

    const QString passwordSentinel = QStringLiteral("password-sentinel-7f84");
    const QByteArray tokenSentinel("token-sentinel-d813");

    AccountTransportRequest request;
    request.method = QByteArrayLiteral("POST");
    request.path = QStringLiteral("/v1/not-registered");
    request.body.insert(QStringLiteral("password"), passwordSentinel);
    request.bearerToken = tokenSentinel;
    transport->send(6, request);

    QCOMPARE(spy.count(), 1);
    const AccountTransportReply reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    const QByteArray serialized = reply.errorCode.toUtf8()
        + reply.errorMessage.toUtf8()
        + QJsonDocument(reply.body).toJson(QJsonDocument::Compact);

    QVERIFY(!serialized.contains(passwordSentinel.toUtf8()));
    QVERIFY(!serialized.contains(tokenSentinel));
}

QTEST_GUILESS_MAIN(tst_sync_core)

#include "tst_sync_core.moc"
