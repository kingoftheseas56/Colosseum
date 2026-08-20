#pragma once

// TankobanChapterMigration — the one-time, idempotent purge of WeebCentral-era chapter
// data (catalogue-independence Slice 5, 2026-08-20). Hemanth's explicit lock: chapters
// are deleted completely, on-disk bytes included — "we commit to the bit". Run once at
// boot from main.cpp, right after the COLOSSEUM_APPDATA_TAG block resolves (so it always
// operates on whichever root is live for THIS run — a tagged isolated test session or the
// real daily app — and never a stale one).
//
// Deliberately does NOT use a hardcoded org/app QSettings for its own idempotency marker
// (the ProgressStore.h store-isolation trap: a QSettings("Brotherhood","Colosseum")
// two-arg constructor resolves straight to the real registry hive regardless of the
// process's current applicationName, silently bypassing COLOSSEUM_APPDATA_TAG). This
// class only ever touches QStandardPaths::AppDataLocation directly — a plain marker FILE
// under that root — which already follows the active applicationName the same way every
// other AppData-backed store here does (MangaDownloader, MangaVolumeIndex, ...).
//
// Deletes:
//   <AppDataLocation>/manga/                    (chapter dirs + index.json — WC-era only)
//   ProgressStore records with kind == "manga"  (chapter-lane resume data)
// Leaves untouched:
//   <AppDataLocation>/manga-volumes/            (tankoban volume archives)
//   ProgressStore records with kind == "tankoban" / "comic"
//
// Idempotent via a marker file (<AppDataLocation>/tankoban-chapter-migration.v1.done):
// once written, every later run() call is a no-op (Result.ran stays false). The marker is
// written ONLY after a successful disk purge (or when there was nothing to delete), so a
// failed deletion (e.g. a locked file) is retried on the next boot instead of being
// silently forgotten.
//
// progressStoreIsDurable (closing-sweep fix, 2026-08-21): the account/profile runtime
// (Bundle 8C) boots EVERY run behind a Sealed placeholder ProgressStore backed by a
// fresh QTemporaryDir (ProfileStoreRuntime::createSealedStores) — a throwaway instance
// that is discarded, never the store QML's `Progress` ends up bound to once the user's
// onboarding choice ("continue local" / sign in) rebinds ProfileStoreRuntime to a real,
// durable profile. Purging kind:"manga" records from the Sealed placeholder purges
// nothing that will ever persist, and if the marker were written on that pass, the real
// store's manga-kind records would never be reached (the closing sweep, 2026-08-21,
// ground-truthed exactly this). The caller passes false while the bound store is Sealed;
// run() then does its disk-only pass and WITHHOLDS the marker so a later call — made
// once the caller's own rebind signal fires — gets the real store and the real chance to
// write it. Defaults to true so every existing disk-only/no-store caller and every
// already-durable-store caller is unaffected.

#include <QString>

class ProgressStore;

class TankobanChapterMigration
{
public:
    struct Result {
        bool ran = false;                 // false when the marker already existed (no-op)
        bool mangaDirExisted = false;      // <AppDataLocation>/manga/ existed before this run
        bool mangaDirDeleted = false;      // it existed AND removeRecursively() succeeded
        int  chapterDirsDeleted = 0;       // immediate child dirs under manga/ (one per series)
        bool indexDeleted = false;         // manga/index.json existed and was removed with it
        int  progressRecordsPurged = 0;    // kind:"manga" ProgressStore records removed
    };

    // appDataRoot: QStandardPaths::AppDataLocation for THIS run (tagged or real — the
    // caller resolves it, this class never calls QStandardPaths itself, so a Qt Test
    // fixture can hand it an arbitrary QTemporaryDir path with no QCoreApplication
    // identity dance required).
    // progress: may be null (a caller that only wants the disk purge exercised, e.g. a
    // disk-only fixture) — the ProgressStore purge step is skipped when null.
    // progressStoreIsDurable: false when `progress` is a Sealed/ephemeral placeholder —
    // see the class-comment note above. Ignored when progress is null.
    static Result run(const QString &appDataRoot, ProgressStore *progress,
                       bool progressStoreIsDurable = true);

private:
    static QString markerPath(const QString &appDataRoot);
    static bool deleteChapterTree(const QString &appDataRoot, Result &out);
    static int purgeMangaProgress(ProgressStore *progress);
};
