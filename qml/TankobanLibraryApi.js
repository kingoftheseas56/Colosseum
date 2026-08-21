.pragma library

// TankobanLibraryApi — pure row derivation for the Tankoban Library tab (spec:
// Brotherhood#1). Mirrors LibraryApi.js's shape (pure joins, no context-property or
// network access — a .pragma library script can't see those anyway) but carries
// Tankoban's own vocabulary: manga/comic rows, chapter/volume reading lanes, no
// airing/watched/new-episode concepts. See CONTEXT.md for "Library row", "Collection",
// "reading lane".
//
// TB-002 slice: manga-chapter progress join added. A manga Collection entry joins the
// kind:"manga" progress list — canonical first (record.id === entry.id), then a legacy
// title fallback (a still-title-keyed entry finding its seriesId-keyed progress). Two
// Collection entries that resolve to the SAME progress record (the brief re-file window
// where both a canonical and a legacy row exist) collapse to one canonical row.
//
// TB-003 slice: the volume lane (kind:"tankoban") joins manga entries. Originally this
// picked the newer of a chapter lane and the volume lane; catalogue-independence Slice 5
// (2026-08-20) removed the chapter lane entirely (chapters deleted, WC unplugged), so a
// manga entry now resolves to volume-vs-nothing — no "newer of two" comparison left to
// make. Comic entries (type:"comic") join kind:"comic" progress only — they never match
// manga/tankoban records. Comics use the same canonical-then-title matcher shape but a
// strict kind filter, so a saved western comic and a manga with the same title cannot
// cross-resume.
//
// TB-004 slice: a row whose resume target is on disk shows a download badge. The badge
// is a pure function of the resume target's chapter id + a per-chapter-id "is on disk"
// map the page feeds in (the page owns the native download-index seam; this pure module
// never touches it). No new native API and no series-level scan: one lookup per resume
// target, honest for the resume action. A row resuming via the VOLUME lane has no honest
// per-volume on-disk state without a new seam (volumes live in TankobanVolumes, not the
// chapter download index), so volume-lane rows are deliberately left unbadged — an
// explicit, scoped gap, not a silent bug. Filters and sorts still stay at their TB-001
// defaults; TB-005 owns them.

function normalizedTitle(title) {
    return String(title || "").toLowerCase().replace(/\s+/g, " ").trim();
}

// Find the best progress record of ONE kind for an entry. Canonical (record.id ===
// entry.id) outranks the legacy title fallback; the title path is only tried when the
// exact id match found nothing. Returns the record object or null. Used by buildRows
// for each lane separately so a manga series can hold a chapter record AND a volume
// record at the same time and let the caller pick the newer.
function _matchProgressOfKind(entry, progressList, kind) {
    var list = progressList || [];
    var eid = String(entry.id || "");
    // 1. canonical: exact id match within this kind
    for (var i = 0; i < list.length; i++) {
        var rec = list[i];
        if (!rec) continue;
        if (rec.kind !== kind) continue;
        if (String(rec.id || "") === eid) return rec;
    }
    // 2. legacy fallback: normalized title match within this kind (canonical missed)
    var etitle = normalizedTitle(entry.title);
    if (!etitle.length) return null;
    for (var j = 0; j < list.length; j++) {
        var r = list[j];
        if (!r) continue;
        if (r.kind !== kind) continue;
        if (normalizedTitle(r.title) === etitle) return r;
    }
    return null;
}

// Pick the newer of two records (higher updatedAt wins). Ties break to the first arg
// (chapterLane) for determinism — a chapter read and a volume read at the same timestamp
// resume as a chapter. null/undefined loses to anything.
function _newerOf(chapterLane, otherLane) {
    if (!chapterLane) return otherLane || null;
    if (!otherLane) return chapterLane;
    var ct = Number(chapterLane.updatedAt) || 0;
    var ot = Number(otherLane.updatedAt) || 0;
    return ot > ct ? otherLane : chapterLane;
}

