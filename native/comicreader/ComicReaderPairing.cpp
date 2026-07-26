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

    // TWO leading singles anchor the pairing (Hemanth 2026-07-25). In a physical tankoban the
    // COVER (index 0) is a single leaf, AND the FIRST content page (index 1) is the lone recto that
    // faces the inside cover — it does not come in a set. Emitting BOTH as singles shifts every
    // following spread onto its true book parity (pages 3-4, 5-6, 7-8 ...) and lands a wide spread on
    // a pair boundary instead of orphaning its neighbours. Neither leading page adds an extra parity
    // slot (they ARE the anchor). Each is a full-width spread unit if it is itself a confirmed spread.
    {
        PairUnit cover;
        cover.rightIndex = 0;
        if (isSpread(pages[0]))
            cover.spread = true;
        else
            cover.coverAlone = true;
        units.append(cover);
    }
    if (n > 1) {
        PairUnit first;
        first.rightIndex = 1;
        if (isSpread(pages[1]))
            first.spread = true;
        // else a plain lone page (coverAlone stays false — it is the first page, not "the cover")
        units.append(first);
    }

    // Pairing begins at index 2, anchored so (2,3),(4,5),... pair by default (parity == 0). ONLY a
    // confirmed spread consumes an extra parity slot; a page forced alone (its partner is a spread)
    // does NOT compensate parity — the nudge (P) / auto-coupling are the escape hatches.
    int extraSlots = 0;
    int idx = 2;
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
        if (parity == 0) {
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
