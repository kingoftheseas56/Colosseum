// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/SyncOwnershipInventory.h"
#include "account/SyncPayloadFirewall.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QtTest>

namespace {
QStringList approvedInventoryIds() {
    return {
        QStringLiteral("collection"),
        QStringLiteral("continue_progress"),
        QStringLiteral("watch_state"),
        QStringLiteral("full_history"),
        QStringLiteral("activity_fact"),
        QStringLiteral("per_world_customization"),
        QStringLiteral("wallpaper_personalization"),
        QStringLiteral("explicit_content_preference"),
        QStringLiteral("other_durable_preferences"),

        QStringLiteral("tankoban_reading_position"),
        QStringLiteral("tankoban_reader_preferences"),

        QStringLiteral("biblio_reading_position"),
        QStringLiteral("biblio_bookmarks"),
        QStringLiteral("biblio_annotations"),
        QStringLiteral("biblio_reader_settings"),
        QStringLiteral("biblio_audio_pairing"),

        QStringLiteral("theatre_track_preferences"),
        QStringLiteral("theatre_subtitle_appearance"),
        QStringLiteral("theatre_row_customization"),
        QStringLiteral("theatre_watched_history"),

        QStringLiteral("extension_roster"),
        QStringLiteral("extension_safe_config"),
        QStringLiteral("vault_identity_decisions"),
        QStringLiteral("desired_download_intent"),

        QStringLiteral("account_password"),
        QStringLiteral("recovery_key"),
        QStringLiteral("access_token"),
        QStringLiteral("refresh_token"),
        QStringLiteral("extension_credentials"),
        QStringLiteral("api_credentials"),

        QStringLiteral("search_history"),
        QStringLiteral("shell_session_state"),
        QStringLiteral("window_state"),
        QStringLiteral("pip_state"),
        QStringLiteral("cast_state"),
        QStringLiteral("room_state"),
        QStringLiteral("filesystem_paths"),
        QStringLiteral("physical_media"),
        QStringLiteral("media_blobs"),
        QStringLiteral("download_queue"),
        QStringLiteral("download_acquisition_history"),
        QStringLiteral("cache_state"),
        QStringLiteral("vault_path_aliases"),
        QStringLiteral("extension_transport_endpoint"),
        QStringLiteral("biblio_local_book_identity")
    };
}

QJsonObject safeCollectionPayload() {
    QJsonObject item;
    item.insert(
        QStringLiteral("id"),
        QStringLiteral("series-42"));
    item.insert(
        QStringLiteral("world"),
        QStringLiteral("Tankoban"));
    item.insert(
        QStringLiteral("type"),
        QStringLiteral("manga"));
    item.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture Manga"));
    item.insert(
        QStringLiteral("addedAt"),
        1720000000000.0);
    return item;
}
}

class tst_sync_inventory : public QObject {
    Q_OBJECT

private slots:
    void inventoryContainsEveryFrozenCategoryExactlyOnce();
    void everyExcludedCategoryCarriesAnExplicitDenial();
    void fullHistoryHasDedicatedCumulativeOwner();
    void activityFactsHaveImmutablePortableOwner();
    void searchHistoryIsHardLocalOnly();
    void progressPreservesSilentMutationWarning();
    void watchStateIsPortableAndSeparateFromContinue();
    void tankobanOwnershipIsFrozenButActivationRemainsScoped();
    void confirmedSafeOrdinaryCategoriesCanPass();
    void unresolvedSyncableCategoriesFailClosed();
    void unknownCategoryFailsClosed();
    void secretCategoriesNeverEnterOrdinaryPayloads();

    void forbiddenFieldSentinels_data();
    void forbiddenFieldSentinels();

    void filesystemPathSentinels_data();
    void filesystemPathSentinels();

    void nestedForbiddenFieldCannotHide();
    void ordinaryRemoteUrlsAreNotMistakenForFilesystemPaths();
};

