#include "account/NuvioApi.h"
#include "account/NuvioAccount.h"
#include "account/HistoryStore.h"
#include "account/ProfilePreferencesStore.h"
#include "CollectionStore.h"
#include "ProgressStore.h"
#include "engine/ExtensionsStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <utility>

namespace {
const auto accountId = QStringLiteral("15d3f753-7e5d-4fe8-bb27-80e79b60686d");
const auto sourceProfile = QStringLiteral("11dbd899-9400-4d95-9a7c-0d9b331b2e3b");
const auto lockedProfile = QStringLiteral("7356ea6f-a635-498b-9963-17bd289d1ad8");

class Vault final : public AccountCredentialStore {
public:
    bool isAvailable() const override { return true; }
    std::optional<StoredAccountCredential> loadActive() const override { return {}; }
    bool saveActive(const StoredAccountCredential &) override { return false; }
    bool clearActive() override { return false; }
    QList<QByteArray> pendingRevocations() const override { return {}; }
    bool addPendingRevocation(const QByteArray &) override { return false; }
    bool removePendingRevocation(const QByteArray &) override { return false; }
    QList<StoredAccountDeletion> pendingDeletions() const override { return {}; }
    bool savePendingDeletion(const StoredAccountDeletion &) override { return false; }
    bool removePendingDeletion(const QString &) override { return false; }

