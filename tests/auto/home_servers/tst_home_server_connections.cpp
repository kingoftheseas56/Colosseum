#include "home_servers/HomeServerConnectionStore.h"

#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <algorithm>
#include <memory>
#include <utility>

using namespace Colosseum::HomeServers;

namespace {
class MemorySecrets final : public AccountSecretBackend {
public:
    bool ready = true;
    bool rejectNextWrite = false;
    bool corruptNextWrite = false;
    bool rejectRemove = false;
    QMap<QString, QByteArray> blobs;

    bool isAvailable() const override { return ready; }
    bool write(const QString &target, const QByteArray &blob) override
    {
        if (!ready || blob.isEmpty() || blob.size() > 2560)
            return false;
        if (std::exchange(rejectNextWrite, false))
            return false;
        blobs[target] = std::exchange(corruptNextWrite, false)
            ? QByteArrayLiteral("{invalid") : blob;
        return true;
    }
    std::optional<QByteArray> read(const QString &target) const override
    {
        auto it = blobs.constFind(target);
        return it == blobs.cend() ? std::nullopt
                                  : std::optional<QByteArray>(*it);
    }
    bool remove(const QString &target) override
    {
        if (rejectRemove)
            return false;
        blobs.remove(target);
        return true;
    }
    QStringList targets(const QString &prefix) const override
    {
        QStringList out;
        for (auto it = blobs.cbegin(); it != blobs.cend(); ++it) {
            if (it.key().startsWith(prefix))
                out.append(it.key());
        }
        return out;
    }
};

ServerSession session(const QString &user, const QByteArray &token)
{
    return {user, QStringLiteral("server-A"), token};
}

QUrl base(const QString &path = {})
{
    return QUrl(QStringLiteral("https://server.example") + path);
}
}

class tst_home_server_connections final : public QObject {
    Q_OBJECT

private slots:
    void differentProfilesAndServersStayIsolated();
    void disabledConnectionsNeverReturnSession();
    void reconnectRequiresSameServerAndUser();
    void partialWriteAndOversizeFailClosed();
    void corruptEntriesAndInvalidInputAreIgnored();
    void candidatePathsNeverChangeAuthority();
    void testProfileNamespacesCannotSeeRealProfile();
};

void tst_home_server_connections::differentProfilesAndServersStayIsolated()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionStore alice(QStringLiteral("profile-A"), secrets);
    HomeServerConnectionStore bob(QStringLiteral("profile-B"), secrets);
    QVERIFY(alice.available());
    QVERIFY(bob.available());
    const auto id1 = alice.add(ServerKind::Jellyfin, QStringLiteral("Living room"),
                               base(QStringLiteral("/jellyfin")),
                               session(QStringLiteral("user-A"), "alice-token"));
    const auto id2 = alice.add(ServerKind::Emby, QStringLiteral("Bedroom"),
                               base(QStringLiteral("/emby")),
                               session(QStringLiteral("user-B"), "bedroom-token"));
    const auto id3 = bob.add(ServerKind::Jellyfin, QStringLiteral("Living room"),
                             base(QStringLiteral("/jellyfin")),
                             session(QStringLiteral("user-A"), "bob-token"));
    QVERIFY(id1 && id2 && id3);
    QVERIFY(*id1 != *id2 && *id1 != *id3);
    QCOMPARE(alice.list().size(), 2);
    QCOMPARE(bob.list().size(), 1);
    QCOMPARE(alice.activeSession(*id1)->accessToken, QByteArray("alice-token"));
    QCOMPARE(bob.activeSession(*id3)->accessToken, QByteArray("bob-token"));
    QVERIFY(!alice.activeSession(*id3).has_value());
    QVERIFY(!bob.activeSession(*id1).has_value());
    QVERIFY(!alice.add(ServerKind::Jellyfin, QStringLiteral("Duplicate"),
                       base(QStringLiteral("/jellyfin")),
                       session(QStringLiteral("user-A"), "new-token")));
    QCOMPARE(secrets->blobs.size(), 3);
    QVERIFY(alice.remove(*id1));
    QCOMPARE(alice.list().size(), 1);
    QCOMPARE(bob.activeSession(*id3)->accessToken, QByteArray("bob-token"));
    for (auto it = secrets->blobs.cbegin(); it != secrets->blobs.cend(); ++it) {
        QVERIFY(!it.key().contains(QStringLiteral("profile-A")));
        QVERIFY(!it.key().contains(QStringLiteral("profile-B")));
        QVERIFY(!it.key().contains(QStringLiteral("alice-token")));
        QVERIFY(!it.value().contains("password"));
    }
}