void tst_sync_inventory::
inventoryContainsEveryFrozenCategoryExactlyOnce() {
    const QList<SyncOwnershipEntry> &entries =
        SyncOwnershipInventory::all();
    const QStringList expected =
        approvedInventoryIds();

    QCOMPARE(entries.size(), expected.size());

    QSet<QString> seen;
    for (const SyncOwnershipEntry &entry : entries) {
        QVERIFY2(
            !entry.id.trimmed().isEmpty(),
            "Every inventory row requires an id.");
        QVERIFY2(
            entry.id == entry.id.toLower(),
            qPrintable(
                QStringLiteral(
                    "Inventory id must be lowercase: %1")
                    .arg(entry.id)));
        QVERIFY2(
            !seen.contains(entry.id),
            qPrintable(
                QStringLiteral(
                    "Duplicate sync inventory id: %1")
                    .arg(entry.id)));
        seen.insert(entry.id);

        QVERIFY2(
            !entry.approvedFields.isEmpty(),
            qPrintable(
                QStringLiteral(
                    "Inventory row lacks machine-readable field coverage: %1")
                    .arg(entry.id)));

        QVERIFY2(
            entry.futureSlice > 0,
            qPrintable(
                QStringLiteral(
                    "Inventory row lacks a future/adoption slice: %1")
                    .arg(entry.id)));

        const QString disposition =
            SyncOwnershipInventory::dispositionName(
                entry.disposition);
        QVERIFY(
            disposition == QStringLiteral("syncable")
            || disposition == QStringLiteral("secret")
            || disposition == QStringLiteral("local-only"));

        const QString ownerStatus =
            SyncOwnershipInventory::ownerStatusName(
                entry.ownerStatus);
        QVERIFY(
            ownerStatus == QStringLiteral("confirmed")
            || ownerStatus == QStringLiteral("partial")
            || ownerStatus == QStringLiteral("absent"));
    }

    for (const QString &id : expected) {
        QVERIFY2(
            seen.contains(id),
            qPrintable(
                QStringLiteral(
                    "Frozen category missing from typed inventory: %1")
                    .arg(id)));
    }

    QCOMPARE(
        SyncOwnershipInventory::inspectionBaseCommit(),
        QStringLiteral(
            "e2ec3416bb706f324a24a004deace05c5a026edc"));
}

void tst_sync_inventory::
everyExcludedCategoryCarriesAnExplicitDenial() {
    for (const SyncOwnershipEntry &entry :
         SyncOwnershipInventory::all()) {
        if (entry.disposition
                == SyncDisposition::Syncable) {
            continue;
        }

        QVERIFY2(
            !entry.ordinaryPayloadEligible,
            qPrintable(
                QStringLiteral(
                    "Excluded category was marked ordinary-payload eligible: %1")
                    .arg(entry.id)));
        QVERIFY2(
            !entry.denialCode.isEmpty(),
            qPrintable(
                QStringLiteral(
                    "Excluded category lacks explicit denial code: %1")
                    .arg(entry.id)));

        const SyncPayloadValidation result =
            SyncPayloadFirewall::validate(
                entry.id,
                QJsonObject());
        QVERIFY(!result.allowed);
        QCOMPARE(
            result.code,
            entry.denialCode);
    }
}

