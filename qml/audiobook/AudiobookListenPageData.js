.pragma library
// AudiobookListenPageData — maps the app-wide AudiobookSession plus a book record onto the
// `pageData` contract that Vinyl's song page and player bar read. Pure helpers: nothing here
// touches the engine except through the session object handed in.
//
// Every Vinyl-only field is returned at its "off" value (no video, no lyrics, no torrent or
// YouTube choice, no queue, no Genius). Absent book fields stay absent: nothing is invented.

// The reader's speed ladder (ReaderShell.audioSpeeds) and its skip step (HudGlyph "10").
var SPEEDS = [1.0, 1.25, 1.5, 1.75, 2.0, 0.75]
var SKIP_SECONDS = 10
// The reader has no sleep control UI; this page offers off + these lengths (session.sleepMinutes).
var SLEEP_STEPS = [0, 15, 30, 45, 60]

function text(v) { return v === undefined || v === null ? "" : String(v) }

function list(v) {
    if (!v) return []
    var out = [], src = (typeof v === "string") ? [v] : v
    for (var i = 0; i < src.length; i++) {
        var s = text(src[i]).trim()
        if (s.length) out.push(s)
    }
    return out
}

function chapters(session) { return (session && session.chapterModel) ? session.chapterModel : [] }

// Index of the chapter now playing. A multi-file set plays one file per chapter; a single
// file (m4b) is located by its embedded chapter start times. -1 when nothing is loaded.
function chapterIndex(session) {
    var ch = chapters(session)
    if (!session || !session.ready || ch.length === 0) return -1
    if (ch[0].kind === "file") {
        var i = Number(session.currentIndex)
        return (i >= 0 && i < ch.length) ? i : 0
    }
    var pos = Number(session.position) || 0, found = 0
    for (var j = 0; j < ch.length; j++) {
        if ((Number(ch[j].time) || 0) <= pos + 0.25) found = j
    }
    return found
}

function chapterTitle(session, book, index) {
    var ch = chapters(session)
    if (index >= 0 && index < ch.length && text(ch[index].label).length) return text(ch[index].label)
    return text(book && book.title)
}

function canPrevious(session) { return chapterIndex(session) > 0 }
function canNext(session) {
    var i = chapterIndex(session)
    return i >= 0 && i < chapters(session).length - 1
}

function formatTime(seconds) {
    var s = Math.floor(Number(seconds))
    if (!isFinite(s) || s < 0) s = 0
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60
    var pad = function (n) { return (n < 10 ? "0" : "") + n }
    return h > 0 ? h + ":" + pad(m) + ":" + pad(r) : m + ":" + pad(r)
}

function speedLabel(rate) {
    var r = Number(rate)
    if (!isFinite(r) || r <= 0) r = 1
    return r.toFixed(2).replace(/(\.\d)0$/, "$1") + "\u00d7"
}

function nextSpeed(current) {
    for (var i = 0; i < SPEEDS.length; i++)
        if (Math.abs(SPEEDS[i] - Number(current)) < 0.01) return SPEEDS[(i + 1) % SPEEDS.length]
    return SPEEDS[0]
}

function nextSleep(current) {
    var i = SLEEP_STEPS.indexOf(Number(current))
    return SLEEP_STEPS[(i + 1) % SLEEP_STEPS.length]   // unknown value (-1) restarts at 0
}

function sleepLabel(minutes) { return Number(minutes) > 0 ? Number(minutes) + " min" : "Sleep" }

// A local file path becomes a file URL; anything already a URL is kept; empty stays empty.
function coverUrl(cover) {
    var c = text(cover)
    if (!c.length || /^(file:|qrc:|https?:|data:)/.test(c)) return c
    if (/^[A-Za-z]:[\\/]/.test(c)) return "file:///" + c.replace(/\\/g, "/")
    return ""
}

function authorsLine(book) { return list(book && book.authors).join(" \u00b7 ") }

// The edition a Vinyl page shows: the book itself. Artists are the authors; narrators ride
// along only when given.
function edition(book) {
    var b = book || ({})
    var authors = list(b.authors)
    return {
        id: text(b.id), title: text(b.title), name: text(b.title),
        artist: authors.join(" \u00b7 "), artists: authors,
        narrators: list(b.narrators), cover: text(b.cover),
        origin: "local",   // AudiobookSession only ever plays files already on this machine
        hasLyrics: false, hasNotes: false, hasVideo: false
    }
}

// Vinyl-only fields at their off values.
function offFields() {
    return {
        lyricsShown: false, autoNotes: false, videoShown: false, hasLyrics: false, hasVideo: false,
        lyricsSource: "", lyricsDocumentId: "", lyricsStatus: "", lyricsError: "",
        timedLyrics: false, lyrics: [], currentLineIndex: -1,
        notes: ({}), activeNoteLine: -1, manualNoteLine: -1,
        annotationsStatus: "", annotationCount: 0, geniusSongId: "", readingContextKey: "",
        personalEnabled: false, songIsSaved: false,
        sourceKind: "", torrentAvailable: false, torrentBusy: false, torrentError: "",
        videoBusy: false, videoError: "", queueEnabled: false
    }
}

function build(session, book) {
    var idx = chapterIndex(session)
    var snap = offFields()
    snap.playingTrack = { title: chapterTitle(session, book, idx), available: true,
                          artists: list(book && book.authors) }
    snap.playingEdition = edition(book)
    snap.playingEditionId = text(book && book.id)
    snap.playingIndex = idx
    snap.position = session ? Number(session.position) || 0 : 0
    snap.duration = session ? Number(session.duration) || 0 : 0
    snap.paused = session ? session.paused === true : true
    snap.loaded = !!(session && session.ready)
    snap.sourceState = snap.loaded ? "playing" : "idle"
    snap.canNext = canNext(session)
    snap.canPrevious = canPrevious(session)
    snap.seek = function (t) { session.seekTo(t) }
    snap.togglePlay = function () { session.togglePlay() }
    snap.next = function (delta) {
        var i = chapterIndex(session), target = i + (delta < 0 ? -1 : 1)
        if (i >= 0 && target >= 0 && target < chapters(session).length) session.goToChapter(target)
    }
    snap.formatTime = formatTime
    snap.artwork = coverUrl
    return snap
}