    std::optional<StoredNuvioCredential> loadNuvio(const QString &profileId) const override {
        return stored && stored->profileId == profileId ? stored : std::nullopt;
    }
    bool saveNuvio(const StoredNuvioCredential &value) override {
        stored = value;
        return true;
    }
    bool clearNuvio(const QString &profileId) override {
        if (stored && stored->profileId == profileId)
            stored.reset();
        return true;
    }
    std::optional<StoredNuvioCredential> stored;
};

NuvioApi::Reply tokenReply() {
    return {200, QJsonObject{
        {"user", QJsonObject{{"id", accountId}}},
        {"access_token", "fixture-access"},
        {"refresh_token", "fixture-refresh"},
        {"expires_in", 3600}}, {}};
}

QJsonObject libraryRow(const QString &id, qint64 at) {
    return {{"content_id", id}, {"content_type", "movie"}, {"name", id}, {"added_at", at}};
}

QJsonObject watchedRow(const QString &id, qint64 at) {
    return {{"content_id", id}, {"content_type", "movie"}, {"title", id}, {"watched_at", at}};
}

QJsonObject progressRow(const QString &id, qint64 at, qint64 position) {
    return {{"content_id", id}, {"content_type", "movie"}, {"video_id", id},
        {"progress_key", id}, {"position", position}, {"duration", 100000}, {"last_watched", at}};
}

struct Fixture {
    QTemporaryDir temporary;
    Vault vault;
    CollectionStore collection{temporary.filePath("collection.ini")};
    ProgressStore progress{temporary.filePath("progress.ini")};
    HistoryStore history{temporary.filePath("history.ini")};
    ProfilePreferencesStore preferences{temporary.filePath("preferences.ini")};
    QNetworkAccessManager network;
    ExtensionsStore extensions{&network};
    QHash<QString, int> calls;
    QJsonArray library;
    QJsonArray watched;
    QJsonArray positions;
    QJsonArray addonRows;
    QJsonArray pushedProgress;
    QJsonArray pushedLibrary;
    QJsonArray pushedWatched;
    QJsonObject libraryEvent;
    QJsonObject watchedEvent;
    QJsonObject progressEvent;
    NuvioApi::Completion heldDelta;
    bool holdNextDelta = false;
    bool failNextRefresh = false;
    NuvioAccount account{&vault, nullptr, [this](const QString &path, bool, const QJsonObject &body,
        const QByteArray &, NuvioApi::Completion done) {
        const QString method = path.section('/', -1);
        ++calls[method];
        if (method == QLatin1String("token?grant_type=password")) {
            done(tokenReply());
        } else if (method == QLatin1String("token?grant_type=refresh_token")) {
            done(failNextRefresh ? NuvioApi::Reply{400, {}, {}} : tokenReply());
        } else if (method == QLatin1String("sync_pull_profiles")) {
            done({200, QJsonArray{
                QJsonObject{{"id", sourceProfile}, {"user_id", accountId}, {"profile_index", 1},
                    {"name", "Main"}, {"pin_enabled", false}},
                QJsonObject{{"id", lockedProfile}, {"user_id", accountId}, {"profile_index", 2},
                    {"name", "Locked"}, {"pin_enabled", true}}}, {}});
        } else if (method.startsWith(QLatin1String("sync_get_"))) {
            const bool hasEvent = (method.contains("watch_progress") && !progressEvent.isEmpty())
                || (method.contains("watched_items") && !watchedEvent.isEmpty())
                || (method.contains("library") && !libraryEvent.isEmpty());
            done({200, QJsonValue(hasEvent ? 8 : 7), {}});
        } else if (method.endsWith(QLatin1String("_delta"))) {
            if (std::exchange(holdNextDelta, false)) {
                heldDelta = std::move(done);
                return;
            }
            QJsonArray events;
            if (body.value("p_since_event_id").toInteger() < 8) {
                const QJsonObject event = method.contains("watch_progress") ? progressEvent
                    : method.contains("watched_items") ? watchedEvent : libraryEvent;
                if (!event.isEmpty())
                    events.append(event);
            }
            done({200, events, {}});
        } else if (method == QLatin1String("sync_pull_library")) {
            done({200, library, {}});
        } else if (method == QLatin1String("sync_pull_watched_items")) {
            done({200, watched, {}});
        } else if (method == QLatin1String("sync_pull_watch_progress")) {
            done({200, positions, {}});
        } else if (path.startsWith(QLatin1String("/rest/v1/addons?"))) {
            done({200, addonRows, {}});
        } else if (method == QLatin1String("sync_push_watch_progress")) {
            const QJsonArray rows = body.value("p_entries").toArray();
            for (const QJsonValue &row : rows)
                pushedProgress.append(row);
            if (!rows.isEmpty()) {
                progressEvent = rows.first().toObject();
                progressEvent.insert("progress_key", progressEvent.value("content_id"));
                progressEvent.insert("event_id", 8);
                progressEvent.insert("operation", "upsert");
            }
            done({204, {}, {}});
        } else if (method == QLatin1String("sync_push_library_items")
                   || method == QLatin1String("sync_push_watched_items")) {
            const bool isLibrary = method.contains("library");
            auto &pushed = isLibrary ? pushedLibrary : pushedWatched;
            auto &event = isLibrary ? libraryEvent : watchedEvent;
            const auto rows = body.value("p_items").toArray();
            for (const QJsonValue &row : rows)
                pushed.append(row);
            if (!rows.isEmpty()) {
                event = rows.first().toObject();
                event.insert("event_id", 8);
                event.insert("operation", "upsert");
            }
            done({204, {}, {}});
        } else {
            done({500, {}, {}});
        }
    }};

    void activate() {
        account.setExtensions(&extensions);
        account.activate(QStringLiteral("local"), &collection, &progress, &history, &preferences,
            temporary.filePath("nuvio/cursors-v1.json"));
    }

    void signIn() {
        account.signIn(QStringLiteral("fixture@example.com"), QStringLiteral("fixture-password"));
    }

    QVariantMap count(const QString &name) const {
        for (const QVariant &value : account.preview()) {
            const QVariantMap row = value.toMap();
            if (row.value("category") == name)
                return row;
        }
        return {};
    }
};
}

class tst_nuvio_api : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void signInIsStagedAndDisconnectClearsTokens();
    void refreshIsSharedAndRevocationPauses();
    void cancellationFencesLateRepliesAndRetryAfterStopsRequests();
    void previewedImportIsScopedDatedAndRepeatable();
    void firstRunAndPrimaryRoundTripAreFenced();
};

