// native/comicreader/ComicReaderPairing.h
//
// Pure canonical double-page pairing for the Comic Reader (Agent 1, plan
// 2026-07-23). Ported from Tankoban 2's proven reader as behaviour, not code:
// no QWidget, no QPixmap, no I/O — just the deterministic combinatorial walk.
#pragma once

#include "comicreader/ComicReaderTypes.h"

#include <QVector>

namespace comicreader {

// override > detection. No index special-casing lives here — the cover-alone
// decision belongs to buildUnits, which knows a page's position.
bool isSpread(const PageMeta& m);

// The canonical pairing law (ported exactly from Tankoban 2's buildTwoPagePairs
// and QTGroundWork's canonical-pairing walk — both agree):
//  - The cover (index 0) rides alone, UNLESS page 0 is itself a confirmed spread
//    (then it is a full-width spread unit, not coverAlone).
//  - A confirmed spread is one full-width unit; ONLY a spread consumes an extra
//    parity slot. A page whose pair-partner would be a spread simply stands alone
//    and does NOT shift parity (nudge/auto-coupling are the edge-case escapes).
//  - Phase Shifted adds +1 to parity but never pairs across a spread.
// Deterministic and pure.
QVector<PairUnit> buildUnits(const QVector<PageMeta>& pages, CouplingPhase phase);

// Index into `units` of the unit containing pageIndex; clamped to [0, size-1]
// (and 0 for empty units).
int unitForPage(const QVector<PairUnit>& units, int pageIndex);

} // namespace comicreader
