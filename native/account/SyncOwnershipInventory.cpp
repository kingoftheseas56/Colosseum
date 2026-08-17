// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncOwnershipInventory.h"

namespace {
const QList<SyncOwnershipEntry> &entries() {
    static const QList<SyncOwnershipEntry> value = {
        SyncOwnershipEntry{
            QStringLiteral("collection"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("collection")},
            QStringLiteral("native/CollectionStore.h"),
            QStringLiteral("ProfileStoreRuntime -> CollectionStore"),
            QStringLiteral("items(world)"),
            QStringLiteral("add(world, entry) / remove(world, id)"),
            QStringLiteral("revision + changed()"),
            14,
            QStringLiteral(""),
            QStringLiteral("Current owner is authoritative. Adapter must export logical collection fields only; path/media fields remain forbidden.")
        },
        SyncOwnershipEntry{
            QStringLiteral("continue_progress"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("continue"), QStringLiteral("progress")},
            QStringLiteral("native/ProgressStore.h"),
            QStringLiteral("ProfileStoreRuntime -> ProgressStore"),
            QStringLiteral("get(kind, id) / recent(kind, limit)"),
            QStringLiteral("record(entry) / recordSilent(entry) / forget(kind, id)"),
            QStringLiteral("revision + changed() for visible mutations; recordSilent() deliberately emits no visible revision"),
            14,
            QStringLiteral(""),
            QStringLiteral("Preserve recordSilent() performance contract; whole-store polling is not an acceptable substitute for a narrow dirty/export seam.")
        },
        SyncOwnershipEntry{
            QStringLiteral("full_history"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("watched_history"), QStringLiteral("read_history"), QStringLiteral("completed_history")},
            QStringLiteral(""),
            QStringLiteral("native/account/HistoryStore.* -> ProfileStoreRuntime -> HistoryStore"),
            QStringLiteral("records() / get(kind, id) / syncEntries()"),
            QStringLiteral("recordActivity(kind,id,at) / markCompleted(kind,id,at) / remove(kind,id) / applySyncedRecord(record)"),
            QStringLiteral("revision + changed(); syncDirty() only for local durable history mutations"),
            14,
            QStringLiteral(""),
            QStringLiteral("No live native/HistoryStore.h exists at the 7B evidence head; Bundle 7B promotes the cumulative profile-owned HistoryStore into the dedicated full-history authority. Continue/progress remains separate.")
        },
        SyncOwnershipEntry{
            QStringLiteral("per_world_customization"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("per_world_customization")},
            QStringLiteral("qml/PersonalizePage.qml and world-specific QML settings surfaces (exact portable seam not frozen)"),
            QStringLiteral(""),
            QStringLiteral("fragmented QML/Settings state"),
            QStringLiteral("fragmented QML/Settings state"),
            QStringLiteral("fragmented"),
            14,
            QStringLiteral(""),
            QStringLiteral("7C reinspection did not establish one durable portable owner/schema. PersonalizePage is a prototype and world-specific preference owners remain distributed across later domain slices. Registration remains blocked.")
        },
        SyncOwnershipEntry{
            QStringLiteral("wallpaper_personalization"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("wallpapers"), QStringLiteral("personalization")},
            QStringLiteral("qml/PersonalizePage.qml (confirmed by approved plan evidence; exact persistence seam requires adoption inspection)"),
            QStringLiteral(""),
            QStringLiteral("unknown exact portable snapshot seam"),
            QStringLiteral("unknown exact apply seam"),
            QStringLiteral("unknown"),
            14,
            QStringLiteral(""),
            QStringLiteral("Live PersonalizePage is a prototype/throwaway design probe at the 7C evidence head and does not establish a production persistence/apply seam. Keep wallpaper personalization blocked until a durable logical owner exists; never sync local image paths or media blobs.")
        },
        SyncOwnershipEntry{
            QStringLiteral("explicit_content_preference"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("explicit_content_on_off")},
            QStringLiteral("qml/ContentPreferences.qml"),
            QStringLiteral("native/account/ProfilePreferencesStore.* via cumulative 4A+ composition"),
            QStringLiteral("showExplicit"),
            QStringLiteral("showExplicit assignment"),
            QStringLiteral("QML property change / cumulative ProfilePreferencesStore::showExplicitChanged()"),
            14,
            QStringLiteral(""),
            QStringLiteral("Live ContentPreferences.qml remains the QML explicit-content surface; cumulative production authority is ProfilePreferencesStore and 7C adds its production sync adapter.")
        },
        SyncOwnershipEntry{
            QStringLiteral("other_durable_preferences"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("other_durable_user_choices")},
            QStringLiteral("multiple current QML/native settings owners"),
            QStringLiteral(""),
            QStringLiteral("not unified"),
            QStringLiteral("not unified"),
            QStringLiteral("not unified"),
            14,
            QStringLiteral(""),
            QStringLiteral("Current settings remain distributed across domain-specific QML/native owners. 7C does not create an arbitrary settings JSON blob; exact owners remain gated.")
        },
        SyncOwnershipEntry{
            QStringLiteral("tankoban_reading_position"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("page_position"), QStringLiteral("strip_position")},
            QStringLiteral("native/ProgressStore.h; qml/comicreader/ComicReaderShell.qml + ComicReaderState.js produce the logical resume payload"),
            QStringLiteral("ProfileStoreRuntime -> ProgressStore -> existing continue_progress adapter"),
            QStringLiteral("ProgressStore::get(kind,id) / raw sync entries; Tankoban resume is nested in the logical Progress record"),
            QStringLiteral("ComicReaderState.js::progressPayload -> ProgressStore::record/recordSilent"),
            QStringLiteral("existing ProgressStore syncDirty/revision contract from 7A; recordSilent remains visually silent; 8B adds remote-only syncedEntryApplied(kind,id) for active-reader import reaction"),
            15,
            QStringLiteral(""),
            QStringLiteral("8B keeps Tankoban page/strip resume inside continue_progress and adds no separate position adapter. Remote winning Progress PUT may notify the active reader through syncedEntryApplied plus ComicReaderSyncedResumeBridge after logical validation. Final private Shell restore hookup remains repository-local discovery-gated; archive/download paths stay local.")
        },
        SyncOwnershipEntry{
            QStringLiteral("tankoban_reader_preferences"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("reading_mode"), QStringLiteral("reading_direction"), QStringLiteral("zoom"), QStringLiteral("strip_width"), QStringLiteral("gap"), QStringLiteral("auto_scroll"), QStringLiteral("brightness"), QStringLiteral("contrast"), QStringLiteral("gamma"), QStringLiteral("rotation"), QStringLiteral("crop"), QStringLiteral("night_treatment")},
            QStringLiteral("shared per-series qml/comicreader seriesRecords map + ComicReaderState.migrateReaderPrefs + native/comicreader/ComicReaderRenderProfile.*"),
            QStringLiteral(""),
            QStringLiteral("confirmed semantic owner is the durable shared per-series seriesRecords map; 8C requires a concrete Tankoban-only headless enumeration binding before activation"),
            QStringLiteral("8C owner contract defines applySyncedRawRecord/removeSyncedRawRecord; binding to the exact production seriesRecords Settings backing remains discovery-gated"),
            QStringLiteral("8C owner contract defines revision + localMutationAvailable + remote-only syncedRecordApplied; production emitter wiring remains discovery-gated"),
            15,
            QStringLiteral(""),
            QStringLiteral("8C completes the schema-v1 codec and SyncAdapter contract but keeps registration fail-closed. The durable seriesRecords map is shared by ComicReader lanes, and current inspectable evidence does not prove a persistent Tankoban lane discriminator or exact headless Settings backing. Exporting every shared record would misclassify manga/comic preferences. renderProfile.quality remains excluded pending product decision.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_reading_position"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("exact_reading_position")},
            QStringLiteral("native/reader/BookStores.* + native/reader2/Reader2Bridge.*"),
            QStringLiteral(""),
            QStringLiteral("BookStores::get(progress.json, bookId)"),
            QStringLiteral("BookStores::save(progress.json, bookId, data)"),
            QStringLiteral("bridge/store mutation; portable identity seam required"),
            16,
            QStringLiteral(""),
            QStringLiteral("Current BookStores::keyFor() is derived from normalized absolute path. A portable identity must replace/translate that key before ordinary sync.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_bookmarks"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("bookmarks")},
            QStringLiteral("native/reader/BookStores.* + native/reader2/Reader2Bridge.*"),
            QStringLiteral(""),
            QStringLiteral("BookStores::listGet(bookmarks.json, bookId)"),
            QStringLiteral("listSave/listDelete/listClear(bookmarks.json, ...)"),
            QStringLiteral("bridge/store mutation; portable identity seam required"),
            16,
            QStringLiteral(""),
            QStringLiteral("Bookmark content is approved; absolute-path-derived book identity is not portable.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_annotations"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("annotations")},
            QStringLiteral("native/reader/BookStores.* + native/reader2/Reader2Bridge.*"),
            QStringLiteral(""),
            QStringLiteral("BookStores::listGet(annotations.json, bookId)"),
            QStringLiteral("listSave/listDelete/listClear(annotations.json, ...)"),
            QStringLiteral("bridge/store mutation; portable identity seam required"),
            16,
            QStringLiteral(""),
            QStringLiteral("Annotation payload is approved; local-path identity must not leave the device.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_reader_settings"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("typography"), QStringLiteral("theme"), QStringLiteral("layout"), QStringLiteral("custom_css")},
            QStringLiteral("native/reader/BookStores.* + native/reader2/Reader2Bridge.*"),
            QStringLiteral(""),
            QStringLiteral("BookStores::get(settings.json, bookId)"),
            QStringLiteral("BookStores::save(settings.json, bookId, data)"),
            QStringLiteral("bridge/store mutation; portable identity seam required"),
            16,
            QStringLiteral(""),
            QStringLiteral("Covers typography/theme/layout/custom CSS where supported. Custom CSS text is allowed; filesystem references inside portable records are not.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_audio_pairing"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("book_audiobook_read_along_pairing")},
            QStringLiteral("native/AudioPairingStore.h"),
            QStringLiteral("ProfileStoreRuntime -> AudioPairingStore"),
            QStringLiteral("getPairing(bookId) / allPairings()"),
            QStringLiteral("savePairing(bookId, pairing) / deletePairing(bookId)"),
            QStringLiteral("revision + changed() + pairingSaved/pairingDeleted"),
            16,
            QStringLiteral(""),
            QStringLiteral("Current bookId is documented as stable identity '(id or path)'; portable cross-device identity must be proven before registration.")
        },
        SyncOwnershipEntry{
            QStringLiteral("theatre_track_preferences"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("preferred_audio_language"), QStringLiteral("preferred_subtitle_language"), QStringLiteral("portable_track_choices"), QStringLiteral("subtitle_delay")},
            QStringLiteral("qml/PlayerTrackPrefs.js"),
            QStringLiteral(""),
            QStringLiteral("getPref(jsonText, showKey)"),
            QStringLiteral("upsertPref(jsonText, showKey, patch, nowMs)"),
            QStringLiteral("owning QML Settings/property seam must trigger export; JS helper itself is pure"),
            17,
            QStringLiteral(""),
            QStringLiteral("Allowlisted fields include audio/subtitle language, portable track labels, subtitlesOff, audioDelay and subDelay.")
        },
        SyncOwnershipEntry{
            QStringLiteral("theatre_subtitle_appearance"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("subtitle_appearance")},
            QStringLiteral("player/Theatre QML settings surfaces; exact durable owner not frozen in current evidence"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            17,
            QStringLiteral(""),
            QStringLiteral("Approved sync category; 5B blocked until exact live persistence owner is inspected.")
        },
        SyncOwnershipEntry{
            QStringLiteral("theatre_row_customization"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("row_order"), QStringLiteral("row_hide_show"), QStringLiteral("row_rename")},
            QStringLiteral("qml/TheatreRowPreferences.qml"),
            QStringLiteral(""),
            QStringLiteral("valueFor(pageKey)"),
            QStringLiteral("move/toggleHidden/rename/reset"),
            QStringLiteral("changed(pageKey) after real mutation"),
            17,
            QStringLiteral(""),
            QStringLiteral("Qt Settings category theatreCatalogRows stores order/hidden/renamed per movies/shows/anime tab.")
        },
        SyncOwnershipEntry{
            QStringLiteral("theatre_watched_history"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Absent,
            false,
            QStringList{QStringLiteral("theatre_watched_history")},
            QStringLiteral(""),
            QStringLiteral("reference HistoryStore is not live-repo authority"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            14,
            QStringLiteral(""),
            QStringLiteral("Resolved through the same full-history discovery gap; do not reuse Continue as watched history.")
        },
        SyncOwnershipEntry{
            QStringLiteral("extension_roster"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Confirmed,
            true,
            QStringList{QStringLiteral("installed_extension_identity"), QStringLiteral("extension_list"), QStringLiteral("extension_order"), QStringLiteral("enabled_state")},
            QStringLiteral("native/engine/ExtensionsStore.h"),
            QStringLiteral(""),
            QStringLiteral("installed() / isInstalled(id)"),
            QStringLiteral("remove(id) / setEnabled(id,on) / moveTo(id,index)"),
            QStringLiteral("revision + changed()"),
            19,
            QStringLiteral(""),
            QStringLiteral("Only logical extension id/list/order/enabled state is ordinary-sync eligible. Raw transportUrl is explicitly denied by the 5A firewall.")
        },
        SyncOwnershipEntry{
            QStringLiteral("extension_safe_config"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("extension_account_state"), QStringLiteral("explicitly_safe_non_secret_configuration")},
            QStringLiteral("extension-specific configuration owners; no unified safe-config export seam confirmed"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            19,
            QStringLiteral(""),
            QStringLiteral("Only explicitly safe non-secret configuration may sync. Each field requires an owner and allowlist before 5B registration.")
        },
        SyncOwnershipEntry{
            QStringLiteral("vault_identity_decisions"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("media_identity_decisions")},
            QStringLiteral("native/engine/VaultIdentity.*"),
            QStringLiteral(""),
            QStringLiteral("pendingCeremonies()/pathAliases()/resolve()/knows()"),
            QStringLiteral("decideCeremony()/reconcile()/observeFile()"),
            QStringLiteral("changed()"),
            18,
            QStringLiteral(""),
            QStringLiteral("Current identity registry stores paths and derives ids from normalizedPath::size::mtimeMs. Slice 18 must define a portable decision record that strips path/file facts.")
        },
        SyncOwnershipEntry{
            QStringLiteral("desired_download_intent"),
            SyncDisposition::Syncable,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("current_desired_downloads")},
            QStringLiteral("native/player/downloadstore.* is current acquisition/queue owner, not a dedicated portable desired-intent authority"),
            QStringLiteral(""),
            QStringLiteral("jobs()/downloadedVideos() are local acquisition state, not approved desired-intent snapshot"),
            QStringLiteral("enqueue/cancel/remove are local acquisition actions"),
            QStringLiteral("queueRevision/changed/libraryChanged"),
            20,
            QStringLiteral(""),
            QStringLiteral("Slice 20 must introduce/confirm desired-download intent separate from queue, URLs, paths, bytes, and historical acquisition transactions.")
        },
        SyncOwnershipEntry{
            QStringLiteral("account_password"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("account_password")},
            QStringLiteral("account authentication input/service only"),
            QStringLiteral("AccountController never exposes password as Q_PROPERTY"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Never enters ordinary sync records.")
        },
        SyncOwnershipEntry{
            QStringLiteral("recovery_key"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("recovery_key")},
            QStringLiteral("one-time AccountRecoveryKeyPresenter/service recovery flow"),
            QStringLiteral("transient one-time presenter"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Never enters ordinary sync records or diagnostics.")
        },
        SyncOwnershipEntry{
            QStringLiteral("access_token"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("access_token")},
            QStringLiteral("AccountClient volatile session"),
            QStringLiteral("AccountClient volatile session"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Never enters ordinary sync records.")
        },
        SyncOwnershipEntry{
            QStringLiteral("refresh_token"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("refresh_token")},
            QStringLiteral("Windows Credential Manager active/pending-revocation credential slots"),
            QStringLiteral("WindowsAccountCredentialStore"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Never enters ordinary sync records.")
        },
        SyncOwnershipEntry{
            QStringLiteral("extension_credentials"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("extension_credentials")},
            QStringLiteral("extension-specific credential stores"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            19,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Approved post-Fable clarification: extension/API credentials do not cloud-sync in v1.")
        },
        SyncOwnershipEntry{
            QStringLiteral("api_credentials"),
            SyncDisposition::Secret,
            SyncOwnerStatus::Partial,
            false,
            QStringList{QStringLiteral("api_credentials")},
            QStringLiteral("provider/extension-specific credential stores"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("secret_requires_protected_channel"),
            QStringLiteral("Ordinary sync denied. No protected secret-sync design is adopted in v1.")
        },
        SyncOwnershipEntry{
            QStringLiteral("search_history"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("search_history")},
            QStringLiteral("native/SearchHistoryStore.h"),
            QStringLiteral("ProfileStoreRuntime -> SearchHistoryStore"),
            QStringLiteral("list(scope)"),
            QStringLiteral("record/remove/clear(scope,...)"),
            QStringLiteral("revision + changed(scope)"),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Hard product rule: search history never syncs.")
        },
        SyncOwnershipEntry{
            QStringLiteral("shell_session_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("open_sessions"), QStringLiteral("active_session"), QStringLiteral("session_saved_state")},
            QStringLiteral("native/SessionStore.h"),
            QStringLiteral(""),
            QStringLiteral("get/list/groups/activeId"),
            QStringLiteral("openOrSwitch/switchTo/close/saveState"),
            QStringLiteral("revision/changed/activeChanged"),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Open-session/savedState/taskbar model is machine/session state.")
        },
        SyncOwnershipEntry{
            QStringLiteral("window_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("window_geometry"), QStringLiteral("window_mode")},
            QStringLiteral("native/player/windowmodestore.* + windowstatepolicy.* and shell/window owners"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Window geometry/mode is machine state.")
        },
        SyncOwnershipEntry{
            QStringLiteral("pip_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("pip_state")},
            QStringLiteral("player/window shell state"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Picture-in-picture/window mode remains device-local.")
        },
        SyncOwnershipEntry{
            QStringLiteral("cast_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("cast_state")},
            QStringLiteral("native/player/caststore.*"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Cast target/session state is machine/LAN-owned.")
        },
        SyncOwnershipEntry{
            QStringLiteral("room_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("room_state"), QStringLiteral("watch_party_state")},
            QStringLiteral("native/player/roomstore.*"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Room/watch-party state is not account sync.")
        },
        SyncOwnershipEntry{
            QStringLiteral("filesystem_paths"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("filesystem_paths"), QStringLiteral("machine_paths")},
            QStringLiteral("multiple owners: Vault, Biblio, downloads, media/session descriptors"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Absolute/relative machine filesystem paths are rejected at payload validation.")
        },
        SyncOwnershipEntry{
            QStringLiteral("physical_media"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("media_files")},
            QStringLiteral("Vault/download/reader/player filesystem owners"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Actual media files are never uploaded as account sync.")
        },
        SyncOwnershipEntry{
            QStringLiteral("media_blobs"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("media_blobs"), QStringLiteral("binary_media_payloads")},
            QStringLiteral("media/image/cache/download subsystems"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Binary/media payloads are excluded from ordinary sync records.")
        },
        SyncOwnershipEntry{
            QStringLiteral("download_queue"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("download_queue"), QStringLiteral("download_job_state")},
            QStringLiteral("native/player/downloadstore.*"),
            QStringLiteral(""),
            QStringLiteral("jobs()/status()"),
            QStringLiteral("enqueue/cancel/retry/pause/resume"),
            QStringLiteral("queueRevision + changed()"),
            20,
            QStringLiteral("category_local_only"),
            QStringLiteral("Queue/acquisition state includes URLs, paths, byte counts and machine-local work.")
        },
        SyncOwnershipEntry{
            QStringLiteral("download_acquisition_history"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("download_acquisition_history"), QStringLiteral("source_transaction_history")},
            QStringLiteral("native/player/downloadstore.* and world downloader state"),
            QStringLiteral(""),
            QStringLiteral("downloadedVideos()/local indexes"),
            QStringLiteral("remove/download lifecycle"),
            QStringLiteral("libraryChanged"),
            20,
            QStringLiteral("category_local_only"),
            QStringLiteral("Spec syncs current desired downloads, never exact historical acquisition/source transactions.")
        },
        SyncOwnershipEntry{
            QStringLiteral("cache_state"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("caches")},
            QStringLiteral("comicreader/page caches, catalogue caches, player/media caches"),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral(""),
            26,
            QStringLiteral("category_local_only"),
            QStringLiteral("Rebuildable caches remain device-local.")
        },
        SyncOwnershipEntry{
            QStringLiteral("vault_path_aliases"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("vault_path_aliases"), QStringLiteral("vault_file_facts")},
            QStringLiteral("native/engine/VaultIdentity.*"),
            QStringLiteral(""),
            QStringLiteral("pathAliases()"),
            QStringLiteral("reconcile()/observeFile()"),
            QStringLiteral("changed()"),
            18,
            QStringLiteral("category_local_only"),
            QStringLiteral("oldPath/newPath/path aliases and FileFacts never enter ordinary sync.")
        },
        SyncOwnershipEntry{
            QStringLiteral("extension_transport_endpoint"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("raw_extension_transport_endpoint")},
            QStringLiteral("native/engine/ExtensionsStore.h raw transportUrl field"),
            QStringLiteral(""),
            QStringLiteral("installed()"),
            QStringLiteral("installation/source ownership"),
            QStringLiteral("revision + changed()"),
            19,
            QStringLiteral("category_local_only"),
            QStringLiteral("Raw transport endpoint is not the portable roster identity; may contain machine/private or credential-bearing data.")
        },
        SyncOwnershipEntry{
            QStringLiteral("biblio_local_book_identity"),
            SyncDisposition::LocalOnly,
            SyncOwnerStatus::Confirmed,
            false,
            QStringList{QStringLiteral("absolute_path_derived_book_identity")},
            QStringLiteral("native/reader/BookStores.h keyFor(absPath)"),
            QStringLiteral(""),
            QStringLiteral("keyFor(absPath)"),
            QStringLiteral("path-derived key"),
            QStringLiteral(""),
            16,
            QStringLiteral("category_local_only"),
            QStringLiteral("Current SHA1 key is derived from normalized absolute path and is not a portable sync identity.")
        }
    };
    return value;
}
}

const QList<SyncOwnershipEntry> &
SyncOwnershipInventory::all() {
    return entries();
}

const SyncOwnershipEntry *
SyncOwnershipInventory::find(
    const QString &id) {
    const QString normalized =
        id.trimmed().toLower();

    const QList<SyncOwnershipEntry> &items =
        entries();
    for (const SyncOwnershipEntry &entry : items) {
        if (entry.id == normalized)
            return &entry;
    }

    return nullptr;
}

QString SyncOwnershipInventory::dispositionName(
    SyncDisposition disposition) {
    switch (disposition) {
    case SyncDisposition::Syncable:
        return QStringLiteral("syncable");
    case SyncDisposition::Secret:
        return QStringLiteral("secret");
    case SyncDisposition::LocalOnly:
        return QStringLiteral("local-only");
    }
    return QString();
}

QString SyncOwnershipInventory::ownerStatusName(
    SyncOwnerStatus status) {
    switch (status) {
    case SyncOwnerStatus::Confirmed:
        return QStringLiteral("confirmed");
    case SyncOwnerStatus::Partial:
        return QStringLiteral("partial");
    case SyncOwnerStatus::Absent:
        return QStringLiteral("absent");
    }
    return QString();
}

QString SyncOwnershipInventory::inspectionBaseCommit() {
    return QStringLiteral("e2ec3416bb706f324a24a004deace05c5a026edc");
}
