// native/comicreader/ComicReaderTypes.h
//
// Typed boundary for the from-scratch Comic Reader (Agent 1, plan 2026-07-23).
// Pure value types + QVariant (de)serialization for the manga/comic/Tankoban
// reader engine. No Qt GUI, no image decode, no cache, no I/O — this is the
// combinatorial foundation every native/comicreader/ unit builds on.
#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <optional>

namespace comicreader {

// Raise `target` to `value` if `value` is larger. Lock-free, and safe when
// several writers race — compare_exchange_weak reloads `seen` with whatever the
// winner stored, so the loop re-tests against the new truth.
//
// Lives in this neutral foundation header because BOTH image tiers publish a
// high-water mark through it, and the lower one (ComicReaderPageCache) must not
// have to depend on the higher one (ComicReaderScaleCache) for four lines of
// generic arithmetic that has nothing to do with scaling.
inline void raiseMax(std::atomic<quint64>& target, quint64 value) {
    quint64 seen = target.load(std::memory_order_relaxed);
    while (value > seen
           && !target.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

enum class Mode { LongStrip, DoublePage };
enum class Direction { Ltr, Rtl };
enum class PageError { None, MissingFile, DecodeFailed, UnsupportedImage };
enum class PageSourceKind { LocalFile, CbzEntry };
enum class CouplingMode { Auto, Manual };
enum class CouplingPhase { Normal, Shifted };

// One source page in an entry. `detectedSpread` is the decoder's geometry verdict
// (see spreadRatioExceeded); `spreadOverride`, when present, is the user's manual
// mark and always beats detection. Pairing consumes these; it never decodes.
struct PageMeta {
    int index = -1;
    PageSourceKind sourceKind = PageSourceKind::LocalFile;
    QString localPath;
    QString archivePath;
    QString archiveEntry;
    QSize sourceSize;
    bool decoded = false;
    bool detectedSpread = false;       // width >= 1.08 * height (or override)
    std::optional<bool> spreadOverride; // manual override beats detection
    PageError error = PageError::None;

    QVariantMap toVariantMap() const;
    static PageMeta fromVariantMap(const QVariantMap& map);
};

// One display unit in Double Page mode: a pair (both indices set), a single
// (leftIndex == -1), a full-width spread (spread == true), or the lone cover
// (coverAlone == true). Indices are -1 when absent.
struct PairUnit {
    int rightIndex = -1;
    int leftIndex = -1;
    bool spread = false;
    bool coverAlone = false;

    QVariantMap toVariantMap() const;
    static PairUnit fromVariantMap(const QVariantMap& map);
};

// House number (plan 2026-07-23): width >= 1.08 * height ⇒ landscape spread.
bool spreadRatioExceeded(const QSize& size);

// Stable snake_case wire codes for PageError:
// none / missing_file / decode_failed / unsupported_image.
QString pageErrorToString(PageError error);
PageError pageErrorFromString(const QString& code);

} // namespace comicreader
