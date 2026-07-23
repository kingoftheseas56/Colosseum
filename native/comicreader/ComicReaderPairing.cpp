// native/comicreader/ComicReaderPairing.cpp
#include "comicreader/ComicReaderPairing.h"

namespace comicreader {

bool isSpread(const PageMeta& m) {
    return m.spreadOverride.has_value() ? *m.spreadOverride : m.detectedSpread;
}

QVector<PairUnit> buildUnits(const QVector<PageMeta>& pages, CouplingPhase phase) {
    QVector<PairUnit> units;
    const int n = pages.size();
    if (n == 0)
        return units;

    const int nudge = (phase == CouplingPhase::Shifted) ? 1 : 0;

    // Cover unit (index 0): rides alone unless page 0 is itself a confirmed
    // spread, in which case it is a full-width spread unit. The cover anchors the
    // pairing parity, so it never contributes an extra slot.
    {
        PairUnit cover;
        cover.rightIndex = 0;
        if (isSpread(pages[0]))
            cover.spread = true;
        else
            cover.coverAlone = true;
        units.append(cover);
    }

    // Proven TB2 / QTGroundWork law: ONLY a confirmed spread consumes an extra
    // parity slot. A page forced to stand alone (its would-be partner is a
    // spread) does NOT compensate parity — the nudge (P) and auto-coupling are
    // the escape hatches for pairing edge cases, so the default behaves exactly
    // like the readers Hemanth already uses.
    int extraSlots = 0;
    int idx = 1;
    while (idx < n) {
        // Never pair across a spread: a confirmed spread is one full-width unit.
        if (isSpread(pages[idx])) {
            PairUnit u;
            u.rightIndex = idx;
            u.spread = true;
            units.append(u);
            ++extraSlots;
            ++idx;
            continue;
        }

        const int parity = (idx + extraSlots + nudge) % 2;
        if (parity == 1) {
            // idx opens a pair with idx+1 when that partner exists and is not a
            // spread; otherwise idx stands as an unpaired single.
            const int left = idx + 1;
            if (left < n && !isSpread(pages[left])) {
                PairUnit u;
                u.rightIndex = idx;
                u.leftIndex = left;
                units.append(u);
                idx += 2;
            } else {
                PairUnit u;
                u.rightIndex = idx;
                units.append(u);
                ++idx;
            }
        } else {
            PairUnit u;
            u.rightIndex = idx;
            units.append(u);
            ++idx;
        }
    }

    return units;
}

int unitForPage(const QVector<PairUnit>& units, int pageIndex) {
    if (units.isEmpty())
        return 0;
    // Explicit clamp BEFORE the scan: a negative index must never coincidentally
    // match a PairUnit's -1 sentinel (e.g. the cover's absent leftIndex). Keeps the
    // intent robust if that sentinel value ever changes.
    if (pageIndex < 0)
        return 0;
    for (int k = 0; k < units.size(); ++k) {
        const PairUnit& u = units[k];
        if (u.rightIndex == pageIndex || u.leftIndex == pageIndex)
            return k;
    }
    // Above the last page → clamp to the last unit (pages ascend across units).
    return units.size() - 1;
}

} // namespace comicreader