void tst_home_server_connections::disabledConnectionsNeverReturnSession()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionStore store(QStringLiteral("local"), secrets);
    const auto id = store.add(ServerKind::Jellyfin, QStringLiteral("Jellyfin"),
                              base(), session(QStringLiteral("user-A"), "token-A"));
    QVERIFY(id);
    QVERIFY(store.activeSession(*id));
    QVERIFY(store.setEnabled(*id, false));
    QCOMPARE(store.list().first().enabled, false);
    QVERIFY(!store.activeSession(*id));
    HomeServerConnectionStore restored(QStringLiteral("local"), secrets);
    QCOMPARE(restored.list().first().enabled, false);
    QVERIFY(!restored.activeSession(*id));
    QVERIFY(restored.setEnabled(*id, true));
    QCOMPARE(restored.activeSession(*id)->accessToken, QByteArray("token-A"));
    QVERIFY(restored.remove(*id));
    QVERIFY(restored.list().isEmpty());
    QVERIFY(!store.activeSession(*id));
    QVERIFY(restored.remove(*id));
}

void tst_home_server_connections::reconnectRequiresSameServerAndUser()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionStore store(QStringLiteral("local"), secrets);
    const auto id = store.add(ServerKind::Emby, QStringLiteral("Emby"),
                              base(QStringLiteral("/emby")),
                              session(QStringLiteral("user-A"), "old-token"));
    QVERIFY(id);
    QVERIFY(!store.replaceSession(*id, session(QStringLiteral("different-user"), "bad")));
    auto alien = session(QStringLiteral("user-A"), "alien");
    alien.serverId = QStringLiteral("other-server");
    QVERIFY(!store.replaceSession(*id, alien));
    QCOMPARE(store.activeSession(*id)->accessToken, QByteArray("old-token"));
    QVERIFY(store.replaceSession(*id, session(QStringLiteral("user-A"), "new-token")));
    QCOMPARE(store.activeSession(*id)->accessToken, QByteArray("new-token"));
    QCOMPARE(store.list().size(), 1);
}

void tst_home_server_connections::partialWriteAndOversizeFailClosed()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionStore store(QStringLiteral("local"), secrets);
    const auto id = store.add(ServerKind::Jellyfin, QStringLiteral("Cinema"),
                              base(), session(QStringLiteral("user-A"), "original"));
    QVERIFY(id);
    secrets->rejectNextWrite = true;
    QVERIFY(!store.setEnabled(*id, false));
    QVERIFY(store.list().first().enabled);
    secrets->corruptNextWrite = true;
    QVERIFY(!store.replaceSession(*id, session(QStringLiteral("user-A"), "updated")));
    QCOMPARE(store.activeSession(*id)->accessToken, QByteArray("original"));
    QVERIFY(!store.add(ServerKind::Emby, QStringLiteral("Too large"),
                       base(QStringLiteral("/emby")),
                       session(QStringLiteral("user-A"), QByteArray(3000, 'a'))));
    QCOMPARE(store.list().size(), 1);
    secrets->rejectRemove = true;
    QVERIFY(!store.remove(*id));
    QVERIFY(store.activeSession(*id));
}

