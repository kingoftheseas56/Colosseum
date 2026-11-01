# Cover-flow allocation, re-evaluated under the selected rebuilt header

Measured 2026-11-01 in headless Chromium 153 on Linux by `candidate/verify/focused-b1.mjs`; raw numbers are in `focused-candidate.json` → `flowAllocation`. These are HTML geometry measurements of the mock, not Qt/Android measurements.

## What is fixed in both variants

The flow is the source flow (`qml/MangaTankobanLibrary.qml` at the pin, lines 548–600): `bookHeight = max(190, min(276, floor((flowViewport − 54 − 16) / 1.10)))`, `bookWidth = round(bookHeight × 2/3)`, centre scale 1.10, neighbours 1.00 / 0.95 / 0.90 / 0.86, centred by index. The checks confirmed a 1.10 drawn focus scale and the exact falloff in every case. The surrounding header is now the Hemanth-selected rebuilt masthead under the real TitlePageBar, rendered by the same code as the main series mock.

## The two allocations

- **A · source original fit** (`volume-flow-original-fit.html`): the page does not scroll; the masthead keeps its height and the flow takes what is left. This is how the desktop QML composes it. Where too little height is left, the 190 px floor holds and the focused cover is clipped, exactly as the formula implies.
- **B · page scroll** (`volume-flow-minimal-alternative.html`): the whole title page scrolls, as in the rebuilt preview Hemanth viewed. The flow keeps a fixed 294 px stage on phones, so the floor and focus scale always fit. This is still a proposal.

## Measurements

"Raw" is the formula before the 190 floor. "Fits" means the focused card is wholly inside the flow and, for A, on the first screen. "Scroll" is how far the page must scroll to show the focused card and its action row. "Below bar" is the height the library (flow plus action row) needs, against the height left under the pinned 76 px bar.

| Viewport | Masthead bottom | A: flow / raw / book | A fits | B: book | B scroll | B below bar |
|---|---|---|---|---|---|---|
| 412×900 | 533 | 193 / 111 / 190 | no (clipped) | 203 | 102 px | 385 / 802 ✓ |
| 900×412 | 348 | 0 / −64 / 190 | no (no room) | 203 | 405 px | 385 / 336 ✗ |
| 354×792 | 533 | 85 / 13 / 190 | no | 203 | 210 px | 385 / 694 ✓ |
| 792×354 | 348 | 0 / −64 / 190 | no | 203 | 463 px | 385 / 278 ✗ |
| 354×792, 1.3×, 24 px insets | 549 | 8 / −57 / 190 | no | 203 | 286 px | 391 / 646 ✓ |
| 792×354, 1.3×, 24 px insets | 385 | 0 / −64 / 190 | no | 203 | 559 px | 391 / 230 ✗ |
| Tablet 800×1280 | 399 | 747 / 615 / 276 | yes | 276 | 0 | ✓ |
| Tablet 1280×800 | 399 | 278 / 189 / 190 | yes | 190 | 11 px | ✓ |
| Desktop 1280×700 | 333 | 278 / 189 / 190 | yes | 190 | 45 px | ✓ |

## Reading

1. Under the selected header, **A fails on every phone viewport**, including plain 412×900 portrait. The rebuilt masthead alone is about 533 px tall in portrait and 348 px in landscape, which leaves no room for a 190 px cover at 1.10. Shrinking covers below 190 px would break the source floor, so A cannot be made to fit on phones without new UI. A works on tablets and desktop.
2. **B keeps the floor and focus scale everywhere.** Its cost is vertical scrolling: 102 px in 412×900 portrait, and 405–559 px in phone landscape.
3. **In phone landscape, B cannot show the focused cover, its caption and the action row at once below the pinned bar** (385 px needed, 230–336 px available). The user scrolls between cover and actions. If the bar scrolled away with the page instead of staying pinned, 900×412 would fit (385 ≤ 412) but 792×354 still would not. That bar behaviour is not implemented; it would be a separate decision.
4. Desktop at 1280×700 already sits on the floor (raw 189). That is a property of the rebuilt masthead at that height, not of the phone port.

## Decision needed (Hemanth)

Choose B (page scroll, as in the viewed preview) or keep A (tablets/desktop only, with a phone exception still to be designed). The main series mock currently uses B because it is what the selected preview did. That is a presentation default, not an approval. The older Pro comparison under the superseded header is preserved unchanged in `baseline/`.