void tst_nuvio_api::signInIsStagedAndDisconnectClearsTokens() {
    Vault vault;
    NuvioApi api(&vault, nullptr, [](const QString &path, bool get,
        const QJsonObject &body, const QByteArray &bearer, NuvioApi::Completion done) {
        QCOMPARE(path, QStringLiteral("/auth/v1/token?grant_type=password"));
        QVERIFY(!get);
        QVERIFY(bearer.isEmpty());
        QCOMPARE(body.value("password").toString(), QStringLiteral("fixture-password"));
        done(tokenReply());
    });
    api.activate(QStringLiteral("local"));
    auto signIn = [&api]() {
        api.signIn(QStringLiteral("fixture@example.com"), QStringLiteral("fixture-password"),
            [](NuvioApi::Reply reply) {
                QCOMPARE(reply.status, 200);
                QVERIFY(reply.data.isNull());
            });
    };
    signIn();
    QVERIFY(!vault.stored);
    api.cancelSignIn();
    QVERIFY(!api.saveSignIn());
    signIn();
    QVERIFY(api.saveSignIn());
    QCOMPARE(vault.stored->accessToken, QByteArray("fixture-access"));
    QCOMPARE(vault.stored->refreshToken, QByteArray("fixture-refresh"));
    QVERIFY(api.disconnectAccount());
    QVERIFY(!vault.stored);
}

void tst_nuvio_api::refreshIsSharedAndRevocationPauses() {
    Vault vault;
    vault.stored = StoredNuvioCredential{"local", accountId, "expired", "refresh", 1};
    NuvioApi::Completion refreshReply;
    int refreshes = 0;
    int reads = 0;
    NuvioApi api(&vault, nullptr, [&](const QString &path, bool, const QJsonObject &body,
        const QByteArray &bearer, NuvioApi::Completion done) {
        if (path == QLatin1String("/auth/v1/token?grant_type=refresh_token")) {
            ++refreshes;
            QVERIFY(body.contains("refresh_token"));
            refreshReply = std::move(done);
            return;
        }
        ++reads;
        QCOMPARE(bearer, QByteArray("fixture-access"));
        done({200, QJsonArray{}, {}});
    });
    api.activate(QStringLiteral("local"));
    int successes = 0;
    auto read = [&api, &successes]() {
        api.rpc(QStringLiteral("sync_pull_profiles"), {}, [&successes](NuvioApi::Reply reply) {
            successes += reply.status == 200;
        });
    };
    read();
    read();
    QCOMPARE(refreshes, 1);
    QCOMPARE(reads, 0);
    std::exchange(refreshReply, {})(tokenReply());
    QCOMPARE(reads, 2);
    QCOMPARE(successes, 2);
    vault.stored->accessExpiresAtUtcMs = 1;
    api.activate(QStringLiteral("local"));
    read();
    std::exchange(refreshReply, {})({400, {}, {}});
    QVERIFY(api.needsReconnect());
    read();
    QCOMPARE(refreshes, 2);
    QCOMPARE(reads, 2);
}

void tst_nuvio_api::cancellationFencesLateRepliesAndRetryAfterStopsRequests() {
    Vault vault;
    NuvioApi::Completion pending;
    int sent = 0;
    NuvioApi api(&vault, nullptr, [&](const QString &, bool, const QJsonObject &,
        const QByteArray &, NuvioApi::Completion done) {
        ++sent;
        pending = std::move(done);
    });
    api.activate(QStringLiteral("local"));
    const auto email = QStringLiteral("fixture@example.com");
    const auto password = QStringLiteral("fixture-password");
    api.signIn(email, password, [](NuvioApi::Reply) { QFAIL("Cancelled sign-in replied"); });
    api.cancelSignIn();
    std::exchange(pending, {})(tokenReply());
    QVERIFY(!api.saveSignIn());
    QVERIFY(!vault.stored);

    api.signIn(email, password, [](NuvioApi::Reply reply) { QCOMPARE(reply.status, 429); });
    std::exchange(pending, {})({429, {}, "60"});
    api.signIn(email, password, [](NuvioApi::Reply reply) { QCOMPARE(reply.status, 429); });
    QCOMPARE(sent, 2);
}

