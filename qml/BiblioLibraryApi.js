.pragma library

// BiblioLibraryApi — every pure derivation behind Biblio's Library tab (the Theatre-parity
// Library page, plan 2026-08-06-biblio-library-tab-theatre-parity.md). Fetch-free: ALL inputs
// are passed in (Collection/Progress live in C++/QML, never here), so the whole module is
// provable headless. Proven by tests/biblio_library_api_harness.qml.
//
// This is the book-domain mirror of LibraryApi.js, MINUS Theatre's video concepts: no
// watched/airing/finale/new-episode logic. Saved entries and local audiobook recordings
// share the shelf. Conservative
// Progress matching: reliable existing match enables Resume; uncertain match → Details.

// buildBiblioRows — the one live snapshot the page renders. Joins Collection entries with
// book Progress (Progress.recent("book", N)). Input-pure: the QML call site fetches both
// and passes them in alongside downloaded audiobook records and their progress lookup.
//   entries      = Collection.items("biblio")           (each carries id, title, cover, payload, world, addedAt)
//   progressList = Progress.recent("book", 200)          (each carries id, kind, progress, updatedAt, resume?)
// A row is a read-only projection — the page never persists it. Fields:
//   { entry, progressRecord, title, author, cover, progress, downloaded, canResume, lastReadAt, addedAt }
function buildBiblioRows(entries, progressList, downloads, audiobookProgress) {
    entries = entries || [];
    progressList = progressList || [];
    var rows = [];
    var savedAudiobooks = [];
    for (var i = 0; i < entries.length; i++) {
        var e = entries[i];
        if (!e || !e.id) continue;
        if (e.payload && e.payload.kind === "audiobook") {
            savedAudiobooks.push(e);
            continue;
        }
        var pm = _matchProgress(String(e.id), progressList);
        var rawProgress = pm ? Number(pm.progress || 0) : 0;
        if (isNaN(rawProgress)) rawProgress = 0;
        var lastReadAt = pm ? Number(pm.updatedAt || 0) : 0;
        if (!lastReadAt) lastReadAt = Number(e.addedAt || 0);
        rows.push({
            entry: e,
            medium: "book",
            progressRecord: pm || null,
            title: String(e.title || ""),
            author: _authorOf(e),
            cover: String(e.cover || (e.payload && e.payload.book && e.payload.book.cover) || ""),
            progress: rawProgress,
            downloaded: false,        // no honest availability source in v1 (plan §7) — left false, indicator omitted
            canResume: _canResume(pm),
            lastReadAt: lastReadAt,
            addedAt: Number(e.addedAt || 0)
        });
    }
    var matchedSaves = Object.create(null);
    var localRecordings = Array.prototype.filter.call(downloads || [], function(download) {
        return download && (download.recordingKey || download.id);
    });
    localRecordings.sort(function(a, b) {
        return Number(!!a.missing) - Number(!!b.missing)
            || Number(b.addedAt || 0) - Number(a.addedAt || 0);
    });
    for (var j = 0; j < localRecordings.length; j++) {
        var download = localRecordings[j];
        var saved = matchSavedAudiobook(download, savedAudiobooks);
        // One save can annotate a local recording. Keep additional downloads as distinct cards.
        var matched = saved && !matchedSaves[saved.id] ? saved : null;
        if (matched) matchedSaves[matched.id] = true;
        if (!download.missing || matched)
            rows.push(audiobookRow(matched, download, audiobookProgress));
    }
    for (var k = 0; k < savedAudiobooks.length; k++) {
        var entry = savedAudiobooks[k];
        if (!matchedSaves[entry.id]) rows.push(audiobookRow(entry, null, audiobookProgress));
    }
    return rows;
}

function matchSavedAudiobook(download, savedAudiobooks) {
    var recordingId = download.audiosiloRecordingId || download.recordingId;
    // Catalogue recording IDs are scoped to a work. The legacy download index has
    // title/author and pairKey but no canonical work ID.
    var downloadTitle = normalizedCredit(download.title);
    var downloadAuthor = normalizedCredit(download.author);
    var match = null;
    for (var i = 0; i < savedAudiobooks.length; i++) {
        var entry = savedAudiobooks[i];
        var saved = entry.payload;
        if (!recordingId || saved.recordingId !== recordingId) continue;
        var sameWork = download.workId ? download.workId === saved.workId
            : (download.pairKey && saved.pairKey ? download.pairKey === saved.pairKey
               : downloadTitle && downloadAuthor
                 && normalizedCredit(entry.title) === downloadTitle
                 && normalizedCredit(saved.author) === downloadAuthor);
        if (!sameWork) continue;
        if (match) return null;
        match = entry;
    }
    return match;
}

function normalizedCredit(value) {
    return String(value || "").trim().replace(/\s+/g, " ").toLowerCase();
}

