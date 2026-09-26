# Theatre surface — section agreement (pilot)

[Agent 0 (Claude)] · web half: Claude · native half: Codex A (`TheatreFeed`). Visual authority: the Theatre ×
Portico halfway mock (`agents/colosseum-theatre-portico-halfway-mock.html`).

Theatre uses only shared records (`Item`, `Choice`); no page-owned `data` schemas. The page is laid out by
**section id**, so native controls the content and web controls where each section goes.

## Page layout

```
┌ theatre.featured      hero carousel     ┐  "top region" — shown above the tab bar on EVERY tab.
├ theatre.nextUp        rail              │  Native sends these two in every tab's `world` reset
│                                         │  (cached, so a tab switch doesn't refetch them).
├ Continue Watching     ← `continue` feed {scope:"Theatre"} (not part of `world`)
├ [ Discover · Movies · Shows · Anime · Library ]   tab bar (docks under the TopBar)
└ tab pane: every other section of the current tab, in index order
```

## Section ids per tab (`world` feed, `{world:"Theatre", tab}`)

| Tab | Sections (in order) | Notes |
|---|---|---|
| all tabs | `theatre.featured` (hero), `theatre.nextUp` (rail) | nextUp items open with intent `nextUp`; use `subtitle` for "S2 · E5" |
| discover | `theatre.discover.<shelf>` rails / grid | today: movies, shows, anime |
| movies / shows / anime | `theatre.<tab>.top10` (rail, 10 items, **rendered with rank numerals**), `theatre.<tab>.<shelf>` rails with `seeAll`, `theatre.<tab>.genres` (`tiles` of genre `Choice`s with Route targets) | top10 is presentation-only: web adds the numerals |
| library | `theatre.library` (grid of the user's saved titles) | web filters All / Movies / Shows / Anime locally by `Item.kind` |

Anything else that arrives renders in the pane in index order, so native can add shelves without a web change.
