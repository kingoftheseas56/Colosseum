#pragma once

// Metadata-aware manga volume file picker. Given the requested volume and a
// torrent's REAL engine metadata (the QJsonArray shape emitted by
// TorrentEngine::metadataReady / TorrentEngine::torrentFiles — each element an
// object with "index", "name", "size"), isolate the single archive that IS that
// volume. It refuses when the volume cannot be honestly isolated: no comic
// archive present, the target is absent, two candidates match equally, or the
// only match is an inseparable multi-volume combined archive.
//
// Self-contained by design (spec: do NOT couple to MangaNyaaSource). The volume
// coverage grammar mirrors the Nyaa source's detectCoverage() — explicit
// v / vol / volume markers plus inclusive ranges — reimplemented locally so this
// picker owns its own parsing.

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace MangaVolumeFilePicker {

enum class PickFailure {
    None,           // a single archive was isolated
    NoArchive,      // no .cbz/.cbr/.cb7/.cbt candidate at all
    TargetMissing,  // archives present, none covers the requested volume
    Ambiguous,      // two distinct archives each exactly match the target
    CombinedArchive // the only cover is an inclusive multi-volume range archive
};

struct MangaVolumePick {
    int index = -1;               // engine file index of the chosen archive, -1 on failure
    QString path;                 // its "name" (relative path) from the metadata
    qint64 size = 0;              // its "size" in bytes from the metadata
    PickFailure failure = PickFailure::None;
};

// Choose the single archive that isolates `target` from `files`. `files` is the
// engine metadata array (index/name/size element shape). On failure the returned
// pick has index == -1 and a non-None failure explaining why.
MangaVolumePick pick(const QString& target, const QJsonArray& files);

// Merge chosen file indices into a libtorrent file-priority vector of length
// `fileCount`: priority 7 (max) at each picked index, 0 (do-not-download)
// everywhere else. Out-of-range indices are ignored. This is a UNION so a
// torrent shared across several picked volumes downloads all of them.
QVector<int> unionPriorities(const QVector<int>& picks, int fileCount);

} // namespace MangaVolumeFilePicker