function audiobookRow(saved, download, audiobookProgress) {
    var payload = saved ? saved.payload : {};
    var local = download || {};
    var recordingKey = String(local.recordingKey || local.id || "");
    var progressKey = recordingKey || payload.recordingKey;
    var record = progressKey && audiobookProgress ? audiobookProgress(progressKey) : null;
    var title = String((saved && saved.title) || local.title || "");
    var author = String(payload.author || local.author || "");
    var cover = String((saved && saved.cover) || payload.cover || local.coverUrl || local.cover
                       || (record && record.cover) || "");
    var addedAt = Math.max(Number((saved && saved.addedAt) || 0), Number(local.addedAt || 0));
    var downloaded = !!download && !download.missing;
    var route = Object.assign({}, local, {
        workId: String(payload.workId || local.workId || ""),
        recordingId: String(payload.recordingId || local.audiosiloRecordingId || local.recordingId || ""),
        recordingKey: recordingKey,
        title: title, author: author, coverUrl: cover, downloaded: downloaded
    });
    return {
        entry: saved || { id: "audiobook:download:" + recordingKey, title: title },
        medium: "audiobook", title: title, author: author, cover: cover,
        progressRecord: record || null,
        progress: Math.max(0, Math.min(1, Number(record && record.progress) || 0)),
        listeningState: listeningState(record),
        downloaded: downloaded, canResume: false, canRemove: !!saved,
        lastReadAt: Number((record && record.updatedAt) || local.lastPlayedAt || addedAt),
        addedAt: addedAt, audiobook: route
    };
}

function listeningState(record) {
    if (record === null || record === undefined) return "new";
    if (record.completed === true || record.progress >= 0.98) return "finished";
    if ((record.progress || 0) === 0
            && !(record.resume && record.resume.position > 0)
            && !(record.resume && record.resume.fileIndex > 0)) return "new";
    return "started";
}

// Conservative match, first wins:
//  (1) exact entry.id === progress.id;
//  (2) a stable book/work id present in progress metadata (progress.workId / progress.bookId)
//      matching entry.id — only if the Progress record already carries one;
//  (3) title AND author equality on both sides — only if BOTH sides carry BOTH fields.
// No match → null (row keeps progressRecord:null, canResume:false, progress:0).
// Conservative by design (plan §6): uncertain identity falls back to Details, not new infra.
function _matchProgress(entryId, progressList) {
    for (var i = 0; i < progressList.length; i++) {
        var p = progressList[i];
        if (!p) continue;
        var pid = String(p.id || "");
        // (1) exact id
        if (pid === entryId) return p;
        // (2) stable work id carried in progress metadata
        var workId = String(p.workId || p.bookId || "");
        if (workId && workId === entryId) return p;
    }
    return null;
}

// (3) title+author fallback — invoked explicitly by the page ONLY when an id match misses
// AND both sides carry both fields. Kept as a separate function so the harness can prove the
// id-match branch never accidentally triggers it, and the page can opt IN deliberately.
function matchByTitleAuthor(entry, progressList) {
    if (!entry) return null;
    var et = String(entry.title || "").trim().toLowerCase();
    var ea = _authorOf(entry).trim().toLowerCase();
    if (!et || !ea) return null;            // both fields required on the entry side
    for (var i = 0; i < progressList.length; i++) {
        var p = progressList[i];
        if (!p) continue;
        var pt = String(p.title || "").trim().toLowerCase();
        var pa = String((p.author || (p.resume && p.resume.book && p.resume.book.author)) || "").trim().toLowerCase();
        if (pt && pa && pt === et && pa === ea) return p;
    }
    return null;
}

// Resume is honest only when a matched record carries a reopen payload the existing route can use:
//   resume.path (a downloaded local file) OR resume.book (the full book object for openBook).
// A Progress record without a resume payload → canResume:false → the card's action is Details.
function _canResume(pm) {
    if (!pm || !pm.resume) return false;
    return !!(pm.resume.path || pm.resume.book);
}

function _authorOf(entry) {
    if (!entry) return "";
    if (entry.author) return String(entry.author);
    var b = entry.payload && entry.payload.book;
    return String((b && b.author) || "");
}

// applyBiblioFilters — AND-compose the state fragment and the search needle.
//   state.stateFilter: "" (all) | "inProgress" (progress>0) | "downloaded" (only if an honest
//     availability source is wired in the page; until then the page OMITS this filter and the
//     branch here is inert — left in so adding the source later needs no API change)
//   state.query: case-insensitive substring against title OR author
function applyBiblioFilters(rows, state) {
    state = state || {};
    var sf = state.stateFilter || "";
    var medium = state.mediumFilter || "";
    var q = String(state.query || "").trim().toLowerCase();
    var out = [];
    for (var i = 0; i < (rows || []).length; i++) {
        var r = rows[i];
        if (medium && (r.medium || "book") !== medium) continue;
        if (sf === "inProgress" && !(r.medium === "audiobook" ? r.listeningState === "started" : r.progress > 0))
            continue;
        if (sf === "downloaded" && !r.downloaded) continue;
        if (q) {
            var title = String(r.title || "").toLowerCase();
            var author = String(r.author || "").toLowerCase();
            if (title.indexOf(q) === -1 && author.indexOf(q) === -1) continue;
        }
        out.push(r);
    }
    return out;
}

// sortBiblioRows — returns a NEW ordered array; never mutates the input.
//   "added" (default): addedAt desc
//   "lastRead":        lastReadAt desc, addedAt fallback
//   "az":              title, case-insensitive
function sortBiblioRows(rows, mode) {
    var out = (rows || []).slice();
    function num(x) { return (typeof x === "number" && !isNaN(x)) ? x : 0; }
    if (mode === "lastRead") {
        out.sort(function (a, b) {
            var la = num(a.lastReadAt), lb = num(b.lastReadAt);
            if (la !== lb) return lb - la;
            return num(b.addedAt) - num(a.addedAt);
        });
    } else if (mode === "az") {
        out.sort(function (a, b) {
            var ta = String(a.title || "").toLowerCase();
            var tb = String(b.title || "").toLowerCase();
            return ta < tb ? -1 : (ta > tb ? 1 : 0);
        });
    } else { // "added" (default)
        out.sort(function (a, b) { return num(b.addedAt) - num(a.addedAt); });
    }
    return out;
}