function buildRows(collectionEntries, mangaProgress, volumeProgress, comicProgress, downloadSeries) {
    var entries = collectionEntries || [];
    // downloadSeries is a per-chapter-id "is on disk" map the page derives from the
    // native download index: { "<chapterId>": true, ... }. The page owns that seam; this
    // pure module only reads the resulting map.
    var onDisk = downloadSeries || {};
    var rows = [];
    // First pass: build a row per entry and resolve its progress. For a manga entry the
    // chapter lane (kind:"manga") and the volume lane (kind:"tankoban") are both probed
    // and the NEWER record wins; comics only probe kind:"comic". We track which Progress
    // record id each row matched so the duplicate-suppression pass can drop a legacy
    // entry that resolves to the same record as a canonical one.
    for (var i = 0; i < entries.length; i++) {
        var entry = entries[i];
        if (!entry) continue;

        var mediaType = entry.type === "comic" ? "comic" : "manga";
        var row = {
            entry: entry,
            mediaType: mediaType,
            state: "notStarted",
            progress: 0,
            resumeTarget: null,
            resumeLane: "",
            lastActivityAt: 0,
            addedAt: Number(entry.addedAt) || 0,
            downloaded: false,
            downloadSeriesKey: "",
            // internal-only: the Progress record id this row matched (for dedupe), or "".
            // Stripped before returning so it never leaks as part of the row contract.
            _matchedProgressId: ""
        };

        if (mediaType === "manga") {
            // Volume lane (kind:"tankoban") only — catalogue-independence Slice 5,
            // 2026-08-20 removed the chapter lane (kind:"manga") completely: chapters
            // are deleted on disk and their progress records are purged by the
            // one-time migration, so there is nothing left to join here. The
            // `mangaProgress` parameter stays in the signature (existing callers still
            // pass it) but is no longer read — TB-003's "newer of two lanes" rule
            // collapses to volume-vs-nothing for manga.
            var volumeRec = _matchProgressOfKind(entry, volumeProgress, "tankoban");
            if (volumeRec) {
                row.state = "inProgress";
                row.progress = Number(volumeRec.progress) || 0;
                row.resumeTarget = volumeRec;          // the exact record, unmodified
                row.resumeLane = "tankoban";
                row.lastActivityAt = Number(volumeRec.updatedAt) || 0;
                row._matchedProgressId = String(volumeRec.id || "");
            }
        } else {
            // Comic lane (kind:"comic") only. Comics never cross-match manga/tankoban
            // records even on identical titles — the strict kind filter prevents it.
            var comicRec = _matchProgressOfKind(entry, comicProgress, "comic");
            if (comicRec) {
                row.state = "inProgress";
                row.progress = Number(comicRec.progress) || 0;
                row.resumeTarget = comicRec;
                row.resumeLane = "comic";
                row.lastActivityAt = Number(comicRec.updatedAt) || 0;
                row._matchedProgressId = String(comicRec.id || "");
            }
        }

        // TB-004 download badge: the row's resume-target chapter is on disk. Honest for
        // the resume action only; says nothing about the rest of the series. Volume-lane
        // rows have no honest per-volume on-disk state here (volumes live in
        // TankobanVolumes, not the chapter download index), so they are deliberately
        // left unbadged.
        if (row.resumeTarget && row.resumeLane !== "tankoban") {
            var chId = String((row.resumeTarget.resume && row.resumeTarget.resume.chapterId) || "");
            if (chId.length && onDisk[chId] === true) {
                row.downloaded = true;
                row.downloadSeriesKey = chId;
            }
        }
        rows.push(row);
    }

    // Duplicate suppression: if a canonical entry (id = seriesId) and a legacy entry
    // (id = title) BOTH resolved to the same Progress record id, drop the legacy row and
    // keep the canonical one. This covers the brief re-file window where both Collection
    // rows coexist for one series. (TB-002.)
    var seen = {};   // Progress id -> index in rows of the canonical row that claimed it
    var keep = [];
    for (var k = 0; k < rows.length; k++) {
        var r = rows[k];
        var pid = r._matchedProgressId;
        if (pid.length) {
            // Is this row's entry id the same as the matched Progress id? That makes it
            // canonical w.r.t. this record; otherwise it's a legacy title-keyed match.
            var canonicalForThis = String(r.entry.id || "") === pid;
            if (seen.hasOwnProperty(pid)) {
                // A row already claimed this record. Drop whichever of the two is the
                // legacy (non-canonical) one; if both are non-canonical (ambiguous), keep
                // the earlier one for determinism.
                var claimer = rows[seen[pid]];
                var claimerCanonical = String(claimer.entry.id || "") === pid;
                if (canonicalForThis && !claimerCanonical) {
                    // replace the legacy claimant with this canonical row
                    keep[seen[pid]] = r;
                    continue;
                }
                // otherwise this row is the duplicate — drop it
                continue;
            }
            seen[pid] = keep.length;
        }
        keep.push(r);
    }
    rows = keep;

    // Strip the internal dedupe key so the returned rows carry only the row contract.
    for (var m = 0; m < rows.length; m++) delete rows[m]._matchedProgressId;

    // Deterministic Added ordering (TB-001's only ordering rule): addedAt desc, then
    // normalized title asc, then Collection id asc — same tie-break chain the plan
    // specifies for the "Added" sort mode, applied here as buildRows' own default order
    // since sortRows doesn't exist until TB-005.
    rows.sort(function (a, b) {
        if (a.addedAt !== b.addedAt) return b.addedAt - a.addedAt;
        var ta = normalizedTitle(a.entry.title), tb = normalizedTitle(b.entry.title);
        if (ta !== tb) return ta < tb ? -1 : 1;
        var ia = String(a.entry.id || ""), ib = String(b.entry.id || "");
        if (ia === ib) return 0;
        return ia < ib ? -1 : 1;
    });

    return rows;
}

