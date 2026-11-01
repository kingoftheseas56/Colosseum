# Batch 2 — finished runnable mocks

Open [index.html](index.html) to choose any of the eight pages, its original worker version and its before/after comparison. Corrected runtime files live only in this directory. The original workers, frozen references, batch README and INPUTS.json remain preserved.

## Running

Double-click index.html for the local workbench, or run **RUN-LOCAL.cmd** from this directory and open the printed localhost address. The command uses the already available Python runtime to serve the existing project tree on loopback port 8766. It installs nothing and builds nothing. Stop that local server with Ctrl+C.

Keep this directory in its assigned location:

`D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks`

The page groups resolve their approved artwork and fonts through `../../../../reference/`. Copying only finished-mocks to another location will break those paths; retain the surrounding project tree. The shared Collections picker is under shared/.

## What the pages contain

| Page | Fixture and finished behavior |
|---|---|
| Biblio book | Full book synopsis; Read chooser; seven expandable torrent rows; three editions and two audiobook rows; Library and transactional Collections. |
| Theatre film | Legacy/default source presentation; full phone synopsis; original actions; collection picker; removal confirmation; expanded cast and recommendation rail. |
| Theatre series | Same source branch, plus 23 season choices, episode order, jump to final episode, row actions/context, notifications and fitting sticky ledger. |
| Comic series | Original GetComics-style shelf; search, clear, sort, eight releases, local acquisition/read states and Collections. |
| Comic editions | Five editions and alternate sources; weak confirmation; inspecting, incomplete, combined and ambiguous archive outcomes. This is the original parked edition route, not a routing promotion. |
| Comic archives | Taxonomy board, index, archive-titled All releases shelf and Back restoration. The bundled Batman route opens the finished series page. |
| Saga universe | Seven stories, seven books and eight films; branches, Upcoming, full information; source Continue tile and keyboard grid. |
| Shelves universe | Bond's 25 films and 14 books; alternate Weekly Shōnen Jump fixture with 40 numbered rows, source column order and keyboard navigation. |

All media actions and downloads are local fixtures. Some actions intentionally log a route or source signal outside the app frame because their destination lies outside these eight pages. Nothing connects to a provider, account, reader, player, download engine or app service. In particular, Continue Remove reproduces the source signal: the frozen Saga/Shelves parents do not attach a removal handler, so the fixture remains visible.

## Workbench settings and reproducible URLs

The controls surrounding each app frame are review tools, not proposed product UI. Use each page's size selector for the exact reviewed dimensions. Normal sizes are 412×900, 354×792, 900×412, 792×354, desktop 1280×700 and tablets 800×1280 / 1280×800. Stress is 1.3 text with illustrative 24px top/bottom insets.

| Group | Parameters |
|---|---|
| Biblio | `?width=354&height=792&scale=1.3&insetTop=24&insetBottom=24`. Use the workbench Scenario selector for Read, Collections, expanded torrents and saved state. |
| Theatre | For exact browser-size capture: `?capture=1&scale=1.3&top=24&bottom=24`. States include `season-open`, `jump-open`, `episodes`, `context-open`, `removal-open`. `renderer=rebuilt` is an explicit comparison of the opt-in source branch. `linked=1` displays the source's optional linked-account removal choice as a fixture only. |
| Comics | `?capture=1&font=1.3&top=24&bottom=24`. Edition states include `alt`, `source-inspecting`, `source-incomplete`, `source-combined`, `source-ambiguous`, `source-weak`. Archive `state=index` opens its index. Noncapture custom dimensions use `w` and `h`. |
| Universes | `?v=narrow&scale=1.3&top=24&bottom=24`; presets are portrait, narrow, landscape, narrow-land, desktop, tablet and tablet-land. Add `fixture=jump` for the magazine. Add `capture=1` only when the actual browser viewport matches the requested preset. |

## Decision B2-018

Universe tabs default to the **proposed, unapproved** adaptation: keep their original beside-Back position when it fits; place the original-size tab track below Back on narrow phones. The workbench clearly labels the pending decision. Set `tabs=original` or use its selector to run the original source-geometry attempt. Set `tabs=proposed` to compare the fitting alternative.

Both versions, their measured obstacle and all four matched phone stress comparisons are linked from [the decision comparison](../comparisons/decision-B2-018.html). Hemanth's choice is required before applying that behavior to QML.

## Handoff

See [REVIEW.md](../REVIEW.md), [QML-PORTING-NOTES.md](../QML-PORTING-NOTES.md), [COVERAGE.csv](../COVERAGE.csv), [FINDINGS.csv](../FINDINGS.csv), [ASSETS-AND-PROVENANCE.md](../ASSETS-AND-PROVENANCE.md) and [REPORT.json](../REPORT.json).

Screenshots are Chrome HTML evidence. They are not native app screenshots or device qualification. Exact item jackets/posters are absent from the frozen input; existing actual project artwork is retained and identified as a stand-in.
