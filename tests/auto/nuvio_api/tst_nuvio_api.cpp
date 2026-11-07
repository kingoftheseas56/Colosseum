#include "account/NuvioApi.h"
#include "account/NuvioAccount.h"
#include "account/HistoryStore.h"
#include "account/PlatformAccountCredentialStore.h"
#include "account/ProfilePreferencesStore.h"
#include "CollectionStore.h"
#include "ProgressStore.h"
#include "engine/ExtensionsStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
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

NuvioApi::Reply tokenReply(const QByteArray &access = QByteArrayLiteral("fixture-access")) {
    return {200, QJsonObject{
        {"user", QJsonObject{{"id", accountId}}},
        {"access_token", QString::fromLatin1(access)},
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

QJsonObject episodeRow(const QString &id, int episode, qint64 at) {
    auto row = progressRow(id, at, 25000);
    row.insert("content_type", "series");
    row.insert("season", 1);
    row.insert("episode", episode);
    row.insert("progress_key", QStringLiteral("%1_s1e%2").arg(id).arg(episode));
    row.insert("video_id", QStringLiteral("%1:1:%2").arg(id).arg(episode));
    row.insert("watched_at", at);
    return row;
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
    bool offline = false;
    QHash<QString, QJsonArray> deletes;
    NuvioApi::Reply pinReply{200, QJsonArray{QJsonObject{{"unlocked", true}, {"retry_after_seconds", 0}}}, {}};
    bool holdNextPin = false;
    bool replaceLockedProfile = false;
    QString settingsStamp = QStringLiteral("2026-07-07T20:15:13.123456+00:00");
    QJsonObject settings;
    bool settingsMissing = false;
    bool conflictNextSettingsWrite = false;
    QJsonObject lastSettingsWrite;
    int settingsWrites = 0;
    NuvioApi::Completion heldPin;
    NuvioAccount account{&vault, nullptr, [this](const QString &path, bool, const QJsonObject &body,
        const QByteArray &, NuvioApi::Completion done) {
        const QString method = path.section('/', -1);
        ++calls[method];
        if (offline) {
            done({503, {}, {}});
        } else if (method == QLatin1String("token?grant_type=password")) {
            done(tokenReply());
        } else if (method == QLatin1String("token?grant_type=refresh_token")) {
            done(failNextRefresh ? NuvioApi::Reply{400, {}, {}} : tokenReply());
        } else if (method == QLatin1String("sync_pull_profiles")) {
            done({200, QJsonArray{
                QJsonObject{{"id", sourceProfile}, {"user_id", accountId}, {"profile_index", 1},
                    {"name", "Main"}, {"pin_enabled", false}},
                QJsonObject{{"id", replaceLockedProfile
                        ? QStringLiteral("4abdb11d-dd89-4873-92b4-ce8cf79abc4f") : lockedProfile},
                    {"user_id", accountId}, {"profile_index", 2},
                    {"name", "Locked"}, {"pin_enabled", true}}}, {}});
        } else if (method == QLatin1String("sync_pull_profile_settings_blob")) {
            QCOMPARE(body.value("p_platform"), QJsonValue("mobile"));
            const int index = body.value("p_profile_id").toInt();
            done({200, settingsMissing ? QJsonArray{} : QJsonArray{QJsonObject{
                {"profile_id", index}, {"updated_at", settingsStamp}, {"settings_json", settings}}}, {}});
        } else if (method == QLatin1String("sync_push_profile_settings_blob_guarded")) {
            QCOMPARE(body.value("p_platform"), QJsonValue("mobile"));
            QCOMPARE(body.value("p_expected_updated_at").toString(), settingsStamp);
            lastSettingsWrite = body.value("p_settings_json").toObject();
            if (std::exchange(conflictNextSettingsWrite, false)) {
                settings.insert("fromAnotherDevice", true);
                settingsStamp = QStringLiteral("2026-07-07T20:15:13.223456+00:00");
                done({409, {}, {}});
                return;
            }
            settings = lastSettingsWrite;
            ++settingsWrites;
            done({200, settingsStamp, {}});
        } else if (path.startsWith(QLatin1String("/rest/v1/profile_settings_blobs?"))) {
            QCOMPARE(body.value("platform"), QJsonValue("mobile"));
            QCOMPARE(body.value("user_id").toString(), accountId);
            if (std::exchange(conflictNextSettingsWrite, false)) {
                settingsMissing = false;
                settings.insert("fromAnotherDevice", true);
                done({201, QJsonArray{}, {}});
                return;
            }
            settings = body.value("settings_json").toObject();
            settingsMissing = false;
            ++settingsWrites;
            done({201, QJsonArray{QJsonObject{{"updated_at", settingsStamp}}}, {}});
        } else if (method == QLatin1String("verify_profile_pin")) {
            QCOMPARE(body.value("p_profile_id").toInt(), 2);
            QCOMPARE(body.value("p_pin").toString().size(), 4);
            if (std::exchange(holdNextPin, false)) {
                heldPin = std::move(done);
                return;
            }
            done(pinReply);
        } else if (method.startsWith(QLatin1String("sync_get_"))) {
            const auto event = method.contains("watch_progress") ? progressEvent
                : method.contains("watched_items") ? watchedEvent : libraryEvent;
            done({200, QJsonValue(event.value("event_id").toInteger(7)), {}});
        } else if (method.endsWith(QLatin1String("_delta"))) {
            if (std::exchange(holdNextDelta, false)) {
                heldDelta = std::move(done);
                return;
            }
            QJsonArray events;
            const auto event = method.contains("watch_progress") ? progressEvent
                : method.contains("watched_items") ? watchedEvent : libraryEvent;
            if (event.value("event_id").toInteger() > body.value("p_since_event_id").toInteger())
                events.append(event);
            done({200, events, {}});
        } else if (method == QLatin1String("sync_pull_library")) {
            done({200, library, {}});
        } else if (method == QLatin1String("sync_pull_watched_items")) {
            done({200, watched, {}});
        } else if (method == QLatin1String("sync_pull_watch_progress")) {
            QCOMPARE(body.value("p_limit").toInt(), 200);
            QVERIFY(!body.contains("p_since_last_watched"));
            done({200, positions, {}});
        } else if (path.startsWith(QLatin1String("/rest/v1/addons?"))) {
            done({200, addonRows, {}});
        } else if (path.startsWith(QLatin1String("/rest/v1/watch_progress?"))) {
            const QUrlQuery query(QUrl(QStringLiteral("https://fixture.invalid") + path));
            QCOMPARE(query.queryItemValue("profile_id"), QStringLiteral("eq.1"));
            QCOMPARE(query.queryItemValue("offset"), QStringLiteral("0"));
            const QString id = query.queryItemValue("content_id").mid(3);
            const QString type = query.queryItemValue("content_type").mid(3);
            const qint64 before = query.queryItemValue("last_watched").mid(4).toLongLong();
            QJsonArray rows;
            for (const auto &value : positions) {
                const auto row = value.toObject();
                if (row.value("content_id") == id && row.value("content_type") == type
                    && row.value("last_watched").toInteger() <= before) {
                    rows.append(row);
                }
            }
            done({200, rows, {}});
        } else if (method.startsWith(QLatin1String("sync_delete_"))) {
            QCOMPARE(body.value("p_profile_id").toInt(), 1);
            for (const auto &key : body.value("p_keys").toArray())
                deletes[method].append(key);
            done({204, {}, {}});
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
    void offlineRemovalsSurviveReloadAndReachNuvio();
    void lockedProfilesRequireExplicitSessionPin();
    void pinRequestsAreNeverReplayedOrAcceptedAfterCancel();
    void nextUpDismissalSyncsAfterOfflineAndDoesNotClearResume();
    void continueWatchingRespectsRemoteDeleteAndKeepsNewerPlayback();
};

void tst_nuvio_api::signInIsStagedAndDisconnectClearsTokens() {
    class CappedBackend final : public AccountSecretBackend {
    public:
        bool isAvailable() const override { return true; }
        bool write(const QString &, const QByteArray &value) override {
            // Model Windows Credential Manager's CRED_MAX_CREDENTIAL_BLOB_SIZE.
            if (value.size() > 2560)
                return false;
            blob = value;
            return true;
        }
        std::optional<QByteArray> read(const QString &) const override { return blob; }
        bool remove(const QString &) override {
            blob.reset();
            return true;
        }
        QStringList targets(const QString &) const override { return {}; }
        std::optional<QByteArray> blob;
    };
    auto backend = std::make_shared<CappedBackend>();
    PlatformAccountCredentialStore vault(backend);
    const QByteArray access =
        QByteArrayLiteral("eyJhbGciOiJIUzI1NiJ9.").leftJustified(2040, 'a') + ".fixture";
    QCOMPARE(access.size(), 2048);
    NuvioApi api(&vault, nullptr, [&access](const QString &path, bool get,
        const QJsonObject &body, const QByteArray &bearer, NuvioApi::Completion done) {
        QVERIFY(!get);
        if (path == QLatin1String("/auth/v1/token?grant_type=password")) {
            QVERIFY(bearer.isEmpty());
            QCOMPARE(body.value("password").toString(), QStringLiteral("fixture-password"));
            done(tokenReply(access));
        } else {
            QCOMPARE(path, QStringLiteral("/rest/v1/rpc/sync_pull_profiles"));
            QCOMPARE(bearer, access);
            done({200, QJsonArray{}, {}});
        }
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
    QVERIFY(!backend->blob);
    api.cancelSignIn();
    QVERIFY(!api.saveSignIn());
    signIn();
    QVERIFY(api.saveSignIn(sourceProfile, 1));
    QVERIFY(backend->blob);
    const QJsonObject saved = QJsonDocument::fromJson(*backend->blob).object();
    QVERIFY(!saved.contains(QStringLiteral("access_token")));
    QVERIFY(!saved.contains(QStringLiteral("access_expires_at_utc_ms")));
    QVERIFY(!saved.contains(QStringLiteral("password")));
    const auto stored = vault.loadNuvio(QStringLiteral("local"));
    QVERIFY(stored);
    QCOMPARE(stored->refreshToken, QByteArray("fixture-refresh"));
    QCOMPARE(stored->nuvioProfileId, sourceProfile);
    QCOMPARE(stored->nuvioProfileIndex, 1);
    api.rpc(QStringLiteral("sync_pull_profiles"), {}, [](NuvioApi::Reply reply) {
        QCOMPARE(reply.status, 200);
    });
    QVERIFY(api.disconnectAccount());
    QVERIFY(!backend->blob);
}

void tst_nuvio_api::refreshIsSharedAndRevocationPauses() {
    Vault vault;
    vault.stored = StoredNuvioCredential{"local", accountId, "refresh", sourceProfile, 1};
    NuvioApi::Completion refreshReply;
    int refreshes = 0;
    int reads = 0;
    NuvioApi api(&vault, nullptr, [&](const QString &path, bool, const QJsonObject &body,
        const QByteArray &bearer, NuvioApi::Completion done) {
        if (path == QLatin1String("/auth/v1/token?grant_type=refresh_token")) {
            const auto expected = refreshes == 0 ? QStringLiteral("refresh")
                                                 : QStringLiteral("fixture-refresh");
            ++refreshes;
            QVERIFY(bearer.isEmpty());
            QCOMPARE(body.value("refresh_token").toString(), expected);
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
    QCOMPARE(vault.stored->refreshToken, QByteArray("fixture-refresh"));
    QCOMPARE(vault.stored->nuvioProfileId, sourceProfile);
    QCOMPARE(vault.stored->nuvioProfileIndex, 1);
    api.activate(QStringLiteral("local"));
    QVERIFY(api.connected());
    QVERIFY(!api.needsReconnect());
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
    QCOMPARE(fixture.account.phase(), QStringLiteral("pin"));
    QCOMPARE(fixture.calls.value("sync_pull_library"), 0);
    fixture.account.cancelPin();
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

void tst_nuvio_api::offlineRemovalsSurviveReloadAndReachNuvio() {
    Fixture fixture;
    QVERIFY(fixture.temporary.isValid());
    const QString show = QStringLiteral("tt991000001");
    const QString first = show + QStringLiteral(":1:1");
    const QString second = show + QStringLiteral(":1:2");
    const QString unrelated = QStringLiteral("tt991000002:1:1");
    auto library = libraryRow(show, 1000);
    library.insert("content_type", "series");
    fixture.library = {library};
    fixture.watched = {episodeRow(show, 1, 2000), episodeRow(show, 2, 2100)};
    fixture.positions = {episodeRow(show, 1, 3000), episodeRow(show, 2, 3100),
        episodeRow(QStringLiteral("tt991000002"), 1, 3200)};
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(1, true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    fixture.account.apply(true);
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY2(fixture.account.phase() == QStringLiteral("done"), qPrintable(fixture.account.message()));
    QVERIFY(fixture.collection.has("theatre", show));
    QVERIFY(!fixture.history.get("episode", first).isEmpty());
    QVERIFY(!fixture.progress.get("video", first).isEmpty());

    fixture.offline = true;
    QVERIFY(fixture.collection.remove("theatre", show));
    QVERIFY(fixture.account.unwatchEpisode(first));
    QVERIFY(fixture.progress.clearVideoResume(first));
    QVERIFY(fixture.account.episodeUnwatched(first));
    QVERIFY(!fixture.progress.get("video", second).isEmpty());
    const auto journals = QDir(fixture.temporary.filePath("nuvio")).entryList(
        {QStringLiteral("removals-v1-*.json")}, QDir::Files);
    QCOMPARE(journals.size(), 1);
    const QString journalPath = fixture.temporary.filePath(QStringLiteral("nuvio/") + journals.first());
    const auto readJournal = [&] {
        QFile file(journalPath);
        return file.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(file.readAll()).object().value("rows").toArray() : QJsonArray();
    };
    QCOMPARE(readJournal().size(), 3);
    fixture.activate();
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.deletes.isEmpty());

    fixture.offline = false;
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(!fixture.collection.has("theatre", show));
    QVERIFY(fixture.history.get("episode", first).isEmpty());
    QVERIFY(!fixture.history.get("episode", second).isEmpty());
    QVERIFY(fixture.progress.get("video", first).isEmpty());
    QVERIFY(!fixture.progress.get("video", second).isEmpty());
    QCOMPARE(fixture.deletes.value("sync_delete_library_items"),
        QJsonArray({QJsonObject{{"content_id", show}, {"content_type", "series"}}}));
    QCOMPARE(fixture.deletes.value("sync_delete_watched_items"),
        QJsonArray({QJsonObject{{"content_id", show}, {"season", 1}, {"episode", 1}}}));
    QCOMPARE(fixture.deletes.value("sync_delete_watch_progress"),
        QJsonArray({show + QStringLiteral("_s1e1")}));
    const auto receipts = readJournal();
    QCOMPARE(receipts.size(), 3);
    for (const auto &receipt : receipts)
        QVERIFY(receipt.toObject().value("confirmedAt").toInteger() > 0);

    fixture.progress.dismissContinue("video", second);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.progress.get("video", second).isEmpty());
    QVERIFY(!fixture.progress.get("video", unrelated).isEmpty());
    QCOMPARE(fixture.deletes.value("sync_delete_watch_progress"),
        QJsonArray({show + QStringLiteral("_s1e1"), show + QStringLiteral("_s1e1"),
            show + QStringLiteral("_s1e2")}));

    // A later genuine add must pass the tombstone barrier.
    library.insert("added_at", QDateTime::currentMSecsSinceEpoch() + 10000);
    fixture.library = {library};
    fixture.libraryEvent = library;
    fixture.libraryEvent.insert("event_id", 8);
    fixture.libraryEvent.insert("operation", "upsert");
    fixture.activate();
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.collection.has("theatre", show));
}

void tst_nuvio_api::lockedProfilesRequireExplicitSessionPin() {
    Fixture fixture;
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(2, true);
    QCOMPARE(fixture.account.phase(), QStringLiteral("pin"));
    QCOMPARE(fixture.account.selectedProfileName(), QStringLiteral("Locked"));
    QVERIFY(fixture.account.connectedProfileName().isEmpty());
    QVERIFY(!fixture.account.profiles().at(1).toMap().value("current").toBool());
    fixture.account.unlockProfile(QStringLiteral("12ab"));
    QCOMPARE(fixture.calls.value("verify_profile_pin"), 0);
    fixture.pinReply.data = QJsonArray{QJsonObject{{"unlocked", false}, {"retry_after_seconds", 89}}};
    fixture.account.unlockProfile(QStringLiteral("0102"));
    QCOMPARE(fixture.calls.value("verify_profile_pin"), 1);
    QVERIFY(fixture.account.message().contains(QStringLiteral("2")));
    QCOMPARE(fixture.calls.value("sync_pull_library"), 0);
    QVERIFY(!fixture.vault.stored);

    fixture.pinReply.data = QJsonArray{QJsonObject{{"unlocked", true}, {"retry_after_seconds", 0}}};
    fixture.replaceLockedProfile = true;
    fixture.account.unlockProfile(QStringLiteral("0102"));
    QCOMPARE(fixture.account.phase(), QStringLiteral("profiles"));
    QCOMPARE(fixture.calls.value("sync_pull_library"), 0);
    fixture.replaceLockedProfile = false;
    fixture.account.loadProfiles();
    fixture.account.previewProfile(2, true);
    fixture.account.unlockProfile(QStringLiteral("0102"));
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    QVERIFY(!fixture.vault.stored);
    fixture.account.apply(true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));
    QVERIFY(fixture.vault.stored);
    QCOMPARE(fixture.vault.stored->nuvioProfileId, lockedProfile);
    QCOMPARE(fixture.vault.stored->refreshToken, QByteArrayLiteral("fixture-refresh"));
    QCOMPARE(fixture.account.connectedProfileName(), QStringLiteral("Locked"));
    QVERIFY(fixture.account.profiles().at(1).toMap().value("current").toBool());
    QVERIFY(!fixture.account.profiles().at(0).toMap().value("current").toBool());

    fixture.account.loadProfiles();
    fixture.account.previewProfile(2);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    QCOMPARE(fixture.calls.value("verify_profile_pin"), 3);
    fixture.activate();
    QCOMPARE(fixture.account.connectedProfileName(), QStringLiteral("Profile 2"));
    fixture.account.loadProfiles();
    QCOMPARE(fixture.account.connectedProfileName(), QStringLiteral("Locked"));
    fixture.account.previewProfile(2);
    QCOMPARE(fixture.account.phase(), QStringLiteral("pin"));
    QCOMPARE(fixture.calls.value("verify_profile_pin"), 3);
}

void tst_nuvio_api::pinRequestsAreNeverReplayedOrAcceptedAfterCancel() {
    Fixture fixture;
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(2);
    fixture.pinReply = {401, {}, {}};
    fixture.account.unlockProfile(QStringLiteral("0102"));
    QCOMPARE(fixture.calls.value("verify_profile_pin"), 1);
    QCOMPARE(fixture.calls.value("token?grant_type=refresh_token"), 0);
    QCOMPARE(fixture.account.phase(), QStringLiteral("signIn"));

    fixture.signIn();
    fixture.account.previewProfile(2);
    fixture.holdNextPin = true;
    fixture.account.unlockProfile(QStringLiteral("0102"));
    QVERIFY(fixture.heldPin);
    fixture.account.cancelPin();
    std::exchange(fixture.heldPin, {})(
        {200, QJsonArray{QJsonObject{{"unlocked", true}, {"retry_after_seconds", 0}}}, {}});
    QCOMPARE(fixture.account.phase(), QStringLiteral("profiles"));
    QCOMPARE(fixture.calls.value("sync_pull_library"), 0);
    fixture.account.previewProfile(1);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
}

void tst_nuvio_api::nextUpDismissalSyncsAfterOfflineAndDoesNotClearResume() {
    Fixture fixture;
    const QString show = QStringLiteral("tt991000001");
    const QString seed = show + QStringLiteral(":1:1");
    const QString key = show + QStringLiteral("|1|1");
    fixture.settingsMissing = true;
    fixture.positions = {episodeRow(show, 1, 3000)};
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(1, true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    fixture.account.apply(true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));

    fixture.offline = true;
    QVERIFY(fixture.account.dismissNextUp(seed));
    QVERIFY(fixture.account.nextUpDismissed(seed));
    QVERIFY(!fixture.progress.get("video", seed).isEmpty());
    fixture.activate();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.account.nextUpDismissed(seed));
    QCOMPARE(fixture.settingsWrites, 0);

    fixture.offline = false;
    fixture.conflictNextSettingsWrite = true;
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.account.nextUpDismissed(seed));
    QCOMPARE(fixture.settingsWrites, 0);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QCOMPARE(fixture.settingsWrites, 1);
    QVERIFY(fixture.settings.value("fromAnotherDevice").toBool());
    const auto dismissals = [&] {
        const auto payload = fixture.settings.value("features").toObject()
            .value("continue_watching_settings_payload").toString().toUtf8();
        return QJsonDocument::fromJson(payload).object().value("dismissedNextUpKeys").toArray();
    };
    QCOMPARE(dismissals(), QJsonArray{key});

    auto playback = fixture.progress.get("video", seed);
    playback.insert("progress", 0.4);
    fixture.progress.record(playback);
    QVERIFY(!fixture.account.nextUpDismissed(seed));
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QCOMPARE(fixture.settingsWrites, 2);
    QVERIFY(dismissals().isEmpty());
    QVERIFY(fixture.settings.value("fromAnotherDevice").toBool());

    fixture.settings.insert("features", QJsonObject{
        {"continue_watching_settings_payload", QString::fromUtf8(QJsonDocument(
            QJsonObject{{"dismissedNextUpKeys", QJsonArray{QStringLiteral("imdb:") + key}}})
            .toJson(QJsonDocument::Compact))}});
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.account.nextUpDismissed(seed));
    QVERIFY(!fixture.account.nextUpDismissed(show + QStringLiteral(":1:2")));

    fixture.settings.insert("features", QJsonObject{{"continue_watching_settings_payload", QJsonObject{
        {"dismissedKeys", QJsonArray{show}}, {"unrelatedSetting", 8}}}});
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.account.nextUpDismissed(show + QStringLiteral(":2:1")));
    fixture.progress.record(playback);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(!fixture.account.nextUpDismissed(seed));
    QVERIFY(dismissals().isEmpty());
    const auto payload = fixture.settings.value("features").toObject()
        .value("continue_watching_settings_payload").toString().toUtf8();
    QCOMPARE(QJsonDocument::fromJson(payload).object().value("unrelatedSetting").toInt(), 8);
}

void tst_nuvio_api::continueWatchingRespectsRemoteDeleteAndKeepsNewerPlayback() {
    Fixture fixture;
    const QString show = QStringLiteral("tt991000001");
    const QString id = show + QStringLiteral(":1:1");
    fixture.positions = {episodeRow(show, 1, 3000), episodeRow(show, 2, 3100)};
    fixture.activate();
    fixture.signIn();
    fixture.account.previewProfile(1, true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("preview"));
    fixture.account.apply(true);
    QTRY_COMPARE(fixture.account.phase(), QStringLiteral("done"));

    fixture.progressEvent = episodeRow(show, 1, 3000);
    fixture.progressEvent.insert("operation", "delete");
    fixture.progressEvent.insert("event_id", 8);
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(fixture.progress.get("video", id).isEmpty());
    QVERIFY(!fixture.progress.get("video", show + QStringLiteral(":1:2")).isEmpty());
    QVERIFY(fixture.deletes.isEmpty());

    fixture.progressEvent = episodeRow(show, 2, 2500);
    fixture.progressEvent.insert("operation", "delete");
    fixture.progressEvent.insert("event_id", 9);
    fixture.activate(); // Re-read the previous state, retaining the saved cursor.
    fixture.account.syncNow();
    QTRY_VERIFY(!fixture.account.busy());
    QVERIFY(!fixture.progress.get("video", show + QStringLiteral(":1:2")).isEmpty());
}

QTEST_GUILESS_MAIN(tst_nuvio_api)
#include "tst_nuvio_api.moc"