void tst_nuvio_api::previewedImportIsScopedDatedAndRepeatable() {
    Fixture fixture;
    QVERIFY(fixture.temporary.isValid());
    QVERIFY(fixture.extensions.activateProfile("local", fixture.temporary.filePath("extensions.json")));
    const QString installedUrl = QStringLiteral("https://fixture.invalid/manifest.json");
    QVERIFY(fixture.extensions.applyTheatreRows({QVariantMap{
        {"id", QStringLiteral("fixture.addon")}, {"transportUrl", installedUrl},
        {"installedAt", qint64(1)}, {"enabled", true}, {"core", false},
        {"manifest", QVariantMap{{"id", QStringLiteral("fixture.addon")},
            {"name", QStringLiteral("Fixture")}, {"types", QStringList{"movie"}},
            {"resources", QStringList{"meta"}}}}}}));
    QTRY_VERIFY(fixture.extensions.isInstalled(installedUrl));
    QVERIFY(fixture.progress.applySyncedWatchedMark("tt200", -1, 3000, false, true));
    fixture.library = {libraryRow("tt100", 1000), libraryRow("tt100", 1000),
        libraryRow("tmdb:550", 1100), libraryRow("opaque:unmatched", 1200)};
    fixture.watched = {watchedRow("tt100", 2000), watchedRow("tt200", 2200),
        QJsonObject{{"content_id", "tt300"}, {"content_type", "series"}, {"season", 2},
            {"episode", 1}, {"watched_at", 2400}},
        QJsonObject{{"content_id", "tt300"}, {"content_type", "series"}, {"season", 2},
            {"watched_at", 2500}}};
    fixture.addonRows = {QJsonObject{{"url", "stremio://fixture.invalid/manifest.json"}}};
    fixture.activate();
    fixture.signIn();
    QCOMPARE(fixture.account.phase(), QStringLiteral("profiles"));
    QVERIFY(!fixture.vault.stored);
    QVERIFY(fixture.account.profiles().at(1).toMap().value("locked").toBool());
    fixture.account.previewProfile(2);
    QCOMPARE(fixture.calls.value("sync_pull_library"), 0);
    fixture.account.previewProfile(1);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    QCOMPARE(fixture.count("library").value("matched").toInt(), 2);
    QCOMPARE(fixture.count("library").value("unresolved").toInt(), 1);
    QCOMPARE(fixture.count("library").value("skipped").toInt(), 1);
    QCOMPARE(fixture.count("movies").value("existing").toInt(), 1);
    QCOMPARE(fixture.count("episodes").value("unresolved").toInt(), 1);
    QCOMPARE(fixture.count("addons").value("existing").toInt(), 1);
    QVERIFY(fixture.count("progress").isEmpty());
    QVERIFY(fixture.collection.items("theatre").isEmpty());
    fixture.account.apply();
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));
    QVERIFY(fixture.vault.stored);
    QCOMPARE(fixture.vault.stored->nuvioProfileId, sourceProfile);
    QCOMPARE(fixture.vault.stored->nuvioProfileIndex, 1);
    QCOMPARE(fixture.collection.items("theatre").size(), 2);
    QCOMPARE(fixture.progress.watchedMark("tt200"), -1);
    QCOMPARE(fixture.progress.watchedMarkActionAt("tt100"), 2000);
    QVERIFY(fixture.progress.syncEntries().isEmpty());
    const auto episode = fixture.history.get("episode", "tt300:2:1");
    QCOMPARE(episode.value("source").toString(), QStringLiteral("nuvio"));
    QCOMPARE(episode.value("completedAt").toLongLong(), 2400);
    QVERIFY(fixture.history.get("episode", "tt300:2:0").isEmpty());
    QCOMPARE(fixture.calls.value("sync_pull_watch_progress"), 0);

    const int revision = fixture.history.revision();
    fixture.account.loadProfiles();
    fixture.account.previewProfile(1);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    QCOMPARE(fixture.count("library").value("matched").toInt(), 0);
    fixture.account.apply();
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));
    QCOMPARE(fixture.history.revision(), revision);
    fixture.account.disconnect();
    QVERIFY(!fixture.vault.stored);
    QCOMPARE(fixture.history.revision(), revision);
    QCOMPARE(fixture.collection.items("theatre").size(), 2);
}

