#pragma once

// Arc 18 M2 — discovery planning owner (TARGET-ARCHITECTURE "Discovery inputs").
// Before Arc 18, Nyaa was searched with the canonical title only; aliases were
// validation needles that could VERIFY a candidate but never FIND one, so an
// alias-only release was invisible to discovery. This class plans the bounded,
// deduplicated query family the search fires: canonical-title variants first
// (highest confidence), then per-alias variants, capped so a series with many
// aliases cannot explode into an unbounded request storm.
//
// It is a pure planner by design — no network, no QObject, no state. The fired
// requests remain MangaNyaaSource's job (one network owner); the persisted
// identity remains MangaTorrentIndex's job. Discovery feeds the indexer; it
// never becomes a second transport.

#include "engine/MangaTankobanTypes.h"

#include <QString>
#include <QStringList>

namespace MangaTankoban {
namespace MangaTorrentDiscovery {

// Total query-family cap across canonical title + every alias. High-confidence
// canonical variants are always emitted first, so the cap trims alias tail, not
// the proven path. 8 keeps a merged Nyaa search bounded on this network.
constexpr int kMaxQueries = 8;

// Volume-aware family (canonical title variants = queryVariants() verbatim,
// then each alias's variants), deduped and capped. An empty volumeNumber
// naturally yields the bare-title family — the series-wide indexing shape.
QStringList queryFamily(const SeriesSnapshot& series, const QString& volumeNumber,
                        int cap = kMaxQueries);

} // namespace MangaTorrentDiscovery
} // namespace MangaTankoban
