#pragma once

// Pure logic for Tankoban volume mode: string-safe volume-number normalization,
// stable escaped identity (volume id + per-series settings key), and assembly of
// a SeriesSnapshot from QML-style QVariant snapshots. No Qt Quick / no I/O — this
// header is harness-testable on its own (see tests/manga_tankoban_logic_harness.cpp).

#include "engine/MangaTankobanTypes.h"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace MangaTankoban {

// Convert any QVariant to a canonical volume-number string: trimmed, leading
// zeroes stripped from the integer part (but "0" preserved), fractional and
// named/special suffixes kept intact. A double variant renders without
// trailing-zero noise (10.5 -> "10.5", 2.0 -> "2").
QString normalizeVolumeNumber(const QVariant& raw);

// Stable, string-safe id for a volume. The seriesId is percent-escaped so its
// internal ':' becomes "%3A"; the (already-normalized) volume number is not.
QString volumeId(const QString& seriesId, const QString& volumeNumber);

// Per-series settings key ("manga/tankobanMode/<escaped seriesId>").
QString settingsKey(const QString& seriesId);

// Assemble a canonical snapshot from a series descriptor plus volume/chapter
// rows. Every volume row is emitted as a record (never dropped for having no
// chapters). Chapters map to volumes first by an explicit "volume" field, then —
// for volumes still empty — by the volume row's chapterStart/chapterEnd range.
SeriesSnapshot prepareSeries(const QVariantMap& descriptor,
                             const QVariantList& volumeRows,
                             const QVariantList& chapterRows);

} // namespace MangaTankoban