void tst_home_server_connections::corruptEntriesAndInvalidInputAreIgnored()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionStore store(QStringLiteral("local"), secrets);
    QVERIFY(!store.add(ServerKind::Jellyfin, QStringLiteral("Invalid"),
                       QUrl(QStringLiteral("https://name:password@server.example")),
                       session(QStringLiteral("user-A"), "good")));
    QVERIFY(!store.add(ServerKind::Jellyfin, QStringLiteral("Invalid"),
                       QUrl(QStringLiteral("https://server.example?api_key=secret")),
                       session(QStringLiteral("user-A"), "good")));
    QVERIFY(!store.add(ServerKind::Jellyfin, QStringLiteral("bad\nname"),
                       base(), session(QStringLiteral("user-A"), "good")));
    QVERIFY(!store.add(ServerKind::Emby, QStringLiteral("Bad session"),
                       base(), session(QStringLiteral("user-A"), "bad\r\nToken:injected")));
    QVERIFY(!store.add(ServerKind::Emby, QStringLiteral("Bad session"),
                       base(), session(QStringLiteral("user-A"), "")));
    HomeServerConnectionStore invalid(QStringLiteral("../invalid"), secrets);
    QVERIFY(!invalid.available());
    QVERIFY(invalid.list().isEmpty());
    QVERIFY(!invalid.add(ServerKind::Jellyfin, QStringLiteral("No"),
                         base(), session(QStringLiteral("user-A"), "token")));
    const auto id = store.add(ServerKind::Jellyfin, QStringLiteral("Valid"),
                              base(), session(QStringLiteral("user-A"), "token"));
    QVERIFY(id);
    const QString key = secrets->blobs.firstKey();
    const QByteArray valid = secrets->blobs.value(key);
    QVERIFY(valid.contains("user-A"));
    auto spoofed = QJsonDocument::fromJson(valid).object();
    spoofed.insert(QStringLiteral("profile"), QStringLiteral("other-profile"));
    secrets->blobs[key] = QJsonDocument(spoofed).toJson(QJsonDocument::Compact);
    QVERIFY(store.list().isEmpty());
    spoofed = QJsonDocument::fromJson(valid).object();
    spoofed.insert(QStringLiteral("connection"), QStringLiteral("other-id"));
    secrets->blobs[key] = QJsonDocument(spoofed).toJson(QJsonDocument::Compact);
    QVERIFY(store.list().isEmpty());
    secrets->blobs[key] = QByteArrayLiteral("{invalid");
    QVERIFY(store.list().isEmpty());
    QVERIFY(!store.activeSession(*id));
    QVERIFY(!store.setEnabled(*id, false));
    QVERIFY(!store.replaceSession(*id, session(QStringLiteral("user-A"), "new")));
}

void tst_home_server_connections::candidatePathsNeverChangeAuthority()
{
    auto urls = HomeServerConnectionStore::candidateApiBases(
        ServerKind::Emby, base(QStringLiteral("/proxy")));
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls[0].toString(), QStringLiteral("https://server.example/proxy"));
    QCOMPARE(urls[1].toString(), QStringLiteral("https://server.example/proxy/emby"));
    urls = HomeServerConnectionStore::candidateApiBases(
        ServerKind::Emby, base(QStringLiteral("/emby/")));
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0].toString(), QStringLiteral("https://server.example/emby"));
    urls = HomeServerConnectionStore::candidateApiBases(
        ServerKind::Jellyfin, base(QStringLiteral("/jellyfin")));
    QCOMPARE(urls.size(), 1);
    urls = HomeServerConnectionStore::candidateApiBases(
        ServerKind::Emby, QUrl(QStringLiteral("ftp://untrusted.example")));
    QVERIFY(urls.isEmpty());
    urls = HomeServerConnectionStore::candidateApiBases(
        ServerKind::Emby, QUrl(QStringLiteral("http://server.example/?key=secret")));
    QVERIFY(urls.isEmpty());
}

void tst_home_server_connections::testProfileNamespacesCannotSeeRealProfile()
{
    auto secrets = std::make_shared<MemorySecrets>();
    const bool wasSet = qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG");
    const QByteArray old = qgetenv("COLOSSEUM_APPDATA_TAG");
    qunsetenv("COLOSSEUM_APPDATA_TAG");
    HomeServerConnectionStore normal(QStringLiteral("local"), secrets);
    const auto normalId = normal.add(ServerKind::Jellyfin, QStringLiteral("Normal"),
                                     base(), session(QStringLiteral("user-A"), "normal-token"));
    QVERIFY(normalId);
    qputenv("COLOSSEUM_APPDATA_TAG", "isolated-test-profile");
    HomeServerConnectionStore tagged(QStringLiteral("local"), secrets);
    QVERIFY(tagged.list().isEmpty());
    const auto taggedId = tagged.add(ServerKind::Jellyfin, QStringLiteral("Sandbox"),
                                     base(), session(QStringLiteral("user-A"), "sandbox-token"));
    QVERIFY(taggedId);
    QCOMPARE(tagged.list().size(), 1);
    QVERIFY(!tagged.activeSession(*normalId));
    if (wasSet)
        qputenv("COLOSSEUM_APPDATA_TAG", old);
    else
        qunsetenv("COLOSSEUM_APPDATA_TAG");
    QCOMPARE(normal.list().size(), 1);
    QVERIFY(!normal.activeSession(*taggedId));
}

QTEST_GUILESS_MAIN(tst_home_server_connections)
#include "tst_home_server_connections.moc"
