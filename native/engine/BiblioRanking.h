#pragma once

#include "BiblioCatalogTypes.h"

#include <QString>
#include <QList>
#include <QVector>
#include <QDateTime>

// Deterministic, pure ranking functions for the Discover shelves (spec 7).
//
// These functions never consider acquisition availability, source-health,
// public-domain status, ownership, or local reading activity — those are out of
// scope for ranking by design. Missing evidence drops only the affected signal,
// never the work from an unrelated shelf. Everything is a pure function of the
// inputs so the ranking is reproducible and testable.
namespace BiblioRanking {

// Rank `works` for a single Discover shelf.
//   catalogId : one of "popular", "top-rated", "new-releases", "trending",
//               "most-read", "classics". Any other id (including "") returns
//               an empty vector. "most-read"/"classics" keep any payload order
//               the seeder attached (Open Library readinglog / year-gated
//               search already rank them) instead of re-scoring locally.
//   works     : the candidate population (also the source of returned rows).
//   history   : dated demand snapshots; used only by "trending".
//   nowUtc    : anchors the trailing-12-month ("new-releases") window.
// The result is ordered best-first, with canonical-id ascending as the
// deterministic tie-break.
QVector<BiblioWork> rank(const QString &catalogId,
                         const QList<BiblioWork> &works,
                         const QList<BiblioRankSnapshot> &history,
                         const QDateTime &nowUtc);

} // namespace BiblioRanking
