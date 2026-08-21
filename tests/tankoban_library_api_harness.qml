// Offscreen proof of TankobanLibraryApi's pure row derivation. TB-001 covered the
// notStarted/no-progress join + deterministic ordering; TB-002 adds the manga-chapter
// progress join (canonical id → legacy title fallback) and duplicate suppression.
// NEVER throw (hangs offscreen); collect fails, Qt.exit(fails.length). Mirrors
// library_api_harness's shape.
import QtQuick
import "../qml/TankobanLibraryApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            function ent(id, title, type, addedAt) {
                return { id: id, title: title, type: type, cover: "", payload: ({}),
                         world: "tankoban", addedAt: addedAt };
            }
            // A manga Progress record, shape per ComicReaderState.js progressPayload().
            // id IS the seriesId; title is the series title; sub is the chapter label.
            function prog(id, title, sub, progress, updatedAt, chapterId, page) {
                return { id: id, kind: "manga", title: title, sub: sub,
                         progress: progress, updatedAt: updatedAt,
                         resume: { chapterId: chapterId, page: page } };
            }
            // Same record shape but with an explicit kind (TB-003: "tankoban" volume lane,
            // "comic" western lane). id is still the seriesId; the volume id / chapter id
            // rides in resume.chapterId per progressPayload().
            function progKind(kind, id, title, sub, progress, updatedAt, chapterId, page) {
                return { id: id, kind: kind, title: title, sub: sub,
                         progress: progress, updatedAt: updatedAt,
                         resume: { chapterId: chapterId, page: page } };
            }

            // ─────────────────────────────────────────────────────────────────────
            // TB-001 regression: no progress/downloads wired yet — every row notStarted
            // ─────────────────────────────────────────────────────────────────────
            var e1 = ent("naruto-id", "Naruto", "manga", 1000);
            var e2 = ent("gc:spawn", "Spawn", "comic", 2000);
            var e3 = ent("One Piece", "One Piece", "manga", 3000);   // legacy title-keyed id
            var entries = [e1, e2, e3];

            var rows = Api.buildRows(entries, [], [], [], []);

            ok(rows.length === 3, "buildRows returns one row per entry: " + rows.length);
            for (var i = 0; i < rows.length; i++) {
                var row = rows[i];
                ok(row.state === "notStarted", "row " + i + " state notStarted: " + row.state);
                ok(row.resumeTarget === null, "row " + i + " resumeTarget null");
                ok(row.resumeLane === "", "row " + i + " resumeLane empty: " + row.resumeLane);
                ok(row.lastActivityAt === 0, "row " + i + " lastActivityAt 0: " + row.lastActivityAt);
                ok(row.downloaded === false, "row " + i + " downloaded false");
                ok(row.progress === 0, "row " + i + " progress 0: " + row.progress);
            }

            // ── mediaType join: manga vs comic, driven by entry.type ──
            function rowFor(id) {
                for (var j = 0; j < rows.length; j++) if (rows[j].entry.id === id) return rows[j];
                return null;
            }
            ok(rowFor("naruto-id").mediaType === "manga", "manga entry -> mediaType manga");
            ok(rowFor("gc:spawn").mediaType === "comic", "comic entry -> mediaType comic");
            ok(rowFor("One Piece").mediaType === "manga", "legacy title-keyed manga -> mediaType manga");

            // ── entry + addedAt ride through unchanged ──
            ok(rowFor("naruto-id").entry.title === "Naruto", "row keeps original entry");
            ok(rowFor("naruto-id").entry === e1, "row.entry is the SAME object reference as the input entry (not a copy)");
            ok(rowFor("naruto-id").addedAt === 1000, "row addedAt from entry.addedAt");

            // ── deterministic Added ordering: newest addedAt first ──
            var order = rows.map(function (r) { return r.entry.id; }).join(",");
            ok(order === "One Piece,gc:spawn,naruto-id", "rows ordered by addedAt desc: " + order);

            // ── tie-break on equal addedAt: normalized title ascending, then id ascending ──
            var t1 = ent("zzz", "Beta Series", "manga", 500);
            var t2 = ent("aaa", "Alpha Series", "comic", 500);
            var tied = Api.buildRows([t1, t2], [], [], [], []);
            var tieOrder = tied.map(function (r) { return r.entry.id; }).join(",");
            ok(tieOrder === "aaa,zzz", "equal addedAt tie-breaks by title asc: " + tieOrder);

            // ─────────────────────────────────────────────────────────────────────
            // TB-002: manga-chapter progress join
            // ─────────────────────────────────────────────────────────────────────

            // --- canonical id match → inProgress + exact record + lane + lastActivityAt ---
            var ce = ent("berserk-id", "Berserk", "manga", 4000);
            var prec = prog("berserk-id", "Berserk", "Ch. 357", 0.42, 9999, "ch-357", 3);
            var rrows = Api.buildRows([ce], [prec], [], [], []);
            ok(rrows.length === 1, "canonical: one row");
            ok(rrows[0].state === "inProgress", "canonical: state inProgress: " + rrows[0].state);
            ok(rrows[0].resumeTarget === prec, "canonical: resumeTarget is the EXACT record object (not a copy)");
            ok(rrows[0].resumeLane === "manga", "canonical: resumeLane manga: " + rrows[0].resumeLane);
            ok(rrows[0].lastActivityAt === 9999, "canonical: lastActivityAt = record.updatedAt: " + rrows[0].lastActivityAt);
            ok(rrows[0].progress === 0.42, "canonical: progress from record: " + rrows[0].progress);

            // --- legacy title fallback: entry id = title, matches a record by normalized title ---
            var le = ent("Vagabond", "Vagabond", "manga", 4100);   // legacy: id = title
            var lrec = prog("vagabond-actual-id", "Vagabond", "Vol. 3", 0.1, 5555, "v3", 0);
            var lrows = Api.buildRows([le], [lrec], [], [], []);
            ok(lrows.length === 1, "legacy: one row");
            ok(lrows[0].state === "inProgress", "legacy: state inProgress via title fallback");
            ok(lrows[0].resumeTarget === lrec, "legacy: resumeTarget is the exact record");
            ok(lrows[0].resumeLane === "manga", "legacy: resumeLane manga");
            ok(lrows[0].lastActivityAt === 5555, "legacy: lastActivityAt = record.updatedAt");

            // normalized title fallback must be case/whitespace-insensitive
            var le2 = ent("  one   piece  ", "  one   piece  ", "manga", 4200);
            var lrec2 = prog("op-real-id", "One Piece", "Ch. 1050", 0.8, 7777, "c1050", 5);
            var lrows2 = Api.buildRows([le2], [lrec2], [], [], []);
            ok(lrows2.length === 1 && lrows2[0].state === "inProgress" && lrows2[0].resumeTarget === lrec2,
               "legacy: normalized title fallback is case/whitespace-insensitive");

            // --- canonical outranks fallback: both an exact-id record and a title record
            //     exist for the same entry. Exact id wins; the title record must NOT be used. ---
            var xe = ent("x-id", "Duplicate", "manga", 4300);
            var canonicalRec = prog("x-id", "Duplicate", "Ch. 1", 0.5, 1111, "c1", 0);
            var titleLure = prog("other-id", "Duplicate", "Ch. 2", 0.9, 2222, "c2", 0);  // same title, different id
            var xrows = Api.buildRows([xe], [canonicalRec, titleLure], [], [], []);
            ok(xrows.length === 1, "outrank: one row");
            ok(xrows[0].resumeTarget === canonicalRec, "outrank: canonical id match beats title fallback (canonicalRec expected)");
            ok(xrows[0].lastActivityAt === 1111, "outrank: used canonical record's updatedAt, not the lure's");

            // --- unrelated title does NOT match a canonical entry (no false joins) ---
            var ue = ent("solo-id", "Solo Leveling", "manga", 4400);
            var urec = prog("monster-id", "Monster", "Ch. 1", 0.3, 3333, "m1", 0);
            var urows = Api.buildRows([ue], [urec], [], [], []);
            ok(urows.length === 1, "unrelated: one row");
            ok(urows[0].state === "notStarted", "unrelated: id+title both miss -> notStarted");
            ok(urows[0].resumeTarget === null, "unrelated: resumeTarget null");

            // --- unmatched entry stays notStarted (already covered above, explicit here) ---
            ok(urows[0].progress === 0 && urows[0].resumeLane === "" && urows[0].lastActivityAt === 0,
               "unrelated: all progress fields stay at TB-001 defaults");

            // --- duplicate canonical+legacy (same Progress record) → ONE canonical row ---
            // The re-file window: Collection holds BOTH the canonical entry (id=seriesId)
            // AND the legacy entry (id=title) for the same series; both join to the SAME
            // Progress record. Only the canonical row should survive.
            var dupCanonical = ent("dup-id", "DupTitle", "manga", 4500);
            var dupLegacy = ent("DupTitle", "DupTitle", "manga", 4550);
            var dupRec = prog("dup-id", "DupTitle", "Ch. 9", 0.66, 8888, "d9", 1);
            var drows = Api.buildRows([dupCanonical, dupLegacy], [dupRec], [], [], []);
            ok(drows.length === 1, "dedupe: one row when canonical+legacy both match the same record");
            ok(drows[0].entry.id === "dup-id", "dedupe: the canonical (id=seriesId) row survives, legacy dropped: " + drows[0].entry.id);
            ok(drows[0].resumeTarget === dupRec, "dedupe: survivor carries the record");
            // order of input entries must not matter
            var drows2 = Api.buildRows([dupLegacy, dupCanonical], [dupRec], [], [], []);
            ok(drows2.length === 1 && drows2[0].entry.id === "dup-id",
               "dedupe: canonical wins regardless of input order");

            // ─────────────────────────────────────────────────────────────────────
            // TB-003: volume-lane + comic joins + most-recent-lane selection
            // ─────────────────────────────────────────────────────────────────────

            // --- volume lane join: a manga entry with a kind:"tankoban" record resumes
            //     as a tankoban lane, with the record's progress + updatedAt carried. ---
            var vEntry = ent("vagabond-id", "Vagabond", "manga", 4600);
            var vRec = progKind("tankoban", "vagabond-id", "Vagabond", "Vol. 3", 0.3, 7777, "vol-3", 0);
            var vRows = Api.buildRows([vEntry], [], [vRec], [], []);
            ok(vRows.length === 1, "volume: one row");
            ok(vRows[0].state === "inProgress", "volume: state inProgress via tankoban lane");
            ok(vRows[0].resumeTarget === vRec, "volume: resumeTarget is the exact tankoban record");
            ok(vRows[0].resumeLane === "tankoban", "volume: resumeLane tankoban: " + vRows[0].resumeLane);
            ok(vRows[0].lastActivityAt === 7777, "volume: lastActivityAt = record.updatedAt");
            ok(vRows[0].progress === 0.3, "volume: progress from the tankoban record");

            // --- comic lane join: a comic entry with a kind:"comic" record resumes as a
            //     comic lane. id is the comic series id (gc:/gcd: shaped); strict kind
            //     filter means a manga record with the same id/title cannot match. ---
            var cEntry = ent("gc:spawn", "Spawn", "comic", 4700);
            var cRec = progKind("comic", "gc:spawn", "Spawn", "Issue #5", 0.7, 6666, "iss-5", 2);
            var cRows = Api.buildRows([cEntry], [], [], [cRec], []);
            ok(cRows.length === 1, "comic: one row");
            ok(cRows[0].state === "inProgress", "comic: state inProgress via comic lane");
            ok(cRows[0].resumeTarget === cRec, "comic: resumeTarget is the exact comic record");
            ok(cRows[0].resumeLane === "comic", "comic: resumeLane comic: " + cRows[0].resumeLane);
            ok(cRows[0].lastActivityAt === 6666, "comic: lastActivityAt = record.updatedAt");
            ok(cRows[0].progress === 0.7, "comic: progress from the comic record");

            // --- most-recent lane: a manga entry with BOTH a chapter record and a volume
            //     record picks the NEWER one and emits exactly one resume lane. ---
            // case A: volume is newer -> tankoban lane wins
            var bothEntry = ent("berserk-id", "Berserk", "manga", 4800);
            var chOlder = progKind("manga", "berserk-id", "Berserk", "Ch. 357", 0.4, 1000, "ch-357", 3);
            var volNewer = progKind("tankoban", "berserk-id", "Berserk", "Vol. 40", 0.5, 2000, "vol-40", 0);
            var bothRowsA = Api.buildRows([bothEntry], [chOlder], [volNewer], [], []);
            ok(bothRowsA.length === 1, "both: one row (volume newer)");
            ok(bothRowsA[0].resumeTarget === volNewer, "both: volume newer -> tankoban record wins");
            ok(bothRowsA[0].resumeLane === "tankoban", "both: volume newer -> lane tankoban: " + bothRowsA[0].resumeLane);
            ok(bothRowsA[0].lastActivityAt === 2000, "both: volume newer -> lastActivityAt = volume updatedAt");

            // case B: chapter is newer -> manga lane wins
            var chNewer = progKind("manga", "berserk-id", "Berserk", "Ch. 358", 0.45, 9000, "ch-358", 0);
            var volOlder = progKind("tankoban", "berserk-id", "Berserk", "Vol. 40", 0.5, 2000, "vol-40", 0);
            var bothRowsB = Api.buildRows([bothEntry], [chNewer], [volOlder], [], []);
            ok(bothRowsB.length === 1, "both: one row (chapter newer)");
            ok(bothRowsB[0].resumeTarget === chNewer, "both: chapter newer -> manga record wins");
            ok(bothRowsB[0].resumeLane === "manga", "both: chapter newer -> lane manga: " + bothRowsB[0].resumeLane);
            ok(bothRowsB[0].lastActivityAt === 9000, "both: chapter newer -> lastActivityAt = chapter updatedAt");

            // case C: tie on updatedAt -> chapter (manga) lane wins for determinism
            var chTie = progKind("manga", "tie-id", "Tie", "Ch. 1", 0.1, 5000, "c1", 0);
            var volTie = progKind("tankoban", "tie-id", "Tie", "Vol. 1", 0.9, 5000, "v1", 0);
            var tieRows = Api.buildRows([ent("tie-id", "Tie", "manga", 4900)], [chTie], [volTie], [], []);
            ok(tieRows.length === 1 && tieRows[0].resumeLane === "manga" && tieRows[0].resumeTarget === chTie,
               "both: updatedAt tie -> chapter (manga) lane wins for determinism");

            // --- comic never matches manga/tankoban records even on identical ids/titles ---
            // A manga record exists for the same id, but the entry is a comic: comic lane
            // probes kind:"comic" only, so the manga record is invisible to it.
            var xEntry = ent("shared-id", "Shared Title", "comic", 5000);
            var xMangaRec = progKind("manga", "shared-id", "Shared Title", "Ch. 1", 0.8, 9999, "c1", 0);
            var xRows = Api.buildRows([xEntry], [xMangaRec], [], [], []);
            ok(xRows.length === 1, "comic-isolation: one row");
            ok(xRows[0].state === "notStarted", "comic-isolation: comic entry does NOT match a manga record");
            ok(xRows[0].resumeTarget === null && xRows[0].resumeLane === "",
               "comic-isolation: no resume picked up from a mismatched-kind record");

            // symmetric: a manga entry never matches a comic record
            var mEntry = ent("shared-id", "Shared Title", "manga", 5100);
            var mComicRec = progKind("comic", "shared-id", "Shared Title", "Issue #1", 0.8, 9999, "i1", 0);
            var mRows = Api.buildRows([mEntry], [], [], [mComicRec], []);
            ok(mRows.length === 1 && mRows[0].state === "notStarted",
               "manga-isolation: manga entry does NOT match a comic record");

            // --- TB-002 regression: the refactor to _matchProgressOfKind did not change
            //     the manga chapter-lane behavior (canonical id, title fallback, etc.) ---
            var regEntry = ent("reg-id", "Reg Title", "manga", 5200);
            var regRec = prog("reg-id", "Reg Title", "Ch. 1", 0.55, 1234, "rc1", 0);
            var regRows = Api.buildRows([regEntry], [regRec], [], [], []);
            ok(regRows.length === 1 && regRows[0].resumeTarget === regRec && regRows[0].resumeLane === "manga",
               "regression (TB-002): manga chapter-lane join still works after refactor");

            // ─────────────────────────────────────────────────────────────────────
            // TB-004: download badge — resume-target chapter is on disk
            // ─────────────────────────────────────────────────────────────────────

            // helper: build a one-row case and return the row, or null
            function badgeRow(entry, mp, vp, cp, onDisk) {
                var rs = Api.buildRows([entry], mp || [], vp || [], cp || [], onDisk || {});
                return rs.length === 1 ? rs[0] : null;
            }

            // --- manga resume target on disk → badged, key = resume chapter id ---
            var be = ent("dl-id", "DL Manga", "manga", 6000);
            var brec = prog("dl-id", "DL Manga", "Ch. 4", 0.4, 7000, "dl-ch-4", 1);
            var bOn = badgeRow(be, [brec], [], [], { "dl-ch-4": true });
            ok(bOn !== null && bOn.downloaded === true, "TB-004: manga resume chapter on disk -> downloaded true");
            ok(bOn.downloadSeriesKey === "dl-ch-4", "TB-004: downloadSeriesKey = resume chapter id: " + bOn.downloadSeriesKey);

            // --- manga resume target NOT on disk → not badged ---
            var bOff = badgeRow(be, [brec], [], [], { "some-other-chapter": true });
            ok(bOff !== null && bOff.downloaded === false, "TB-004: resume chapter absent from on-disk map -> downloaded false");
            ok(bOff.downloadSeriesKey === "", "TB-004: downloadSeriesKey empty when not on disk");

            // --- no resume target at all → not badged (no resume chapter to look up) ---
            var bNone = badgeRow(be, [], [], [], { "dl-ch-4": true });
            ok(bNone !== null && bNone.downloaded === false, "TB-004: no resume target -> downloaded false regardless of on-disk map");

            // --- comic resume target on disk → badged (comics use the same resume.chapterId key) ---
            var bcEnt = ent("gc:batman", "Batman", "comic", 6100);
            var bcRec = progKind("comic", "gc:batman", "Batman", "Issue #1", 0.5, 8000, "bat-1", 0);
            var bcRow = badgeRow(bcEnt, [], [], [bcRec], { "bat-1": true });
            ok(bcRow !== null && bcRow.downloaded === true && bcRow.downloadSeriesKey === "bat-1",
               "TB-004: comic resume chapter on disk -> badged (comic + manga share resume.chapterId key)");

            // --- volume-lane resume is deliberately NOT badged (no honest per-volume on-disk state) ---
            // The volume lane resume lives in TankobanVolumes, not Downloads; the on-disk map is
            // chapter-id keyed and a volume id would be a false lookup. Volume-lane rows stay
            // unbadged as an explicit scoped gap, not a bug.
            var bvEnt = ent("bv-id", "BV Manga", "manga", 6200);
            var bvRec = progKind("tankoban", "bv-id", "BV Manga", "Vol. 2", 0.3, 9000, "vol-2-id", 0);
            var bvRow = badgeRow(bvEnt, [], [bvRec], [], { "vol-2-id": true });   // volume id IS in the map
            ok(bvRow !== null && bvRow.downloaded === false, "TB-004: volume-lane resume is NOT badged even if its volume id appears in the on-disk map (explicit gap)");
            ok(bvRow.downloadSeriesKey === "", "TB-004: volume-lane row keeps empty downloadSeriesKey");

            // --- empty/null on-disk map → no badge anywhere (TB-001/002/003 rows unaffected) ---
            var allLegacy = Api.buildRows(
                [ent("n1", "N1", "manga", 100), ent("gc:c1", "C1", "comic", 200)],
                [prog("n1", "N1", "Ch. 1", 0.3, 300, "n1c1", 0)], [], [], null);
            ok(allLegacy.length === 2, "TB-004: null on-disk map does not break buildRows");
            for (var bi = 0; bi < allLegacy.length; bi++) {
                ok(allLegacy[bi].downloaded === false, "TB-004: null on-disk map -> all rows downloaded false (index " + bi + ")");
                ok(allLegacy[bi].downloadSeriesKey === "", "TB-004: null on-disk map -> all rows downloadSeriesKey empty (index " + bi + ")");
            }

            // --- regression: TB-002/003 calls pass [] as the 5th arg (the historical
            //     placeholder). buildRows must still treat it as "no on-disk state" and
            //     keep every row unbadged, so existing two-arg/old tests stay meaningful. ---
            var legacyCall = Api.buildRows([ent("lr-id", "LR", "manga", 6300)],
                                           [prog("lr-id", "LR", "Ch. 1", 0.2, 6400, "lr-1", 0)], [], [], []);
            ok(legacyCall.length === 1 && legacyCall[0].downloaded === false,
               "TB-004 regression: buildRows(..., []) (placeholder 5th arg) keeps rows unbadged");

            // ─────────────────────────────────────────────────────────────────────
            // Purity: buildRows must not mutate its inputs (TB-001 contract, still holds)
            // ─────────────────────────────────────────────────────────────────────
            var purityEntries = [ent("a-id", "Alpha", "manga", 100), ent("b-id", "Beta", "manga", 200)];
            var purityProgress = [prog("a-id", "Alpha", "Ch. 1", 0.5, 500, "a1", 0)];
            var beforeEntries = JSON.stringify(purityEntries);
            var beforeProgress = JSON.stringify(purityProgress);
            Api.buildRows(purityEntries, purityProgress, [], [], []);
            ok(JSON.stringify(purityEntries) === beforeEntries, "buildRows does not mutate the entries array/objects");
            ok(JSON.stringify(purityProgress) === beforeProgress, "buildRows does not mutate the progress array/objects");

            // internal dedupe key must not leak into the returned row contract
            var cleanRows = Api.buildRows([ent("c-id", "Gamma", "manga", 300)], [prog("c-id", "Gamma", "Ch. 1", 0.2, 600, "c1", 0)], [], [], {});
            ok(!cleanRows[0].hasOwnProperty("_matchedProgressId"), "internal _matchedProgressId stripped from returned rows");

            // TB-004 purity: the on-disk map is not mutated by buildRows (it is only read)
            var purityMap = { "pm-ch": true };
            var beforeMap = JSON.stringify(purityMap);
            Api.buildRows([ent("pm-id", "PM", "manga", 301)], [prog("pm-id", "PM", "Ch. 1", 0.2, 601, "pm-ch", 0)], [], [], purityMap);
            ok(JSON.stringify(purityMap) === beforeMap, "TB-004 purity: buildRows does not mutate the on-disk map");

            // ─────────────────────────────────────────────────────────────────────
            // TB-005: applyFilters + sortRows (search + 3 filters + 3 sorts)
            // ─────────────────────────────────────────────────────────────────────
            // A fixed 4-row fixture spanning the filter/sort axes:
            //   Alpha  — manga, inProgress, downloaded, lastActivityAt 9000, addedAt 1000, "Alpha"
            //   Beta   — manga, inProgress, NOT downloaded, lastActivityAt 8000, addedAt 2000, "Beta"
            //   Gamma  — comic, inProgress, downloaded, lastActivityAt 7000, addedAt 3000, "Gamma"
            //   Delta  — manga, notStarted, NOT downloaded, lastActivityAt 0, addedAt 4000, "Delta"
            // (built directly as row objects — applyFilters/sortRows operate on the row
            //  contract, never on Collection/Progress, so synthetic rows are honest here)
            function mkRow(id, title, type, state, dl, lastAct, added) {
                return {
                    entry: { id: id, title: title, type: type, addedAt: added },
                    mediaType: type, state: state, progress: 0, resumeTarget: null,
                    resumeLane: "", lastActivityAt: lastAct, addedAt: added,
                    downloaded: dl, downloadSeriesKey: ""
                };
            }
            var tRows = [
                mkRow("a", "Alpha", "manga", "inProgress", true, 9000, 1000),
                mkRow("b", "Beta",  "manga", "inProgress", false, 8000, 2000),
                mkRow("c", "Gamma", "comic", "inProgress", true, 7000, 3000),
                mkRow("d", "Delta", "manga", "notStarted", false, 0, 4000)
            ];
            function ids(rows) { return rows.map(function (r) { return r.entry.id; }).join(","); }

            // ── applyFilters ──
            // filter "" (All) → all 4
            var fAll = Api.applyFilters(tRows, { filter: "", query: "" });
            ok(fAll.length === 4, "TB-005 filter: '' (All) returns all rows: " + fAll.length);
            // "all" is treated as "" (case-insensitive) so the All pill's label is safe
            ok(Api.applyFilters(tRows, { filter: "all", query: "" }).length === 4, "TB-005 filter: 'all' == '' (All)");
            ok(Api.applyFilters(tRows, { filter: "ALL", query: "" }).length === 4, "TB-005 filter: 'ALL' == '' (case-insensitive)");

            // "inProgress" → Alpha, Beta, Gamma (Delta notStarted excluded)
            var fProg = Api.applyFilters(tRows, { filter: "inProgress", query: "" });
            ok(fProg.length === 3 && ids(fProg) === "a,b,c", "TB-005 filter: inProgress -> [Alpha,Beta,Gamma]: " + ids(fProg));

            // "downloaded" → Alpha, Gamma (only downloaded===true)
            var fDl = Api.applyFilters(tRows, { filter: "downloaded", query: "" });
            ok(fDl.length === 2 && ids(fDl) === "a,c", "TB-005 filter: downloaded -> [Alpha,Gamma]: " + ids(fDl));

            // search alone: case-insensitive substring on title
            var qBet = Api.applyFilters(tRows, { filter: "", query: "bet" });
            ok(qBet.length === 1 && qBet[0].entry.id === "b", "TB-005 search: 'bet' -> [Beta] (case-insensitive substring)");
            var qCap = Api.applyFilters(tRows, { filter: "", query: "GAMMA" });
            ok(qCap.length === 1 && qCap[0].entry.id === "c", "TB-005 search: 'GAMMA' -> [Gamma] (uppercase query matches lowercase title)");
            var qNone = Api.applyFilters(tRows, { filter: "", query: "zzz" });
            ok(qNone.length === 0, "TB-005 search: no-match query -> []");
            var qMulti = Api.applyFilters(tRows, { filter: "", query: "a" });   // Alpha, Beta, Gamma, Delta all contain 'a'
            ok(qMulti.length === 4 && ids(qMulti) === "a,b,c,d", "TB-005 search: 'a' substring matches all (Beta has 'a'): " + ids(qMulti));

            // AND-compose: filter + query together (both must pass)
            var fAnd = Api.applyFilters(tRows, { filter: "inProgress", query: "a" });
            // inProgress = {Alpha,Beta,Gamma}; all contain 'a' (Beta has 'a')
            ok(fAnd.length === 3 && ids(fAnd) === "a,b,c", "TB-005 AND-compose: inProgress + 'a' -> [Alpha,Beta,Gamma]: " + ids(fAnd));
            var fAndDl = Api.applyFilters(tRows, { filter: "downloaded", query: "a" });
            // downloaded = {Alpha,Gamma}; of those containing 'a' = {Alpha,Gamma}
            ok(fAndDl.length === 2 && ids(fAndDl) === "a,c", "TB-005 AND-compose: downloaded + 'a' -> [Alpha,Gamma]: " + ids(fAndDl));

            // empty/null inputs are safe (no throw)
            ok(Api.applyFilters([], { filter: "inProgress", query: "" }).length === 0, "TB-005 filter: empty rows -> []");
            ok(Api.applyFilters(null, { filter: "", query: "" }).length === 0, "TB-005 filter: null rows -> []");
            ok(Api.applyFilters(tRows, null).length === 4, "TB-005 filter: null state -> all rows (no filter, no query)");
            ok(Api.applyFilters(tRows, {}).length === 4, "TB-005 filter: empty state -> all rows");

            // ── sortRows ──
            // "lastRead" (default) → lastActivityAt desc: Alpha(9000), Beta(8000), Gamma(7000), Delta(0)
            var sLast = Api.sortRows(tRows, "lastRead");
            ok(ids(sLast) === "a,b,c,d", "TB-005 sort lastRead: by lastActivityAt desc: " + ids(sLast));
            // unknown mode falls back to lastRead
            var sUnknown = Api.sortRows(tRows, "bogus");
            ok(ids(sUnknown) === ids(sLast), "TB-005 sort: unknown mode falls back to lastRead");
            // null/undefined mode falls back to lastRead too
            ok(ids(Api.sortRows(tRows, "")) === ids(sLast), "TB-005 sort: empty mode -> lastRead default");

            // "added" → addedAt desc: Delta(4000), Gamma(3000), Beta(2000), Alpha(1000)
            var sAdded = Api.sortRows(tRows, "added");
            ok(ids(sAdded) === "d,c,b,a", "TB-005 sort added: by addedAt desc: " + ids(sAdded));

            // "az" → title asc, case-insensitive: Alpha, Beta, Delta, Gamma (ids a,b,d,c)
            var sAz = Api.sortRows(tRows, "az");
            ok(ids(sAz) === "a,b,d,c", "TB-005 sort az: by title asc: " + ids(sAz));

            // ── TB-005 purity: applyFilters + sortRows never mutate their inputs ──
            var tBefore = JSON.stringify(tRows);
            Api.applyFilters(tRows, { filter: "inProgress", query: "a" });
            Api.sortRows(tRows, "added");
            ok(JSON.stringify(tRows) === tBefore, "TB-005 purity: applyFilters + sortRows do not mutate the input rows array/objects");

            // sortRows returns a NEW array (not the same reference)
            var sRef = Api.sortRows(tRows, "lastRead");
            ok(sRef !== tRows, "TB-005 purity: sortRows returns a new array, not the input reference");

            // applyFilters returns a NEW array too
            var fRef = Api.applyFilters(tRows, { filter: "", query: "" });
            ok(fRef !== tRows, "TB-005 purity: applyFilters returns a new array, not the input reference");

            // ── TB-005 integration: applyFilters → sortRows pipeline (the page's exact call) ──
            // filter inProgress + sort az → Alpha, Beta, Gamma (alphabetical of the 3 in-progress)
            var pipe = Api.sortRows(Api.applyFilters(tRows, { filter: "inProgress", query: "" }), "az");
            ok(ids(pipe) === "a,b,c", "TB-005 pipeline: applyFilters(inProgress) -> sortRows(az) = [Alpha,Beta,Gamma]: " + ids(pipe));

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("tankoban_library_api_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