void tst_nuvio_api::firstRunAndPrimaryRoundTripAreFenced() {
    Fixture fixture;
    QVERIFY(fixture.temporary.isValid());
    fixture.library = {libraryRow("tmdb:550", 1000)};
    fixture.watched = {watchedRow("tt200", 2000)};
    fixture.positions = {progressRow("tmdb:550", 4000, 20000)};
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(1, true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    QCOMPARE(fixture.count("progress").value("matched").toInt(), 1);
    fixture.account.cancel();
    QVERIFY(!fixture.vault.stored);
    QVERIFY(fixture.collection.items("theatre").isEmpty());
    QVERIFY(fixture.progress.syncEntries().isEmpty());
    QVERIFY(fixture.preferences.mainSyncProvider().isEmpty());

    fixture.signIn();
    fixture.account.previewProfile(1, true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    fixture.account.apply(true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));
    QCOMPARE(fixture.preferences.mainSyncProvider(), QStringLiteral("nuvio"));
    auto entry = fixture.progress.get("video", "tmdb:550");
    QCOMPARE(entry.value("resume").toMap().value("position").toDouble(), 20.0);
    QCOMPARE(entry.value("updatedAt").toLongLong(), 4000);

    QVERIFY(fixture.collection.remove("theatre", "tmdb:550"));
    const int fullReads = fixture.calls.value("sync_pull_library");
    fixture.activate();
    QTRY_VERIFY(fixture.calls.value("sync_pull_library") > fullReads);
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(!fixture.collection.has("theatre", "tmdb:550"));

    auto changePosition = [&](int position, qint64 at) {
        entry.insert("resume", QVariantMap{{"position", position}});
        entry.insert("progress", position / 100.0);
        entry.insert("updatedAt", at);
        bool written = false;
        QVERIFY(fixture.progress.applySyncedEntryAsync(entry,
            [&](bool ok, const QString &) { written = ok; }));
        QTRY_VERIFY(written);
    };
    changePosition(30, 4000);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QCOMPARE(fixture.progress.get("video", "tmdb:550").value("resume")
        .toMap().value("position").toDouble(), 30.0);
    QVERIFY(fixture.pushedProgress.isEmpty());

    QVERIFY(fixture.collection.add("theatre", {{"id", QStringLiteral("tt777")},
        {"type", QStringLiteral("movie")}, {"title", QStringLiteral("Fixture film")}, {"addedAt", 4500}}));
    QVERIFY(fixture.progress.applySyncedWatchedMark("tt777", 1, 4600, false, true));
    QVERIFY(fixture.progress.applySyncedWatchedMark("tt888", 1, 4600, false, true));
    changePosition(40, 5000);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QCOMPARE(fixture.pushedProgress.size(), 1);
    QCOMPARE(fixture.pushedProgress.first().toObject().value("position").toInteger(), 40000);
    QCOMPARE(fixture.pushedProgress.first().toObject().value("last_watched").toInteger(), 5000);
    QCOMPARE(fixture.pushedLibrary.size(), 1);
    QCOMPARE(fixture.pushedWatched.size(), 1);
    QCOMPARE(fixture.pushedWatched.first().toObject().value("watched_at").toInteger(), 4600);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QCOMPARE(fixture.pushedProgress.size(), 1);
    QCOMPARE(fixture.pushedLibrary.size(), 1);
    QCOMPARE(fixture.pushedWatched.size(), 1);

    changePosition(50, 6000);
    fixture.holdNextDelta = true;
    fixture.account.syncNow();
    QVERIFY(fixture.heldDelta);
    QVERIFY(fixture.preferences.setMainSyncProvider(QStringLiteral("stremio")));
    std::exchange(fixture.heldDelta, {})({200, QJsonArray{}, {}});
    QVERIFY(!fixture.account.busy());
    QCOMPARE(fixture.pushedProgress.size(), 1);
    QCOMPARE(fixture.calls.value("sync_delete_watched_items"), 0);
}

QTEST_GUILESS_MAIN(tst_nuvio_api)
#include "tst_nuvio_api.moc"
