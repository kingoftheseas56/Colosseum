// Manga volume file-picker contract: from a torrent's REAL engine metadata
// (TorrentEngine::metadataReady element shape — index/name/size), isolate the
// single archive that IS the requested volume. Combined multi-volume archives
// can't be split, two equal candidates need another source, and directory-named
// volumes count only when they're the sole cover. unionPriorities merges the
// chosen file indices into a libtorrent priority vector (7 = max, 0 = skip).
#include "torrent/MangaVolumeFilePicker.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cstdlib>
#include <iostream>

using namespace MangaVolumeFilePicker;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Build the engine's metadata QJsonArray element shape (index/name/size) from a
// bare list of file names, defaulting each to a plausible archive byte size.
QJsonArray files(const QStringList& names)
{
    QJsonArray arr;
    int index = 0;
    for (const QString& name : names) {
        QJsonObject f;
        f["index"] = index++;
        f["name"]  = name;
        f["size"]  = static_cast<qint64>(48 * 1024 * 1024);
        arr.append(f);
    }
    return arr;
}
} // namespace

int main()
{
    // ── Pinned contract ──────────────────────────────────────────────────────
    require(pick("2", files({"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"})).index == 1,
            "target file selected from pack");
    require(pick("2", files({"Series Volumes 1-3.cbz"})).index == -1,
            "inseparable combined archive rejected");
    require(pick("2", files({"Series v02.cbz", "Series Volume 02.cbr"})).index == -1,
            "equal exact candidates require another source");
    require(unionPriorities({0, 2}, 4) == QVector<int>({7, 0, 7, 0}),
            "shared torrent priorities are a union");

    // ── PickFailure reasons ──────────────────────────────────────────────────
    require(pick("2", files({"cover.jpg", "notes.txt"})).failure == PickFailure::NoArchive,
            "no comic archive present → NoArchive");
    require(pick("2", files({"Series v05.cbz"})).failure == PickFailure::TargetMissing,
            "archive present but target volume absent → TargetMissing");
    require(pick("2", files({"Series Volumes 1-3.cbz"})).failure == PickFailure::CombinedArchive,
            "single inclusive-range archive → CombinedArchive");
    require(pick("2", files({"Series v02.cbz", "Series Volume 02.cbr"})).failure == PickFailure::Ambiguous,
            "two distinct exact matches → Ambiguous");

    // ── Happy paths ──────────────────────────────────────────────────────────
    {
        const MangaVolumePick p =
            pick("2", files({"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"}));
        require(p.failure == PickFailure::None, "clean pick reports no failure");
        require(p.path == QStringLiteral("Series v02.cbz"), "pick carries the chosen path");
        require(p.size == static_cast<qint64>(48 * 1024 * 1024), "pick carries the chosen size");
    }
    {
        // Directory-only coverage: the volume number lives in the parent dir,
        // not the file name. Picked because it is the sole cover of vol 2.
        const MangaVolumePick p = pick("2", files({"Series Vol 2/001.cbz"}));
        require(p.index == 0, "directory-only volume picked when sole cover");
        require(p.failure == PickFailure::None, "directory-only sole cover reports no failure");
    }
    // A file-name volume must outrank directory-only evidence when both are present.
    require(pick("2", files({"Series Vol 3/Series v02.cbz"})).index == 0,
            "file-name volume outranks its parent directory label");

    // ── unionPriorities shapes ───────────────────────────────────────────────
    require(unionPriorities({}, 3) == QVector<int>({0, 0, 0}),
            "empty picks download nothing");
    require(unionPriorities({1}, 3) == QVector<int>({0, 7, 0}),
            "single pick sets exactly one max priority");

    // ── Arc 18 M1: string-safe identity through the shared grammar ──────────
    // The old picker converted the target with toInt(), so a canonical "10.5"
    // volume could never be isolated even when the file named it exactly.
    require(pick("10.5", files({"Series v10.cbz", "Series Vol 10.5.cbz"})).index == 1,
            "fractional volume isolated by exact filename");
    require(pick("1.5", files({"Series v1.5.cbz"})).index == 0,
            "fractional single-file pick");
    require(pick("10.50", files({"Series v10.5.cbz"})).index == 0,
            "zero-padded fractional target matches by value");
    require(pick("10.5", files({"Series v10.5.cbz", "Series Volume 10.5.cbr"})).failure
                == PickFailure::Ambiguous,
            "two equal fractional candidates stay ambiguous");
    // Named volumes: exact textual evidence only, fail closed otherwise.
    require(pick("Special", files({"Series Volume Special.cbz"})).index == 0,
            "named volume isolated by exact filename");
    require(pick("Special", files({"Series v02.cbz"})).failure == PickFailure::TargetMissing,
            "named target absent -> TargetMissing, never coerced to a number");
    // Fractional target inside an integer range is still a combined blob.
    require(pick("2.5", files({"Series Volumes 1-3.cbz"})).failure
                == PickFailure::CombinedArchive,
            "fractional target vs combined archive -> CombinedArchive");

    std::cout << "MANGA_VOLUME_FILEPICKER_OK\n";
    return 0;
}
