// tst_extensions_first_run — Living Guide Task 11 (product prerequisite). Proves the fresh-profile
// acquisition-source consent contract, the Stremio model the locked product rule requires:
//   - core catalogues are seeded ENABLED;
//   - every removable acquisition/playback WELL — a non-core extension that provides `stream` sources —
//     is seeded INSTALLED BUT DISABLED until the user turns it on;
//   - non-fetching capabilities (catalog/meta/subtitles/universe) are never disabled by the gate.
// The decision is derived from manifest resources + `core`, never by string-matching a name. GUILESS;
// isolated AppData via QStandardPaths test mode + a wiped index per test.

#include "engine/ExtensionsStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

#include <functional>
#include <type_traits>

template <typename Store, typename = void>
struct CanApplyTheatreRows : std::false_type {};

template <typename Store>
struct CanApplyTheatreRows<
    Store,
    std::void_t<decltype(std::declval<Store &>().applyTheatreRows(
        std::declval<QVariantList>(),
        std::function<void(bool, const QString &)>{}))>> : std::true_type {};

template <typename Store>
bool applyTheatreRows(
    Store &store,
    const QVariantList &rows,
    std::function<void(bool, const QString &)> completion)
{
    if constexpr (CanApplyTheatreRows<Store>::value)
        return store.applyTheatreRows(rows, std::move(completion));
    Q_UNUSED(store);
    Q_UNUSED(rows);
    Q_UNUSED(completion);
    return false;
}

class ManifestFixture final : public QObject
{
public:
    explicit ManifestFixture(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    socket->readAll();
                    if (socket->property("fixtureRequestHandled").toBool())
                        return;
                    socket->setProperty("fixtureRequestHandled", true);
                    if (m_holdResponses) {
                        m_heldResponses.append(socket);
                        return;
                    }
                    respond(socket);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    QString configuredUrl(const QString &pathAndQuery) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2")
            .arg(m_server.serverPort())
            .arg(pathAndQuery);
    }

    void holdResponses(bool hold) { m_holdResponses = hold; }
    int heldRequestCount() const { return m_heldResponses.size(); }
    void releaseHeldResponses()
    {
        m_holdResponses = false;
        const QList<QPointer<QTcpSocket>> held = m_heldResponses;
        m_heldResponses.clear();
        for (const QPointer<QTcpSocket> &socket : held)
            respond(socket.data());
    }

private:
    static void respond(QTcpSocket *socket)
    {
        if (!socket)
            return;
        const QByteArray body = QByteArrayLiteral(
            "{\"id\":\"fixture.same-manifest\",\"name\":\"Fixture addon\","
            "\"resources\":[\"catalog\"],\"types\":[\"movie\"]}");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                         "Content-Type: application/json\r\n"
                                         "Content-Length: ")
                      + QByteArray::number(body.size())
                      + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                      + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    bool m_holdResponses = false;
    QList<QPointer<QTcpSocket>> m_heldResponses;
};

class tst_extensions_first_run : public QObject
{
    Q_OBJECT

