# LOCG fixtures — genre-axis findings (Task 1)

Captured 2026-07-09 from `https://leagueofcomicgeeks.com/comic/get_comics` (keyless, HTTP 200 JSON,
no Cloudflare challenge). 4 fixtures in this dir: `search.json`, `releases.json`, `series.json`,
`popular.json`.

## Chosen branch: **Branch C** — no keyless genre axis; publisher is the floor

Evidence, in the order the decision tree was walked:

1. **No genre filter block (rules out Branch A).** `grep -oiE 'filter-options-genres?|"genres?"|genre'`
   across `search.json` AND `popular.json` returned **zero matches**. For confirmation, re-ran
   case-insensitive `genre` grep across all 4 fixtures (`search.json`, `releases.json`, `series.json`,
   `popular.json`) — **zero matches, in any of them.** The only filter block that exists is
   `filters_publishers` (`<ul id="filter-options-publisher-list">`), plus `filters_format`. There is
   no `filter-options-genre-list` or equivalent. Never made the "probe request comparing counts"
   sub-step because there is no genre param name to probe with — the filter block that would carry
   it doesn't exist at all.
2. **No per-item genre/tag markup (rules out Branch B).** Regex `class=[\"']?[^\"'>]*(genre|tags)[^\"'>]*`
   over the full `list` HTML fragment in `popular.json` (317,334 chars, 300 `<li>` items) returned
   **zero matches**. Manually inspected the first raw `<li>` item (below) — no genre/tag element of
   any kind, only cover, issue-count badge, publisher+year line, and title.
3. **Landed on Branch C.** Publisher IS present per item, confirmed in both of the two distinct
   list-item shapes LOCG uses (see markup shapes below). `grep -oc 'class="publisher'` against
   `popular.json` literally returns 0 because that fixture's shape doesn't use a `class="publisher"`
   attribute at all (see markup note) — publisher there is an unlabeled first `<span class="">` in a
   `copy-really-small` line, not a dedicated class. The `releases.json` item shape (the "issue" list,
   used for the New Comics / weekly-releases view) DOES use a literal `<div class="publisher color-offset">`.
   Both shapes carry the publisher text; there is no world in which a genre string is available
   keyless from this endpoint. **Publisher-only shelving is the ceiling for LOCG without login.**

No genre id→name map exists to record (Branch A not taken).

## Exact-value pins — search.json, list item 0

- **series id:** `148319` (parsed from `href="/comics/series/148319/batman-daredevil"`, also present
  as `data-id="148319"` on the same `<a>`)
- **title:** `Batman / Daredevil` (exact text inside `<div class="title color-primary"><a ...>`)
- **cover URL shape (`data-src`):** `https://s3.amazonaws.com/comicgeeks/comics/covers/medium-6855190.jpg`
  — pattern is `https://s3.amazonaws.com/comicgeeks/comics/covers/medium-<numeric-id>.jpg` (no query
  string on this shape; contrast with the releases shape below, which has a cache-busting query string)
- **publisher:** `DC Comics` (exact text of the first `<span class="">` inside the
  `copy-really-small font-weight-bold color-offset text-truncate` div; second `<span class="">` in
  the same div holds `&nbsp;·&nbsp; 1999`, the year range)

Raw item (search.json, first `<li>`):
```html
<li>
    <div class="cover">
        <a href="/comics/series/148319/batman-daredevil" class="link-collection-series" data-id="148319">
            <img class="lazy" src="data:image/gif;base64,..." data-src="https://s3.amazonaws.com/comicgeeks/comics/covers/medium-6855190.jpg" alt="Batman / Daredevil">
        </a>
        <span class="details count-issues">1</span>
    </div>
    <div class="copy-really-small font-weight-bold color-offset text-truncate">
        <span class="">DC Comics</span>
        <span class="">&nbsp;·&nbsp; 1999</span>
    </div>
    <div class="title color-primary">
        <a href="/comics/series/148319/batman-daredevil" class="link-collection-series" data-id="148319">Batman / Daredevil</a>
    </div>
</li>
```

## Exact-value pins — releases.json, list item 0

- **data-pulls:** `60672` (raw attribute value on the `<li>`)
- **data-community:** `98` (raw attribute value on the `<li>`)
- Also present on the same `<li>`: `data-comic="6863939"`, `data-potw="48"`, `data-parent="0"`, `data-row="1"`
- **publisher:** present — `<div class="publisher color-offset">DC Comics</div>` (literal class this
  time, unlike the search/popular shape)
- **cover:** present — `data-src="https://s3.amazonaws.com/comicgeeks/comics/covers/medium-6863939.jpg?1783602974"`
  (note the `?1783602974` cache-busting query string, absent from the search/popular cover shape)
- **date:** present — `<span class="date" data-date="1783483200">Jul 8th, 2026</span>` (both a Unix
  timestamp in `data-date` and a human string as the span's text)

Raw item (releases.json, first `<li>`, comic-controller/login noise elided):
```html
<li class="issue " data-comic="6863939" data-pulls="60672" data-potw="48" data-community="98" data-parent="0" data-row="1">
    <div class="cover">
        <a href="/comic/6863939/absolute-batman-22">
            <img class="lazy" src="data:image/gif;base64,..." data-src="https://s3.amazonaws.com/comicgeeks/comics/covers/medium-6863939.jpg?1783602974" alt="absolute batman #22">
        </a>
    </div>
    <div class="publisher color-offset">DC Comics</div>
    <div class="title color-primary" data-sorting="Absolute Batman #22">
        <a href="/comic/6863939/absolute-batman-22">Absolute Batman #22</a>
    </div>
    <div class="details">
        <span class="date" data-date="1783483200">Jul 8th, 2026</span>
        <span class="price">&nbsp;&#183;&nbsp; $4.99</span>
    </div>
</li>
```

## Markup shape note for the parser author

**There are two distinct `<li>` item shapes in this API, keyed by which `list` param was used —
don't write one parser and assume it covers both:**

1. **Series/search shape** (`search.json`, `popular.json` — i.e. `list=search&list_option=series`):
   plain `<li>` (no attributes). Series id lives ONLY in the href
   (`/comics/series/<id>/<slug>`, also duplicated as `data-id` on the same `<a>`). Title is the text
   of the second `<a class="link-collection-series">` inside `<div class="title color-primary">`.
   Publisher is NOT a dedicated class — it's the first bare `<span class="">` inside
   `<div class="copy-really-small ...">`, a sibling to a second bare `<span class="">` holding the
   year range. No genre, no tags.
2. **Releases/issue shape** (`releases.json` — i.e. `list=releases`): `<li class="issue" data-comic=
   "<id>" data-pulls="<n>" data-potw="<n>" data-community="<n>" ...>` — the comic id is a real
   `data-comic` attribute here (not parsed from href), plus pull-count/community-score are also
   plain attributes on the `<li>` itself (no HTML-text-scraping needed for those three numbers).
   Publisher IS a literal `<div class="publisher color-offset">`. Cover `data-src` carries a
   cache-busting query string here; the series shape does not.

`series.json` (single-series detail, `series_id=150065` → "Beta Ray Bill", Marvel Comics, `count:5`
issues) uses the releases/issue shape for its `list` (per-issue entries with `data-comic`/pulls/etc.),
since it's listing individual issues of one series, not a list of series.

## Bottom line for downstream tasks

- No genre id→name map — don't build Task 5's genre shelves against an LOCG server-side genre filter;
  it doesn't exist keyless.
- Publisher grouping is real and available in both shapes (parser must branch on shape, not assume
  one class name).
- `filters_format` and `filters_publishers` are the only filter blocks LOCG exposes keyless.