void tst_sync_inventory::
fullHistoryHasDedicatedCumulativeOwner() {
    const SyncOwnershipEntry *history =
        SyncOwnershipInventory::find(
            QStringLiteral("full_history"));
    QVERIFY(history);
    QCOMPARE(
        history->disposition,
        SyncDisposition::Syncable);
    QCOMPARE(
        history->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(history->ordinaryPayloadEligible);
    QVERIFY(history->liveOwner.isEmpty());
    QVERIFY(
        history->cumulativeReferenceOwner.contains(
            QStringLiteral("HistoryStore")));
    QVERIFY(
        history->writeSeam.contains(
            QStringLiteral("markCompleted")));
    QVERIFY(
        history->note.contains(
            QStringLiteral(
                "Continue/progress remains separate")));
}

void tst_sync_inventory::
activityFactsHaveImmutablePortableOwner() {
    const SyncOwnershipEntry *activity =
        SyncOwnershipInventory::find(
            QStringLiteral("activity_fact"));
    QVERIFY(activity);
    QCOMPARE(
        activity->disposition,
        SyncDisposition::Syncable);
    QCOMPARE(
        activity->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(activity->ordinaryPayloadEligible);
    QVERIFY(activity->liveOwner.contains(
        QStringLiteral("ActivityStore")));
    QVERIFY(activity->readSeam.contains(
        QStringLiteral("portableSyncFacts")));
    QVERIFY(activity->writeSeam.contains(
        QStringLiteral("applySyncedPortableFact")));
    QVERIFY(activity->changeSeam.contains(
        QStringLiteral("factCommitted")));
    QVERIFY(activity->note.contains(
        QStringLiteral("immutable"),
        Qt::CaseInsensitive));
    QVERIFY(activity->note.contains(
        QStringLiteral("PUT")));
    QVERIFY(activity->note.contains(
        QStringLiteral("delete"),
        Qt::CaseInsensitive));
}

void tst_sync_inventory::
searchHistoryIsHardLocalOnly() {
    const SyncOwnershipEntry *history =
        SyncOwnershipInventory::find(
            QStringLiteral("search_history"));
    QVERIFY(history);
    QCOMPARE(
        history->disposition,
        SyncDisposition::LocalOnly);
    QCOMPARE(
        history->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QCOMPARE(
        history->denialCode,
        QStringLiteral("category_local_only"));

    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("search_history"),
            QJsonObject{
                {
                    QStringLiteral("query"),
                    QStringLiteral("private search")
                }
            });
    QVERIFY(!result.allowed);
    QCOMPARE(
        result.code,
        QStringLiteral("category_local_only"));
}

void tst_sync_inventory::
progressPreservesSilentMutationWarning() {
    const SyncOwnershipEntry *progress =
        SyncOwnershipInventory::find(
            QStringLiteral("continue_progress"));
    QVERIFY(progress);
    QCOMPARE(
        progress->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(progress->ordinaryPayloadEligible);
    QVERIFY(
        progress->writeSeam.contains(
            QStringLiteral("recordSilent")));
    QVERIFY(
        progress->changeSeam.contains(
            QStringLiteral(
                "recordSilent() deliberately emits no visible revision")));
    QVERIFY(
        progress->note.contains(
            QStringLiteral(
                "whole-store polling")));
}

void tst_sync_inventory::
watchStateIsPortableAndSeparateFromContinue() {
    const SyncOwnershipEntry *watchState =
        SyncOwnershipInventory::find(
            QStringLiteral("watch_state"));
    QVERIFY(watchState);
    QCOMPARE(
        watchState->disposition,
        SyncDisposition::Syncable);
    QCOMPARE(
        watchState->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(watchState->ordinaryPayloadEligible);
    QVERIFY(
        watchState->liveOwner.contains(
            QStringLiteral("ProgressStore")));
    QVERIFY(
        watchState->readSeam.contains(
            QStringLiteral("syncWatchedMarks")));
    QVERIFY(
        watchState->readSeam.contains(
            QStringLiteral("syncLastSeasons")));
    QVERIFY(
        watchState->writeSeam.contains(
            QStringLiteral("applySyncedWatchedMark")));
    QVERIFY(
        watchState->writeSeam.contains(
            QStringLiteral("removeSyncedLastSeason")));
    QVERIFY(
        watchState->note.contains(
            QStringLiteral("continue_progress")));

    const SyncPayloadValidation watched =
        SyncPayloadFirewall::validate(
            QStringLiteral("watch_state"),
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("movie-1")
                },
                {
                    QStringLiteral("mark"),
                    1
                }
            });
    QVERIFY2(
        watched.allowed,
        qPrintable(watched.code));
    const SyncPayloadValidation forbiddenField =
        SyncPayloadFirewall::validate(
            QStringLiteral("watch_state"),
            QJsonObject{
                {
                    QStringLiteral("path"),
                    QStringLiteral("C:\\Private\\watch-state.json")
                }
            });
    QVERIFY(!forbiddenField.allowed);
    QCOMPARE(
        forbiddenField.code,
        QStringLiteral("forbidden_field"));

    const SyncPayloadValidation filesystemValue =
        SyncPayloadFirewall::validate(
            QStringLiteral("watch_state"),
            QJsonObject{
                {
                    QStringLiteral("logicalValue"),
                    QStringLiteral("C:\\Private\\watch-state.json")
                }
            });
    QVERIFY(!filesystemValue.allowed);
    QCOMPARE(
        filesystemValue.code,
        QStringLiteral("filesystem_path_value"));
}

void tst_sync_inventory::
tankobanOwnershipIsFrozenButActivationRemainsScoped() {
    const SyncOwnershipEntry *position =
        SyncOwnershipInventory::find(
            QStringLiteral(
                "tankoban_reading_position"));
    QVERIFY(position);
    QCOMPARE(
        position->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(!position->ordinaryPayloadEligible);
    QVERIFY(
        position->cumulativeReferenceOwner
            .contains(
                QStringLiteral(
                    "continue_progress")));
    QVERIFY(
        position->note.contains(
            QStringLiteral(
                "adds no separate position adapter")));
    QVERIFY(
        position->changeSeam.contains(
            QStringLiteral(
                "syncedEntryApplied")));

    const SyncOwnershipEntry *preferences =
        SyncOwnershipInventory::find(
            QStringLiteral(
                "tankoban_reader_preferences"));
    QVERIFY(preferences);
    QCOMPARE(
        preferences->ownerStatus,
        SyncOwnerStatus::Confirmed);
    QVERIFY(!preferences->ordinaryPayloadEligible);
    QVERIFY(
        preferences->liveOwner.contains(
            QStringLiteral(
                "per-series")));
    QVERIFY(
        preferences->changeSeam.contains(
            QStringLiteral("8C")));

    const SyncPayloadValidation preferenceResult =
        SyncPayloadFirewall::validate(
            QStringLiteral(
                "tankoban_reader_preferences"),
            QJsonObject{
                {
                    QStringLiteral("layout"),
                    QStringLiteral(
                        "paired_pages")
                }
            });
    QVERIFY(!preferenceResult.allowed);
    QCOMPARE(
        preferenceResult.code,
        QStringLiteral(
            "category_not_exportable_yet"));
}

void tst_sync_inventory::
confirmedSafeOrdinaryCategoriesCanPass() {
    const QStringList categories = {
        QStringLiteral("collection"),
        QStringLiteral("continue_progress"),
        QStringLiteral("watch_state"),
        QStringLiteral("full_history"),
        QStringLiteral("activity_fact"),
        QStringLiteral("explicit_content_preference"),
        QStringLiteral("theatre_track_preferences"),
        QStringLiteral("theatre_row_customization"),
        QStringLiteral("extension_roster")
    };

    for (const QString &category : categories) {
        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(
                category);
        QVERIFY(entry);
        QCOMPARE(
            entry->disposition,
            SyncDisposition::Syncable);
        QCOMPARE(
            entry->ownerStatus,
            SyncOwnerStatus::Confirmed);
        QVERIFY(entry->ordinaryPayloadEligible);

        const SyncPayloadValidation result =
            SyncPayloadFirewall::validate(
                category,
                safeCollectionPayload());
        QVERIFY2(
            result.allowed,
            qPrintable(
                QStringLiteral(
                    "%1 unexpectedly rejected: %2 %3")
                    .arg(
                        category,
                        result.code,
                        result.fieldPath)));
    }
}

void tst_sync_inventory::
unresolvedSyncableCategoriesFailClosed() {
    const QStringList categories = {
        QStringLiteral("per_world_customization"),
        QStringLiteral("wallpaper_personalization"),
        QStringLiteral("tankoban_reader_preferences"),
        QStringLiteral("biblio_bookmarks"),
        QStringLiteral("theatre_subtitle_appearance"),
        QStringLiteral("extension_safe_config"),
        QStringLiteral("vault_identity_decisions"),
        QStringLiteral("desired_download_intent")
    };

    for (const QString &category : categories) {
        const SyncOwnershipEntry *entry =
            SyncOwnershipInventory::find(
                category);
        QVERIFY(entry);
        QCOMPARE(
            entry->disposition,
            SyncDisposition::Syncable);
        QVERIFY(!entry->ordinaryPayloadEligible);

        const SyncPayloadValidation result =
            SyncPayloadFirewall::validate(
                category,
                QJsonObject{
                    {
                        QStringLiteral("logicalId"),
                        QStringLiteral("fixture")
                    }
                });
        QVERIFY(!result.allowed);
        QCOMPARE(
            result.code,
            QStringLiteral(
                "category_not_exportable_yet"));
    }
}

void tst_sync_inventory::
unknownCategoryFailsClosed() {
    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("future_magic_category"),
            QJsonObject());
    QVERIFY(!result.allowed);
    QCOMPARE(
        result.code,
        QStringLiteral("unknown_category"));
}

void tst_sync_inventory::
secretCategoriesNeverEnterOrdinaryPayloads() {
    const QStringList categories = {
        QStringLiteral("account_password"),
        QStringLiteral("recovery_key"),
        QStringLiteral("access_token"),
        QStringLiteral("refresh_token"),
        QStringLiteral("extension_credentials"),
        QStringLiteral("api_credentials")
    };

    for (const QString &category : categories) {
        const SyncPayloadValidation result =
            SyncPayloadFirewall::validate(
                category,
                QJsonObject{
                    {
                        QStringLiteral("value"),
                        QStringLiteral("fixture-secret")
                    }
                });
        QVERIFY(!result.allowed);
        QCOMPARE(
            result.code,
            QStringLiteral(
                "secret_requires_protected_channel"));
    }
}

void tst_sync_inventory::forbiddenFieldSentinels_data() {
    QTest::addColumn<QString>("fieldName");

    const QStringList fields = {
        QStringLiteral("path"),
        QStringLiteral("file_path"),
        QStringLiteral("localPath"),
        QStringLiteral("outputPath"),
        QStringLiteral("oldPath"),
        QStringLiteral("newPath"),
        QStringLiteral("media_blob"),
        QStringLiteral("fileBytes"),
        QStringLiteral("search_history"),
        QStringLiteral("savedState"),
        QStringLiteral("windowGeometry"),
        QStringLiteral("pipState"),
        QStringLiteral("castState"),
        QStringLiteral("roomState"),
        QStringLiteral("password"),
        QStringLiteral("recovery_key"),
        QStringLiteral("accessToken"),
        QStringLiteral("refresh_token"),
        QStringLiteral("Authorization"),
        QStringLiteral("cookie"),
        QStringLiteral("api_key"),
        QStringLiteral("clientSecret"),
        QStringLiteral("credentials"),
        QStringLiteral("transportUrl"),
        QStringLiteral("streamUrl"),
        QStringLiteral("downloadUrl")
    };

    for (const QString &field : fields) {
        QTest::newRow(
            qPrintable(field))
            << field;
    }
}

void tst_sync_inventory::forbiddenFieldSentinels() {
    QFETCH(QString, fieldName);

    QJsonObject payload;
    payload.insert(
        fieldName,
        QStringLiteral("sentinel"));

    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("collection"),
            payload);

    QVERIFY(!result.allowed);
    QCOMPARE(
        result.code,
        QStringLiteral("forbidden_field"));
    QVERIFY(
        result.fieldPath.endsWith(
            fieldName));
}

void tst_sync_inventory::filesystemPathSentinels_data() {
    QTest::addColumn<QString>("value");

    QTest::newRow("windows-drive")
        << QStringLiteral(
               "C:\\Users\\fixture\\Videos\\episode.mkv");
    QTest::newRow("windows-forward")
        << QStringLiteral(
               "D:/Media/fixture.cbz");
    QTest::newRow("unc")
        << QStringLiteral(
               "\\\\server\\share\\book.epub");
    QTest::newRow("extended-unc")
        << QStringLiteral(
               "\\\\?\\C:\\Vault\\fixture.cbz");
    QTest::newRow("posix")
        << QStringLiteral(
               "/home/fixture/Book.epub");
    QTest::newRow("relative-parent")
        << QStringLiteral(
               "../private/book.epub");
    QTest::newRow("relative-current")
        << QStringLiteral(
               "./private/book.epub");
    QTest::newRow("file-url")
        << QStringLiteral(
               "file:///C:/Vault/fixture.cbz");
    QTest::newRow("qrc-resource")
        << QStringLiteral(
               "qrc:/private/generated.bin");
}

void tst_sync_inventory::filesystemPathSentinels() {
    QFETCH(QString, value);

    const QJsonObject payload{
        {
            QStringLiteral("logicalValue"),
            value
        }
    };

    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("collection"),
            payload);

    QVERIFY(!result.allowed);
    QCOMPARE(
        result.code,
        QStringLiteral(
            "filesystem_path_value"));
    QCOMPARE(
        result.fieldPath,
        QStringLiteral("$.logicalValue"));
}

