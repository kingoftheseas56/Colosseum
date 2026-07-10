// ContinueSeeAll.js — pure chip logic for the Continue "See All" page (spec: haven
// docs/superpowers/specs/2026-07-11-colosseum-continue-see-all-design.md). One entry
// point: apply(items, sort, medium). No QML/Qt globals — headless-testable
// (tests/continue_see_all_harness.qml). Never mutates its input.
//
//   sort   "recent" (default, updatedAt desc) | "az" | "za"
//          | "watched" (only finished, recency kept) | "unwatched" (only unfinished)
//   medium "" (all) | "video" | "manga" | "comic" | "book"   — the home page's chip group
.pragma library

function label(e) {
    if (e.title !== undefined && ("" + e.title).length) return "" + e.title;
    if (e.caption !== undefined) return "" + e.caption;
    return "";
}

function stamp(e) {
    var n = Number(e.updatedAt);
    return isNaN(n) ? 0 : n;    // missing/garbage timestamps sink to the back
}

function apply(items, sort, medium) {
    var out = (items || []).slice();
    if (medium)
        out = out.filter(function(e) { return e.kind === medium; });
    if (sort === "watched")
        out = out.filter(function(e) { return e.watched === true; });
    else if (sort === "unwatched")
        out = out.filter(function(e) { return e.watched !== true; });

    if (sort === "az" || sort === "za") {
        out.sort(function(a, b) {
            var c = label(a).toLocaleLowerCase().localeCompare(label(b).toLocaleLowerCase());
            return sort === "az" ? c : -c;
        });
    } else {
        // recent / watched / unwatched all present newest-first
        out.sort(function(a, b) { return stamp(b) - stamp(a); });
    }
    return out;
}
