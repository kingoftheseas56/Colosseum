#pragma once

// Canonical value types for Tankoban "volume mode". A VolumeRecord is the app's
// single source of truth for one tankobon volume: a stable escaped id, the
// display fields lifted from the source row, and the ordered chapter ids that
// fall under it. Volume numbers are STRINGS on purpose — fractional (10.5) and
// special/named volumes must never be collapsed by a float round-trip.

#include <QList>
#include <QString>
#include <QStringList>

namespace MangaTankoban {

struct VolumeRecord {
    QString id;            // "tankoban:<escaped seriesId>:volume:<number>"
    QString seriesId;
    QString number;        // normalized string, e.g. "1", "10.5", "Extra"
    QString title;
    QString cover;
    QString chapterStart;  // optional chapter-range lower bound (source-provided)
    QString chapterEnd;    // optional chapter-range upper bound (source-provided)
    QStringList chapterIds;
    // True only when every in-range chapter that exists is mapped to this volume
    // (or, with no valid range, when explicit assignment gave it any chapter). The
    // WeebCentral fallback builds only from a COMPLETE map — a volume with 1 of 10
    // chapters is not buildable and must not be offered as such.
    bool chapterMapComplete = false;
};

struct SeriesSnapshot {
    QString seriesId;
    QString title;
    QString author;
    QStringList aliases;
    QList<VolumeRecord> volumes;
};

} // namespace MangaTankoban
