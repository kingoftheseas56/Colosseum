// Offscreen proof of BiblioLibraryApi's pure derivations (plan 2026-08-06-biblio-library-tab-
// theatre-parity.md, Slice 1). NEVER throw (hangs offscreen); collect fails, print
// BIBLIO_LIBRARY_API_OK only when clean, Qt.exit(fails.length). Mirrors library_api_harness.qml.
import QtQuick
import "../qml/BiblioLibraryApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            // ── fixtures: Collection entries (world:"biblio") + book Progress ──
            function ent(id, title, author, cover, addedAt, payloadExtra) {
                var p = { book: { author: author, cover: cover } };
                if (payloadExtra) for (var k in payloadExtra) p[k] = payloadExtra[k];
                return { id: id, type: "book", title: title, cover: cover, payload: p,
                    world: "biblio", addedAt: addedAt };
            }
            var eMatch = ent("b1", "The Wheel of Time", "Robert Jordan", "wot.jpg", 5000);
            var ePlain = ent("b2", "A Plain Saved Book", "Anon", "plain.jpg", 4000);
            var eWork = ent("b3", "Work-Id Match", "Author C", "work.jpg", 3000, { book: { author: "Author C", cover: "work.jpg", workId: "b3" } });
            var eRecent = ent("b4", "Most Recent", "Author D", "recent.jpg", 9000);
            var entries = [eMatch, ePlain, eWork, eRecent];

            // b1: exact-id match WITH a resume payload (downloaded path) → canResume true
            // b3: matches via progress.workId (stable work id) WITH resume.book → canResume true
            // b4: matches by exact id but resume absent → canResume false (Details fallback)
            // b2: no progress at all → canResume false, progressRecord null
            var plist = [
                { id: "b1", kind: "book", progress: 0.3, updatedAt: 8000,
                  resume: { path: "C:/books/wot.epub", book: { title: "The Wheel of Time", author: "Robert Jordan" } },
                  title: "The Wheel of Time", author: "Robert Jordan" },
                { id: "x:legacy", workId: "b3", kind: "book", progress: 0.5, updatedAt: 7000,
                  resume: { book: { title: "Work-Id Match", author: "Author C" } },
                  title: "Work-Id Match", author: "Author C" },
                { id: "b4", kind: "book", progress: 0.1, updatedAt: 8500,
                  title: "Most Recent", author: "Author D" }
            ];

            // ── buildBiblioRows: one entry → one row ──
            var rows = Api.buildBiblioRows(entries, plist);
            ok(rows.length === 4, "one entry → one row (got " + rows.length + ")");
            ok(rows[0].entry.id === "b1", "buildBiblioRows preserves entry identity (row.entry.id unchanged)");

            // ── conservative matching ──
            function rowById(id) { for (var i = 0; i < rows.length; i++) if (rows[i].entry.id === id) return rows[i]; return null; }
            var rMatch = rowById("b1");
            ok(rMatch && rMatch.canResume === true && rMatch.progress === 0.3 && rMatch.progressRecord !== null,
               "exact-id match enables Resume (canResume=true, progress=0.3)");
            var rWork = rowById("b3");
            ok(rWork && rWork.canResume === true && rWork.progress === 0.5,
               "workId match enables Resume (canResume=true, progress=0.5)");
            var rRecent = rowById("b4");
            ok(rRecent && rRecent.canResume === false && rRecent.progress === 0.1 && rRecent.progressRecord !== null,
               "matched but no resume payload → canResume=false (Details fallback), progress still shown");
            var rPlain = rowById("b2");
            ok(rPlain && rPlain.canResume === false && rPlain.progressRecord === null && rPlain.progress === 0,
               "unmatched → canResume=false, progressRecord=null, progress=0");

            // ── author + cover projection ──
            ok(rMatch.author === "Robert Jordan", "author projected from payload.book.author");
            ok(rMatch.cover === "wot.jpg", "cover projected from entry.cover");

            // ── lastReadAt: progress.updatedAt wins, addedAt fallback ──
            ok(rMatch.lastReadAt === 8000, "lastReadAt = progress.updatedAt when matched");
            ok(rPlain.lastReadAt === 4000, "lastReadAt falls back to addedAt when no progress");

            // ── matchByTitleAuthor: the explicit opt-in fallback (not auto-fired by buildBiblioRows) ──
            // b2 has no id match; buildBiblioRows left it unmatched. A title+author match recovers it.
            var ta = Api.matchByTitleAuthor(ePlain, plist.concat([
                { id: "z:other", kind: "book", title: "A Plain Saved Book", author: "Anon", progress: 0.2, updatedAt: 6000 }
            ]));
            ok(ta && ta.id === "z:other", "matchByTitleAuthor finds a record by title+author");
            ok(Api.matchByTitleAuthor(ePlain, []) === null, "matchByTitleAuthor returns null on empty list");
            // both fields required on the entry side: strip author → no match
            var eNoAuthor = { id: "bX", title: "Title Only", payload: { book: {} }, world: "biblio", addedAt: 1 };
            ok(Api.matchByTitleAuthor(eNoAuthor, [{ id: "y", title: "Title Only", author: "Someone" }]) === null,
               "matchByTitleAuthor requires author on the entry side");

            // ── applyBiblioFilters ──
            function ids(rs) { return rs.map(function (r) { return r.entry.id; }).sort().join(","); }
            ok(ids(Api.applyBiblioFilters(rows, {})) === "b1,b2,b3,b4", "empty filter = all");
            ok(ids(Api.applyBiblioFilters(rows, { stateFilter: "inProgress" })) === "b1,b3,b4",
               "inProgress filter keeps progress>0 (b2 has none): " + ids(Api.applyBiblioFilters(rows, { stateFilter: "inProgress" })));
            // search matches title OR author (case-insensitive)
            ok(ids(Api.applyBiblioFilters(rows, { query: "wheel" })) === "b1", "search by title (ci)");
            ok(ids(Api.applyBiblioFilters(rows, { query: "ROBERT" })) === "b1", "search by author (ci)");
            ok(ids(Api.applyBiblioFilters(rows, { query: "author c" })) === "b3", "search by author 'author c'");
            ok(ids(Api.applyBiblioFilters(rows, { query: "nomatchstring" })) === "", "search with no hits = empty");
            // compose filter + query
            ok(ids(Api.applyBiblioFilters(rows, { stateFilter: "inProgress", query: "recent" })) === "b4",
               "compose inProgress + query");

            // ── sortBiblioRows ──
            var byAdded = Api.sortBiblioRows(rows, "added");
            ok(byAdded[0].entry.id === "b4" && byAdded[3].entry.id === "b3",
               "sort added desc: " + byAdded.map(function (r) { return r.entry.id; }).join(","));
            var byRead = Api.sortBiblioRows(rows, "lastRead");
            // lastReadAt: b4=8500, b1=8000, b3=7000, b2=4000(addedAt fallback)
            ok(byRead[0].entry.id === "b4" && byRead[3].entry.id === "b2",
               "sort lastRead desc: " + byRead.map(function (r) { return r.entry.id; }).join(","));
            var byAz = Api.sortBiblioRows(rows, "az");
            ok(byAz[0].title === "A Plain Saved Book" && byAz[1].title === "Most Recent",
               "sort A–Z: " + byAz.map(function (r) { return r.title; }).join(","));
            // default mode = added
            var byDefault = Api.sortBiblioRows(rows);
            ok(byDefault[0].entry.id === "b4", "default sort = added desc");
            // sortBiblioRows must not mutate the input order
            ok(rows[0].entry.id === "b1", "sortBiblioRows returns a copy (no mutation)");

            // ── Remove uses the original Collection entry: row.entry is the live entry object ──
            ok(rMatch.entry === eMatch, "row.entry is the original Collection entry (remove acts on Collection membership)");

            // ── empty inputs are safe ──
            ok(Api.buildBiblioRows([], []).length === 0, "empty entries → 0 rows");
            ok(Api.buildBiblioRows(null, null).length === 0, "null inputs → 0 rows");
            ok(Api.applyBiblioFilters([], { query: "x" }).length === 0, "filter on empty → empty");
            ok(Api.sortBiblioRows([], "added").length === 0, "sort on empty → empty");

            // ── downloaded stays false in v1 (no honest availability source) ──
            ok(rows.every(function (r) { return r.downloaded === false; }),
               "downloaded=false for all rows in v1 (indicator omitted per plan §7)");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("biblio_library_api_harness: ALL PASS\nBIBLIO_LIBRARY_API_OK");
            Qt.exit(fails.length);
        }
    }
}