    static void wipeIndex()
    {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(base + QStringLiteral("/extensions/installed.json"));
    }
    static bool providesStream(const QVariantMap& e)
    {
        return e.value(QStringLiteral("manifest")).toMap()
                .value(QStringLiteral("resources")).toStringList()
                .contains(QStringLiteral("stream"));
    }
    static bool isRemovableWell(const QVariantMap& e)
    {
        return !e.value(QStringLiteral("core")).toBool() && providesStream(e);
    }
    static QVariantMap findById(const QVariantList& items, const QString& id)
    {
        for (const QVariant& v : items)
            if (v.toMap().value(QStringLiteral("id")).toString() == id)
                return v.toMap();
        return {};
    }
    static QVariantMap findByTransportUrl(const QVariantList& items, const QString& transportUrl)
    {
        for (const QVariant& value : items) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("transportUrl")).toString() == transportUrl)
                return row;
        }
        return {};
    }
    static int indexOfTransportUrl(const QVariantList& items, const QString& transportUrl)
    {
        for (int index = 0; index < items.size(); ++index) {
            if (items.at(index).toMap().value(QStringLiteral("transportUrl")).toString()
                == transportUrl) {
                return index;
            }
        }
        return -1;
    }
    static QVariantMap theatreFixture(const QString &transportUrl)
    {
        return QVariantMap{
            {QStringLiteral("id"), QStringLiteral("fixture.profile.theatre")},
            {QStringLiteral("transportUrl"), transportUrl},
            {QStringLiteral("installedAt"), qint64(1)},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("core"), false},
            {QStringLiteral("manifest"), QVariantMap{
                {QStringLiteral("id"), QStringLiteral("fixture.profile.theatre")},
                {QStringLiteral("name"), QStringLiteral("Profile Theatre fixture")},
                {QStringLiteral("types"), QStringList{QStringLiteral("movie")}},
                {QStringLiteral("resources"), QStringList{QStringLiteral("catalog")}}}}};
    }
    static QVariantMap theatreFixture(const QString &transportUrl,
                                      const QString &id)
    {
        QVariantMap row = theatreFixture(transportUrl);
        row.insert(QStringLiteral("id"), id);
        QVariantMap manifest = row.value(QStringLiteral("manifest")).toMap();
        manifest.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("manifest"), manifest);
        return row;
    }
    static bool activateProfile(ExtensionsStore *store,
                                const QString &profileId,
                                const QString &path)
    {
        bool activated = false;
        return QMetaObject::invokeMethod(store, "activateProfile",
                                         Q_RETURN_ARG(bool, activated),
                                         Q_ARG(QString, profileId),
                                         Q_ARG(QString, path))
            && activated;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumFirstRunTest"));
        QCoreApplication::setApplicationName(QStringLiteral("colosseum_first_run_test"));
        QStandardPaths::setTestModeEnabled(true);
    }
    void init() { wipeIndex(); }
    void cleanup() { wipeIndex(); }

    // A fresh profile seeds core catalogues enabled, but every removable acquisition/playback well disabled.
    void fresh_profile_requires_consent_for_removable_wells()
    {
        ExtensionsStore store(nullptr);
        const QVariantList items = store.installed();
        QVERIFY(!items.isEmpty());

        int wells = 0, coreCatalogues = 0;
        for (const QVariant& v : items) {
            const QVariantMap e = v.toMap();
            const QString id = e.value(QStringLiteral("id")).toString();
            if (e.value(QStringLiteral("core")).toBool()) {
                ++coreCatalogues;
                QVERIFY2(e.value(QStringLiteral("enabled")).toBool(),
                         qPrintable(QStringLiteral("core catalogue seeded disabled: ") + id));
            }
            if (isRemovableWell(e)) {
                ++wells;
                QVERIFY2(!e.value(QStringLiteral("enabled")).toBool(),
                         qPrintable(QStringLiteral("removable acquisition/playback well seeded ENABLED (no consent): ") + id));
            }
        }
        QVERIFY2(coreCatalogues >= 1, "expected at least one core catalogue in the seed");
        QVERIFY2(wells >= 1, "expected at least one removable acquisition/playback well in the seed");
    }

    // Derivation is by resources + core, NOT string-matching: non-core capabilities that do not provide
    // streams (catalog / meta / subtitles / universe) must stay enabled.
    void non_fetching_capabilities_stay_enabled()
    {
        ExtensionsStore store(nullptr);
        for (const QVariant& v : store.installed()) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("core")).toBool()) continue;
            if (providesStream(e)) continue;   // a well — covered above
            const QString id = e.value(QStringLiteral("id")).toString();
            QVERIFY2(e.value(QStringLiteral("enabled")).toBool(),
                     qPrintable(QStringLiteral("non-well capability wrongly disabled by the consent gate: ") + id));
        }
    }

    // Consent is real: enabling a seeded-disabled well is honored.
    void enabling_a_well_is_honored()
    {
        ExtensionsStore store(nullptr);
        QString wellId;
        for (const QVariant& v : store.installed()) {
            const QVariantMap e = v.toMap();
            if (isRemovableWell(e) && !e.value(QStringLiteral("enabled")).toBool()) {
                wellId = e.value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY2(!wellId.isEmpty(), "expected a seeded-disabled removable well");
        store.setEnabled(wellId, true);
        QVERIFY2(findById(store.installed(), wellId).value(QStringLiteral("enabled")).toBool(),
                 "setEnabled(well, true) was not honored");
    }

    // Existing profile (Preflight #6 hardening): a user's explicit enable of a seeded-disabled well
    // SURVIVES store reconstruction — the new fresh-install default never rewrites a persisted choice
    // (no defaults-version bump, and an existing profile is not re-seeded).
    void enabled_choice_survives_store_reconstruction()
    {
        QString wellId;
        {
            ExtensionsStore store(nullptr);            // fresh profile (init() wiped the index)
            for (const QVariant& v : store.installed()) {
                const QVariantMap e = v.toMap();
                if (isRemovableWell(e) && !e.value(QStringLiteral("enabled")).toBool()) {
                    wellId = e.value(QStringLiteral("id")).toString();
                    break;
                }
            }
            QVERIFY(!wellId.isEmpty());
            store.setEnabled(wellId, true);            // persists the choice to installed.json
        }
        ExtensionsStore reopened(nullptr);             // reconstruct WITHOUT wiping — an existing profile
        QVERIFY2(findById(reopened.installed(), wellId).value(QStringLiteral("enabled")).toBool(),
                 "a persisted enable choice was reset on store reconstruction");
    }

    // R1 (release gate for 1.1.1, 2026-08-21, "nyaa ships dark"): the manga Nyaa well
    // rides the SAME generic removable-well gate proven above (fresh_profile_requires_
    // consent_for_removable_wells) -- this case names it explicitly so the release
    // gate has its own direct, traceable assertion rather than relying only on the
    // generic "at least one well" count. Mirrors Torrentio's own treatment exactly:
    // both are non-core, both provide "stream", both seed enabled:false.
    void manga_nyaa_well_seeded_disabled()
    {
        ExtensionsStore store(nullptr);
        const QVariantMap nyaa = findById(store.installed(), QStringLiteral("colosseum.well.nyaa"));
        QVERIFY2(!nyaa.isEmpty(), "colosseum.well.nyaa missing from the house seed");
        QVERIFY2(isRemovableWell(nyaa), "colosseum.well.nyaa is not classified as a removable well");
        QVERIFY2(!nyaa.value(QStringLiteral("enabled")).toBool(),
                 "colosseum.well.nyaa seeded ENABLED on a fresh install -- nyaa must ship dark");
    }

    void legacy_weebcentral_well_migrates_in_place_to_tankoyomi()
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/extensions/installed.json");
        int originalIndex = -1;
        {
            ExtensionsStore seeded(nullptr);
            const QVariantList items = seeded.installed();
            for (int i = 0; i < items.size(); ++i) {
                if (items.at(i).toMap().value(QStringLiteral("id")).toString()
                    == QStringLiteral("colosseum.well.tankoyomi")) {
                    originalIndex = i;
                    break;
                }
            }
        }
        QVERIFY(originalIndex >= 0);

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        QJsonArray rows = root.value(QStringLiteral("extensions")).toArray();
        QVERIFY(originalIndex < rows.size());
        QJsonObject legacy = rows.at(originalIndex).toObject();
        legacy.insert(QStringLiteral("id"), QStringLiteral("colosseum.well.weebcentral.pages"));
        legacy.insert(QStringLiteral("transportUrl"), QStringLiteral("colosseum://well/weebcentral.pages"));
        legacy.insert(QStringLiteral("enabled"), true);
        QJsonObject legacyManifest = legacy.value(QStringLiteral("manifest")).toObject();
        legacyManifest.insert(QStringLiteral("id"), QStringLiteral("colosseum.well.weebcentral.pages"));
        legacyManifest.insert(QStringLiteral("name"), QStringLiteral("WeebCentral"));
        legacy.insert(QStringLiteral("manifest"), legacyManifest);
        rows.replace(originalIndex, legacy);
        root.insert(QStringLiteral("defaultsVersion"), 11);
        root.insert(QStringLiteral("extensions"), rows);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();

        ExtensionsStore migrated(nullptr);
        const QVariantList after = migrated.installed();
        QCOMPARE(after.at(originalIndex).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("colosseum.well.tankoyomi"));

        QVERIFY(after.at(originalIndex).toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(findById(after, QStringLiteral("colosseum.well.weebcentral.pages")).isEmpty());
        const QVariantMap tankoyomi = findById(after, QStringLiteral("colosseum.well.tankoyomi"));
        QVERIFY(!tankoyomi.isEmpty());
        QCOMPARE(tankoyomi.value(QStringLiteral("transportUrl")).toString(),
                 QStringLiteral("colosseum://well/tankoyomi"));
        QCOMPARE(tankoyomi.value(QStringLiteral("manifest")).toMap()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Tankoyomi"));
    }

    void existing_tankoyomi_row_refreshes_configuration_metadata()
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/extensions/installed.json");
        int originalIndex = -1;
        QStringList originalOrder;
        {
            ExtensionsStore seeded(nullptr);
            const QVariantList before = seeded.installed();
            for (int i = 0; i < before.size(); ++i) {
                const QVariantMap row = before.at(i).toMap();
                originalOrder.append(row.value(QStringLiteral("id")).toString());
                if (row.value(QStringLiteral("id")).toString()
                    == QStringLiteral("colosseum.well.tankoyomi")) {
                    originalIndex = i;
                }
            }
        }
        QVERIFY(originalIndex >= 0);

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        QJsonArray rows = root.value(QStringLiteral("extensions")).toArray();
        QVERIFY(originalIndex < rows.size());
        QJsonObject stale = rows.at(originalIndex).toObject();
        stale.insert(QStringLiteral("enabled"), true); // explicit user choice must survive
        QJsonObject staleManifest = stale.value(QStringLiteral("manifest")).toObject();
        staleManifest.remove(QStringLiteral("behaviorHints"));
        stale.insert(QStringLiteral("manifest"), staleManifest);
        rows.replace(originalIndex, stale);
        root.insert(QStringLiteral("defaultsVersion"), 12);
        root.insert(QStringLiteral("extensions"), rows);
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();

        ExtensionsStore migrated(nullptr);
        const QVariantList after = migrated.installed();
        QCOMPARE(after.size(), originalOrder.size());
        for (int i = 0; i < after.size(); ++i) {
            QCOMPARE(after.at(i).toMap().value(QStringLiteral("id")).toString(), originalOrder.at(i));
        }

        const QVariantMap tankoyomi = after.at(originalIndex).toMap();
        QCOMPARE(tankoyomi.value(QStringLiteral("id")).toString(),
                 QStringLiteral("colosseum.well.tankoyomi"));
        QVERIFY(tankoyomi.value(QStringLiteral("enabled")).toBool());
        QVERIFY(tankoyomi.value(QStringLiteral("manifest")).toMap()
                    .value(QStringLiteral("behaviorHints")).toMap()
                    .value(QStringLiteral("configurable")).toBool());

        QFile verify(path);
        QVERIFY(verify.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(verify.readAll()).object()
                     .value(QStringLiteral("defaultsVersion")).toInt(), 14);
    }

    void configured_manifest_urls_keep_query_before_manifest_suffix()
    {
        ExtensionsStore store(nullptr);
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state?token=fixture")),
                 QStringLiteral("https://example.test/user-state/manifest.json?token=fixture"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state/manifest.json?token=fixture")),
                 QStringLiteral("https://example.test/user-state/manifest.json?token=fixture"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state#fragment")),
                 QStringLiteral("https://example.test/user-state/manifest.json#fragment"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("colosseum://house/source")),
                 QStringLiteral("colosseum://house/source"));
    }

    // Task 3: a configured Stremio instance is its normalized transport URL,
    // not its manifest id. Reverting finishInstall to id replacement must make
    // this fail by leaving one configured row instead of two.
    void configured_instances_with_same_manifest_id_survive_independently()
    {
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy finished(&store, &ExtensionsStore::installFinished);

        const QString first = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Configured/manifest.json?Token=Alpha")));
        const QString second = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Configured/manifest.json?Token=Beta")));
        QVERIFY(first != second);

        store.install(first);
        QTRY_COMPARE(finished.count(), 1);
        store.install(second);
        QTRY_COMPARE(finished.count(), 2);

        int matchingRows = 0;
        for (const QVariant &value : store.installed()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString()
                == QStringLiteral("fixture.same-manifest")) {
                ++matchingRows;
            }
        }
        QCOMPARE(matchingRows, 2);
    }

    // Task 3: the first real profile receives the recoverable legacy Theatre
    // portion once. A later profile receives only house defaults, and changing
    // back restores the original profile-local configured instance.
    void legacy_theatre_configuration_migrates_once_to_its_active_profile()
    {
        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy finished(&store, &ExtensionsStore::installFinished);
        const QString privateInstance = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Private/manifest.json?UserToken=Alpha")));

        store.install(privateInstance);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!findById(store.installed(), QStringLiteral("fixture.same-manifest")).isEmpty());

        const QString profileA = profiles.filePath(QStringLiteral("a/extensions.json"));
        const QString profileB = profiles.filePath(QStringLiteral("b/extensions.json"));
        QVERIFY2(activateProfile(&store, QStringLiteral("profile-a"), profileA),
                 "profile-scoped Theatre activation is missing");
        QVERIFY(!findById(store.installed(), QStringLiteral("fixture.same-manifest")).isEmpty());

        QVERIFY(activateProfile(&store, QStringLiteral("profile-b"), profileB));
        QVERIFY(findById(store.installed(), QStringLiteral("fixture.same-manifest")).isEmpty());

        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"), profileA));
        QVERIFY(!findById(store.installed(), QStringLiteral("fixture.same-manifest")).isEmpty());
    }

    // Task 3: extensions sharing a manifest id are different configured
    // instances. The action must remove the requested transport URL only;
    // swapping back to id-based removal removes the wrong/all instance.
    void configured_instance_removal_targets_its_transport_url()
    {
        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy finished(&store, &ExtensionsStore::installFinished);
        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"),
                                profiles.filePath(QStringLiteral("a/extensions.json"))));
        const QString first = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Remove/manifest.json?Mode=First")));
        const QString second = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Remove/manifest.json?Mode=Second")));

        store.install(first);
        QTRY_COMPARE(finished.count(), 1);
        store.install(second);
        QTRY_COMPARE(finished.count(), 2);

        QVERIFY2(QMetaObject::invokeMethod(&store, "removeInstance",
                                            Q_ARG(QString, first)),
                 "exact configured-instance removal is missing");
        QVERIFY(findByTransportUrl(store.installed(), first).isEmpty());
        QVERIFY(!findByTransportUrl(store.installed(), second).isEmpty());
    }

    // Task 3: reorder has the same identity boundary as removal. A move by
    // manifest id cannot select one configured URL when both share that id.
    void configured_instance_reorder_targets_its_transport_url()
    {
        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy finished(&store, &ExtensionsStore::installFinished);
        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"),
                                profiles.filePath(QStringLiteral("a/extensions.json"))));
        const QString first = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Order/manifest.json?Mode=First")));
        const QString second = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Order/manifest.json?Mode=Second")));

        store.install(first);
        QTRY_COMPARE(finished.count(), 1);
        store.install(second);
        QTRY_COMPARE(finished.count(), 2);
        const int firstIndex = indexOfTransportUrl(store.installed(), first);
        const int secondIndex = indexOfTransportUrl(store.installed(), second);
        QVERIFY(firstIndex >= 0);
        QVERIFY(secondIndex > firstIndex);

        QVERIFY2(QMetaObject::invokeMethod(&store, "moveInstanceTo",
                                            Q_ARG(QString, second),
                                            Q_ARG(int, firstIndex)),
                 "exact configured-instance reorder is missing");
        QCOMPARE(indexOfTransportUrl(store.installed(), second), firstIndex);
        QCOMPARE(indexOfTransportUrl(store.installed(), first), firstIndex + 1);
    }

    // Task 3: a duplicate manifest id must not make the enabled action mutate
    // its sibling configured instance.
    void configured_instance_toggle_targets_its_transport_url()
    {
        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy finished(&store, &ExtensionsStore::installFinished);
        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"),
                                profiles.filePath(QStringLiteral("a/extensions.json"))));
        const QString first = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Toggle/manifest.json?Mode=First")));
        const QString second = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Toggle/manifest.json?Mode=Second")));

        store.install(first);
        QTRY_COMPARE(finished.count(), 1);
        store.install(second);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY2(QMetaObject::invokeMethod(&store, "setEnabledInstance",
                                            Q_ARG(QString, second), Q_ARG(bool, false)),
                 "exact configured-instance toggle is missing");
        QVERIFY(findByTransportUrl(store.installed(), first).value(QStringLiteral("enabled")).toBool());
        QVERIFY(!findByTransportUrl(store.installed(), second).value(QStringLiteral("enabled")).toBool());
    }
    void profile_theatre_bulk_apply_receipt_follows_durable_commit()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const QString profileId = QStringLiteral("task3-bulk-profile");
        const QString indexPath = profile.filePath(QStringLiteral("installed.json"));
        const QString transportUrl = QStringLiteral("https://fixture.test/Configured?Case=Alpha");

        ExtensionsStore store(nullptr);
        QVERIFY(activateProfile(&store, profileId, indexPath));
        bool receiptReceived = false;
        bool receiptCommitted = false;
        bool durableAtReceipt = false;
        QVERIFY2(applyTheatreRows(
                     store,
                     QVariantList{theatreFixture(transportUrl)},
                     [&](bool committed, const QString &) {
                         receiptReceived = true;
                         receiptCommitted = committed;
                         ExtensionsStore reopened(nullptr);
                         durableAtReceipt = activateProfile(
                             &reopened, profileId, indexPath)
                             && !findByTransportUrl(reopened.installed(), transportUrl).isEmpty();
                     }),
                 "Task 3 bulk-receipt red: ExtensionsStore has no durable profile apply API.");
        QTRY_VERIFY(receiptReceived);
        QVERIFY(receiptCommitted);
        QVERIFY(durableAtReceipt);
    }
    void failed_profile_theatre_bulk_apply_rolls_back_before_receipt()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const QString profileId = QStringLiteral("task3-bulk-rollback");
        const QString indexPath = profile.filePath(QStringLiteral("installed.json"));
        const QString originalUrl = QStringLiteral("https://fixture.test/Configured?Case=Original");
        const QString rejectedUrl = QStringLiteral("https://fixture.test/Configured?Case=Rejected");

        ExtensionsStore store(nullptr);
        QVERIFY(activateProfile(&store, profileId, indexPath));
        bool initialReceipt = false;
        QVERIFY(applyTheatreRows(
            store,
            QVariantList{theatreFixture(originalUrl)},
            [&](bool committed, const QString &) { initialReceipt = committed; }));
        QTRY_VERIFY(initialReceipt);

        QVERIFY(QFile::remove(indexPath));
        QVERIFY(QDir().mkpath(indexPath));
        bool receiptReceived = false;
        bool receiptCommitted = true;
        QVERIFY(!applyTheatreRows(
            store,
            QVariantList{theatreFixture(rejectedUrl)},
            [&](bool committed, const QString &) {
                receiptReceived = true;
                receiptCommitted = committed;
            }));
        QTRY_VERIFY(receiptReceived);
        QVERIFY(!receiptCommitted);
        QVERIFY(!findByTransportUrl(store.installed(), originalUrl).isEmpty());
        QVERIFY(findByTransportUrl(store.installed(), rejectedUrl).isEmpty());
    }

    // Task 3: an explicit removal is a local owner mutation only after its
    // profile-local QSaveFile commits. A failed write must retain the row so
    // AccountRuntime cannot relay a removal that Theatre never durably owned.
    void failed_profile_instance_removal_keeps_the_durable_owner_snapshot()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const QString profileId = QStringLiteral("task3-instance-remove-rollback");
        const QString indexPath = profile.filePath(QStringLiteral("installed.json"));
        const QString transportUrl = QStringLiteral("https://fixture.test/Configured/manifest.json?Case=RemoveRollback");

        ExtensionsStore store(nullptr);
        QVERIFY(activateProfile(&store, profileId, indexPath));
        bool initialReceipt = false;
        QVERIFY(applyTheatreRows(
            store,
            QVariantList{theatreFixture(transportUrl)},
            [&](bool committed, const QString &) { initialReceipt = committed; }));
        QTRY_VERIFY(initialReceipt);
        QSignalSpy changed(&store, &ExtensionsStore::changed);

        QVERIFY(QFile::remove(indexPath));
        QVERIFY(QDir().mkpath(indexPath));
        QVERIFY2(QMetaObject::invokeMethod(&store, "removeInstance",
                                            Q_ARG(QString, transportUrl)),
                 "exact configured-instance removal is missing");

        QVERIFY(!findByTransportUrl(store.installed(), transportUrl).isEmpty());
        QCOMPARE(changed.count(), 0);
    }

    void failed_profile_install_does_not_publish_false_success()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        const QString profileId = QStringLiteral("task3-install-rollback");
        const QString indexPath = profile.filePath(QStringLiteral("installed.json"));
        ExtensionsStore store(&network);
        QVERIFY(activateProfile(&store, profileId, indexPath));
        const QString transportUrl = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Install/manifest.json?Case=Rollback")));
        QVERIFY(QFile::remove(indexPath));
        QVERIFY(QDir().mkpath(indexPath));
        QSignalSpy installed(&store, &ExtensionsStore::installFinished);
        QSignalSpy failed(&store, &ExtensionsStore::installFailed);

        store.install(transportUrl);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(installed.count(), 0);
        QVERIFY(findByTransportUrl(store.installed(), transportUrl).isEmpty());
    }

    // Task 3 red-first contract: provider membership is the managed suffix;
    // omitted rows are explicit removals/order changes, while required local
    // core capability and native/non-Theatre rows remain outside that set.
    void stremio_membership_never_reseeds_core_or_native_rows()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const QString profileId = QStringLiteral("task3-addon-owner");
        const QString indexPath = profile.filePath(QStringLiteral("installed.json"));
        ExtensionsStore store(nullptr);
        QVERIFY(activateProfile(&store, profileId, indexPath));

        const QVariantMap core = findById(
            store.installed(), QStringLiteral("com.linvo.cinemeta"));
        QVERIFY(!core.isEmpty());
        QVERIFY(core.value(QStringLiteral("core")).toBool());
        const QVariantMap native = findById(
            store.installed(), QStringLiteral("colosseum.catalogue.vault"));
        QVERIFY(!native.isEmpty());

        const QVariantMap a = theatreFixture(
            QStringLiteral("https://remote.test/a/manifest.json"),
            QStringLiteral("fixture.remote.a"));
        const QVariantMap b = theatreFixture(
            QStringLiteral("https://remote.test/b/manifest.json"),
            QStringLiteral("fixture.remote.b"));
        const QVariantMap c = theatreFixture(
            QStringLiteral("https://remote.test/c/manifest.json"),
            QStringLiteral("fixture.remote.c"));
        bool committed = false;
        QVERIFY(applyTheatreRows(
            store, QVariantList{a, b},
            [&](bool ok, const QString &) { committed = ok; }));
        QTRY_VERIFY(committed);
        QCOMPARE(store.stremioRows().size(), 2);
        QCOMPARE(store.stremioRows().at(0).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("fixture.remote.a"));
        QCOMPARE(store.stremioRows().at(1).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("fixture.remote.b"));

        committed = false;
        QVERIFY(applyTheatreRows(
            store, QVariantList{b},
            [&](bool ok, const QString &) { committed = ok; }));
        QTRY_VERIFY(committed);
        QVERIFY(findByTransportUrl(store.installed(),
                                   QStringLiteral("https://remote.test/a/manifest.json"))
                .isEmpty());
        QCOMPARE(store.stremioRows().size(), 1);

        committed = false;
        QVERIFY(applyTheatreRows(
            store, QVariantList{c, b},
            [&](bool ok, const QString &) { committed = ok; }));
        QTRY_VERIFY(committed);
        QCOMPARE(store.stremioRows().at(0).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("fixture.remote.c"));
        QCOMPARE(store.stremioRows().at(1).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("fixture.remote.b"));
        QVERIFY(!findById(store.installed(), QStringLiteral("com.linvo.cinemeta")).isEmpty());
        QVERIFY(!findById(store.installed(), QStringLiteral("colosseum.catalogue.vault")).isEmpty());
        for (const QVariant &value : store.stremioRows())
            QVERIFY(!value.toMap().value(QStringLiteral("core")).toBool());
    }

    // Task 3: a manifest reply is a profile-scoped continuation. A reply that
    // started under A must not publish its configured instance after B owns
    // Theatre, even though both owners use the same native ExtensionsStore.
    void stale_manifest_reply_cannot_publish_into_a_new_profile()
    {
        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        ManifestFixture fixture;
        QVERIFY(fixture.listen());
        fixture.holdResponses(true);
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        ExtensionsStore store(&network);
        QSignalSpy installed(&store, &ExtensionsStore::installFinished);

        const QString profileA = profiles.filePath(QStringLiteral("a/extensions.json"));
        const QString profileB = profiles.filePath(QStringLiteral("b/extensions.json"));
        const QString instance = store.normalizeUrl(
            fixture.configuredUrl(QStringLiteral("/Late/manifest.json?Profile=A")));
        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"), profileA));
        store.install(instance);
        QTRY_COMPARE(fixture.heldRequestCount(), 1);

        QVERIFY(activateProfile(&store, QStringLiteral("profile-b"), profileB));
        fixture.releaseHeldResponses();
        QTest::qWait(40);

        QCOMPARE(installed.count(), 0);
        QVERIFY(findByTransportUrl(store.installed(), instance).isEmpty());
        QVERIFY(activateProfile(&store, QStringLiteral("profile-a"), profileA));
        QVERIFY(findByTransportUrl(store.installed(), instance).isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_extensions_first_run)
#include "tst_extensions_first_run.moc"
