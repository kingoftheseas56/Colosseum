#pragma once

// Restart-safe intent ledger for Tankoban volume-mode torrent downloads.
//
// One ROW per requested volume (keyed by its stable volumeId). A row records
// everything the transport needs to resume a download after a process restart:
// the shared torrent infoHash + magnet, the series/volume identity, the save
// path, the picked file index once metadata resolves, and the current state.
//
// State machine (a row NEVER leaves the ledger; it just advances):
//   awaiting_metadata -> downloading -> validating -> completed
//                                    \-> failed
//   (any live state)                  \-> cancelled
// active() returns only the non-terminal rows (awaiting_metadata / downloading /
// validating) so a fresh downloader can REPLAY exactly the intents still in
// flight. Persistence is atomic via QSaveFile so a crash mid-write can never
// corrupt the ledger.

#include <QList>
#include <QString>

namespace MangaTankoban {

struct VolumeRequestRow {
    QString volumeId;
    QString infoHash;
    QString magnetUri;
    QString seriesId;
    QString volumeNumber;
    QString savePath;
    int     pickedFileIndex = -1;
    // Arc 18 M6: when the request came from a verified index mapping, the
    // persisted identity expectation (exact fileIndex + path) the resume must
    // re-confirm against live metadata before any payload resumes. -1/empty =
    // an ordinary discovery request with no expectation.
    int     expectedFileIndex = -1;
    QString expectedFilePath;
    QString state;  // awaiting_metadata / downloading / validating / completed / failed / cancelled
};

class MangaVolumeRequestLedger {
public:
    explicit MangaVolumeRequestLedger(const QString& path);

    // Add a new row or replace the existing row with the same volumeId, then
    // persist atomically.
    void upsert(const VolumeRequestRow& row);
    // Advance a row's state (found by volumeId) and persist.
    void setState(const QString& volumeId, const QString& state);
    // One write that records the resolved file index AND the downloading state.
    void markDownloading(const QString& volumeId, int pickedFileIndex);

    // Snapshots (in insertion order).
    QList<VolumeRequestRow> all() const { return m_rows; }
    QList<VolumeRequestRow> active() const;  // non-terminal rows only
    bool contains(const QString& volumeId) const;
    VolumeRequestRow row(const QString& volumeId) const;

    // Re-read the on-disk file, discarding the in-memory snapshot. This is how a
    // second ledger instance (or the same one after a restart) observes what
    // another instance persisted.
    void reload();

    static bool isTerminal(const QString& state);

private:
    int indexOf(const QString& volumeId) const;
    void persist() const;

    QString m_path;
    QList<VolumeRequestRow> m_rows;
};

} // namespace MangaTankoban