// ─────────────────────────────────────────────────────────────────────────────
// TB-005 slice: search + 3 filters + 3 sorts. Two pure functions over the row
// contract buildRows returns, mirroring LibraryApi's applyFilters/sortRows shape but
// against Tankoban's own row fields (state "inProgress", downloaded bool, mediaType
// "manga"/"comic", lastActivityAt, addedAt, entry.title). They never touch a singleton.
// Filter modes: "" (All) | "inProgress" | "downloaded". Sort modes: "lastRead" |
// "added" | "az". Both return a NEW array; neither mutates its input. applyFilters
// AND-composes the active filter with the search needle (case-insensitive substring on
// entry.title). sortRows is stable per Array.prototype.sort's native contract; the
// "added" and "az" modes reuse buildRows' own ordering rationale (addedAt desc; title
// asc) so toggling sort never silently reorders equal-key rows.
// ─────────────────────────────────────────────────────────────────────────────

// applyFilters — AND-compose the active filter chip with the search needle. `state` is
// the page's filter/query snapshot. "" (or "all") = no filter; "inProgress" = only rows
// with state==="inProgress"; "downloaded" = only rows with downloaded===true. The query
// is a case-insensitive substring match on entry.title (trimmed). Returns a new array.
function applyFilters(rows, state) {
    state = state || {};
    var sf = String(state.filter || "").toLowerCase();
    if (sf === "all") sf = "";
    var q = String(state.query || "").trim().toLowerCase();
    var out = [];
    for (var i = 0; i < (rows || []).length; i++) {
        var r = rows[i];
        if (!r) continue;
        if (sf === "inprogress" && r.state !== "inProgress") continue;
        if (sf === "downloaded" && !r.downloaded) continue;
        if (q) {
            var title = String((r.entry && r.entry.title) || "").toLowerCase();
            if (title.indexOf(q) === -1) continue;
        }
        out.push(r);
    }
    return out;
}

// sortRows — returns a NEW ordered array; never mutates the input. Modes: "lastRead"
// (lastActivityAt desc, the default — the series you touched most recently first),
// "added" (addedAt desc — most-recently saved first), "az" (entry.title asc,
// case-insensitive). Unknown modes fall back to "lastRead". Ties in "lastRead"/"added"
// are left to the input order (buildRows' deterministic Added order), so equal-key rows
// keep a stable position across re-sorts instead of shuffling.
function sortRows(rows, mode) {
    var out = (rows || []).slice();
    function num(x) { return (typeof x === "number" && !isNaN(x)) ? x : 0; }
    if (mode === "added") {
        out.sort(function (a, b) { return num(b.addedAt) - num(a.addedAt); });
    } else if (mode === "az") {
        out.sort(function (a, b) {
            var ta = String((a.entry && a.entry.title) || "").toLowerCase();
            var tb = String((b.entry && b.entry.title) || "").toLowerCase();
            return ta < tb ? -1 : (ta > tb ? 1 : 0);
        });
    } else { // "lastRead" (default)
        out.sort(function (a, b) { return num(b.lastActivityAt) - num(a.lastActivityAt); });
    }
    return out;
}
