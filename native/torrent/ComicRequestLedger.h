#pragma once

// Restart-safe intent ledger for Tankorent Comic collected-edition torrent
// downloads (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-
// comic-volume-mode-design.md, "Durable shared-infohash transport" ->
// ComicRequestLedger). Ports the proven MangaVolumeRequestLedger discipline
// (native/torrent/MangaVolumeRequestLedger.h) to comics: one ROW per
// requested collected edition, keyed by its stable catalog editionId (chId).
// A row records everything Task 9's shared-infohash downloader needs to
// resume after a process restart: the shared torrent infoHash + magnet, the
// canonical edition identity (series/format/ordinal/ISBN/collected issues),
// the save path, the resolved payload selection, and the current state.
//
// State machine (a row NEVER leaves the ledger; it just advances):
//   awaiting_metadata -> downloading -> assembling -> publishing -> completed
//                                          \-> failed        \-> failed
//   (any live state) -------------------------------------------> cancelled
// active() returns only the non-terminal rows (state not in {completed,
// failed, cancelled}) AND only those with a well-formed 40-hex infoHash, so a
// fresh downloader replays exactly the intents still safely resumable.
//
// Persistence is atomic via QSaveFile (write-temp-then-commit) so a crash
// mid-write can never corrupt the journal. The file is a versioned JSON
// object (schemaVersion()); a version mismatch is ignored with a diagnostic
// rather than partially applied, and a structurally broken row is quarantined
// (dropped) rather than partially loaded.

#include "ComicEditionFileSelector.h"
#include "ComicEditionIdentity.h"

#include <QList>
#include <QString>

struct ComicEditionRequestRow {
    QString editionId;
    QString infoHash;
    QString magnetUri;
    QString seriesId;
    QString seriesTitle;
    QString editionTitle;
    ComicEditionIdentity::ComicCollectionFormat format = ComicEditionIdentity::ComicCollectionFormat::Unknown;
    int     ordinal = -1;
    QString isbnDigits;
    QList<ComicEditionIdentity::ComicIssueRef> collectedIssues;
    // Identity-safety flags — persisted so a restart re-derives the SAME
    // safe/unsafe verdict. Without them, a partially-parsed collected-issue set
    // would look "complete" on replay and could auto-download a subset it
    // should have held back. Both default to the SAFE side (hold back / force
    // manual) when a pre-fix journal row lacks the key.
    bool collectedIssuesComplete = false;
    bool formatAmbiguous = false;
    QString savePath;
    QList<int> pickedFileIndices;   // may hold several (issue-range assembly)
    ComicEditionFileSelector::ComicPayloadKind payloadKind = ComicEditionFileSelector::ComicPayloadKind::None;
    QString state;   // awaiting_metadata / downloading / assembling / publishing / completed / failed / cancelled
};

class ComicRequestLedger {
public:
    // `path` is the JSON journal file; the caller supplies it (production
    // uses <AppDataLocation>/comics-torrent/edition-requests.json). No I/O
    // happens until load() or a mutating call.
    explicit ComicRequestLedger(const QString& path);

    // (Re)reads the on-disk journal, replacing the in-memory snapshot. An
    // absent file is the normal first-run case (empty ledger, no warning). A
    // version mismatch or corrupt file is ignored with a diagnostic, leaving
    // an empty ledger rather than crashing or partially applying. Individual
    // malformed rows within an otherwise valid file are quarantined (dropped)
    // rather than partially loaded; a duplicate editionId within the file
    // keeps the LAST occurrence's data at the FIRST occurrence's position.
    void load();

    // Non-terminal rows (state not in {completed, failed, cancelled}) whose
    // infoHash is a well-formed 40-hex-character string. A row with a
    // malformed infoHash is excluded here (it is not safely resumable) but
    // remains visible via all().
    QList<ComicEditionRequestRow> active() const;
    // Every row currently held in memory, in stable insertion order.
    QList<ComicEditionRequestRow> all() const;

    // Inserts a new row or replaces the existing row with the same
    // editionId, then persists atomically.
    void upsert(const ComicEditionRequestRow& row);
    // Updates the resolved payload selection for a row (found by editionId)
    // and persists. No-op if the row is absent.
    void setSelection(const QString& editionId, const QList<int>& pickedFileIndices,
                       ComicEditionFileSelector::ComicPayloadKind kind);
    // Advances a row's state (found by editionId) and persists. No-op if the
    // row is absent.
    void setState(const QString& editionId, const QString& state);
    // Removes a row (found by editionId) entirely and persists. No-op if the
    // row is absent.
    void remove(const QString& editionId);

    static int schemaVersion();   // 1

private:
    int indexOf(const QString& editionId) const;
    void persist() const;

    QString m_path;
    QList<ComicEditionRequestRow> m_rows;
};
