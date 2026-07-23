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

#include <optional>

namespace comicreader {

enum class Mode { LongStrip, DoublePage };
enum class Direction { Ltr, Rtl };
enum class PageError { None, MissingFile, DecodeFailed, UnsupportedImage };
enum class CouplingMode { Auto, Manual };
enum class CouplingPhase { Normal, Shifted };

// One source page in an entry. `detectedSpread` is the decoder's geometry verdict
// (see spreadRatioExceeded); `spreadOverride`, when present, is the user's manual
// mark and always beats detection. Pairing consumes these; it never decodes.
struct PageMeta {
    int index = -1;
    QString localPath;
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