void tst_sync_inventory::
nestedForbiddenFieldCannotHide() {
    QJsonObject nested;
    nested.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture"));
    nested.insert(
        QStringLiteral("recoveryKey"),
        QStringLiteral("CLSM-DO-NOT-SYNC"));

    QJsonArray rows;
    rows.append(nested);

    QJsonObject payload;
    payload.insert(
        QStringLiteral("items"),
        rows);

    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("collection"),
            payload);

    QVERIFY(!result.allowed);
    QCOMPARE(
        result.code,
        QStringLiteral("forbidden_field"));
    QCOMPARE(
        result.fieldPath,
        QStringLiteral("$.items[0].recoveryKey"));
}

void tst_sync_inventory::
ordinaryRemoteUrlsAreNotMistakenForFilesystemPaths() {
    const QJsonObject payload{
        {
            QStringLiteral("cover"),
            QStringLiteral(
                "https://example.invalid/poster/fixture.jpg")
        },
        {
            QStringLiteral("customCss"),
            QStringLiteral(
                "body { background-image: url(https://example.invalid/bg.png); }")
        }
    };

    const SyncPayloadValidation result =
        SyncPayloadFirewall::validate(
            QStringLiteral("collection"),
            payload);
    QVERIFY2(
        result.allowed,
        qPrintable(
            QStringLiteral("%1 %2")
                .arg(
                    result.code,
                    result.fieldPath)));
}

QTEST_MAIN(tst_sync_inventory)
#include "tst_sync_inventory.moc"
