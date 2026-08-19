.pragma library

// Host-side formatting for the "Your Colosseum" Monthly Portrait
// (CPP-PORT-CONTRACT.md arcs/02-profile-account-centre/activity-engine/reference,
// section 13 "Exact projector output" / section 14 "QML model contract").
//
// qml/account/AccountCenter.qml (Task D10) is the ONLY caller of the projection-cache
// function (projectionFor) and the ONLY place that owns colosseumMonthKey. Every function
// below is pure — no QObject, no context-property access, no Date()-driven reactivity beyond
// the one-shot currentMonthKey() the host calls exactly once per open() — so this file is
// trivially unit-testable from tests/qml/tst_account_activity_binding.qml with no component
// tree, mirroring the existing qml/ActivityLaneHelpers.js / qml/ComicActivityHelpers.js rule:
// pure decision logic lives in ONE pragma-library module the host calls, not reimplemented
// twice.
//
// Determinism (contract section 21): projectMonth() itself already speaks in explicit local
// date strings ("YYYY-MM-DD") and integer seconds/counts/micros, so nothing here parses those
// through `new Date(...)` or a Qt locale — nothing below can vary with the current machine
// timezone, locale, or clock (the one exception, currentMonthKey(), is the deliberate single
// "current month" read the contract itself calls for in section 12).

var MONTH_NAMES = [
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
]

var MONTH_ABBR = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
]

var DASH = "—"

var COMPLETION_UNIT = {
    "movie": ["title", "titles"],
    "episode": ["episode", "episodes"],
    "manga_chapter": ["chapter", "chapters"],
    "comic_issue": ["issue", "issues"],
    "tankoban_volume": ["volume", "volumes"],
    "book": ["book", "books"],
    "audiobook": ["audiobook", "audiobooks"]
}

// ---- month key helpers ("YYYY-MM") -----------------------------------------------------

function isMonthKey(key) {
    return typeof key === "string" && /^\d{4}-(0[1-9]|1[0-2])$/.test(key)
}

// Section 12: "initial selected month = current system-local month" and section 14:
// "computed once at open (not a live clock binding)". AccountCenter.qml calls this ONLY from
// open() when transitioning closed->visible, never from inside a reactive property binding,
// so evaluating `new Date()` here never becomes a per-frame clock dependency.
function currentMonthKey() {
    var now = new Date()
    var month = now.getMonth() + 1
    return now.getFullYear() + "-" + (month < 10 ? "0" + month : String(month))
}

function monthName(monthKey) {
    if (!isMonthKey(monthKey))
        return ""
    return MONTH_NAMES[Number(monthKey.slice(5, 7)) - 1]
}

function monthYear(monthKey) {
    if (!isMonthKey(monthKey))
        return ""
    return monthKey.slice(0, 4)
}

// One month before/after "YYYY-MM", wrapping the year. Pure integer math, deliberately not
// `new Date(...)` (month-length/DST semantics are irrelevant to a "YYYY-MM" key).
function shiftMonthKey(monthKey, delta) {
    if (!isMonthKey(monthKey))
        return ""
    var year = Number(monthKey.slice(0, 4))
    var zeroBasedMonth = Number(monthKey.slice(5, 7)) - 1 + delta
    year += Math.floor(zeroBasedMonth / 12)
    zeroBasedMonth = ((zeroBasedMonth % 12) + 12) % 12
    var month = zeroBasedMonth + 1
    return year + "-" + (month < 10 ? "0" + month : String(month))
}

// "YYYY-MM" sorts lexically identical to chronological order.
function compareMonthKeys(a, b) {
    if (a === b)
        return 0
    return a < b ? -1 : 1
}

// Section 12 month navigation: "next month enabled only while selected month is before
// current month". `currentKey` is the host's frozen colosseumCurrentMonthKey ceiling (set at
// the same open() moment as colosseumMonthKey), not a live re-read of today's date.
function nextMonthEnabled(selectedMonthKey, currentKey) {
    if (!isMonthKey(selectedMonthKey) || !isMonthKey(currentKey))
        return false
    return compareMonthKeys(selectedMonthKey, currentKey) < 0
}

// Section 12: "previous month stops at ProfileActivity.earliestActivityMonth() when one
// exists". An empty/invalid earliest key means the store has no known floor yet (or is
// unavailable), so previous stays enabled (fails open toward navigation, not toward an
// incorrectly locked control — the empty-month projection itself is always valid, section 23).
function previousMonthEnabled(selectedMonthKey, earliestMonthKey) {
    if (!isMonthKey(selectedMonthKey))
        return false
    if (!isMonthKey(earliestMonthKey))
        return true
    return compareMonthKeys(selectedMonthKey, earliestMonthKey) > 0
}

// ---- projection-cache rule (section 14/24) ----------------------------------------------

// The EXACT algorithm from CPP-PORT-CONTRACT.md section 14's "Recommended binding": null-guard,
// read `.revision` (this is what makes the QML binding that calls this function re-evaluate
// when ActivityStore changes — QML's property-capture tracks QObject property reads through
// nested JS calls, not just the top-level binding expression), then call projectMonth()
// exactly once. Lives here (not copy-pasted inline in AccountCenter.qml) so
// tst_account_activity_binding.qml can drive it directly against a recording fake and prove
// the "one projectMonth call per month/revision, never per metric read" rule without
// instantiating the whole composed Account Centre host.
function projectionFor(activityStore, monthKey) {
    if (!activityStore)
        return ({})
    void activityStore.revision
    return activityStore.projectMonth(monthKey)
}

// ---- metric text --------------------------------------------------------------------------

// Localized-looking thousands grouping ("1,284") without a Qt.locale()/toLocaleString()
// dependency, so repeated runs never vary with the current machine locale (section 21).
function countText(value) {
    if (value === undefined || value === null)
        return DASH
    var n = Number(value)
    if (!isFinite(n))
        return DASH
    var rounded = Math.round(Math.abs(n))
    var grouped = String(rounded).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    return (n < 0 ? "-" : "") + grouped
}

function pagesText(count) {
    var n = Number(count || 0)
    return countText(n) + (n === 1 ? " page" : " pages")
}

// seconds -> "37h 24m" (section 14's own example). Floor-based (no rounding into the next
// unit) — verified against the contract's own worked example: 134640s -> 37h 24m.
function durationText(totalSeconds) {
    if (totalSeconds === undefined || totalSeconds === null)
        return DASH
    var seconds = Number(totalSeconds)
    if (!isFinite(seconds) || seconds < 0)
        return DASH
    var totalMinutes = Math.floor(seconds / 60)
    var hours = Math.floor(totalMinutes / 60)
    var minutes = totalMinutes % 60
    return hours > 0 ? (hours + "h " + minutes + "m") : (minutes + "m")
}

// progressMicros (millionths of one whole reflowable book, section 6) -> percent text.
// Section 13/14: never label this as physical pages.
function percentText(progressMicros) {
    if (progressMicros === undefined || progressMicros === null)
        return DASH
    var micros = Number(progressMicros)
    if (!isFinite(micros) || micros < 0)
        return DASH
    return Math.round(micros / 10000) + "%"
}

function pluralUnit(kind, count) {
    var pair = COMPLETION_UNIT[kind] || ["item", "items"]
    return count === 1 ? pair[0] : pair[1]
}

function completionValueText(highlight) {
    var count = Number((highlight && highlight.completedCount) || 0)
    return countText(count) + " " + pluralUnit(highlight ? highlight.kind : "", count)
}

// Section 13: "For reflowable Biblio highlights, do not label progressMicros as literal
// physical pages." pagesRead > 0 means at least one dedupe-qualified FIXED physical page
// contributed to this title's Biblio total this month (section 10), so page copy is truthful;
// otherwise the title's only Biblio contribution this month was reflowable progressMicros, and
// the value must read as reading progress, never invented pages.
function biblioValueText(highlight) {
    var pages = Number((highlight && highlight.pagesRead) || 0)
    if (pages > 0)
        return pagesText(pages)
    return percentText(highlight ? highlight.progressMicros : undefined)
}

// Fallback "recent" role (section 13 buildHighlights' four-way-tie/underfilled fallback): the
// title won no metric role outright, so surface whichever qualifying metric it actually has —
// never invent a cross-kind score comparing minutes to pages (section 13 explicit rule).
function recentValueText(highlight) {
    if (!highlight)
        return DASH
    if (Number(highlight.watchSeconds || 0) > 0)
        return durationText(highlight.watchSeconds)
    if (Number(highlight.listenSeconds || 0) > 0)
        return durationText(highlight.listenSeconds)
    if (Number(highlight.pagesRead || 0) > 0)
        return pagesText(highlight.pagesRead)
    if (Number(highlight.progressMicros || 0) > 0)
        return percentText(highlight.progressMicros)
    if (Number(highlight.completedCount || 0) > 0)
        return completionValueText(highlight)
    return DASH
}

// Section 13 highlight role order (Theatre -> Tankoban/comics -> Biblio ebook -> Audiobook ->
// Completion) mapped to product copy. The "Most read manga"/"Most read comics"/"Most read
// book" wording matches the already-authored/locked Your Colosseum mock's own illustrative
// copy (tests/qml/tst_account_your_colosseum.qml's fixture; only the ILLUSTRATIVE Blue Eye
// Samurai/One Piece/Dune/Berserk VALUES are barred from production, section 23 — the label
// conventions those mocks authored are the product copy).
function highlightLabel(highlight) {
    var role = highlight ? highlight.role : ""
    if (role === "theatre")
        return "Most watched"
    if (role === "tankoban") {
        if (highlight.kind === "manga_chapter")
            return "Most read manga"
        if (highlight.kind === "comic_issue")
            return "Most read comics"
        return "Most read"
    }
    if (role === "biblio_ebook")
        return "Most read book"
    if (role === "audiobook")
        return "Most listened"
    if (role === "completion")
        return "Completed"
    return "Recently active"
}

function highlightValue(highlight) {
    var role = highlight ? highlight.role : ""
    if (role === "theatre")
        return durationText(highlight.watchSeconds)
    if (role === "tankoban")
        return pagesText(highlight.pagesRead)
    if (role === "biblio_ebook")
        return biblioValueText(highlight)
    if (role === "audiobook")
        return durationText(highlight.listenSeconds)
    if (role === "completion")
        return completionValueText(highlight)
    return recentValueText(highlight)
}

// Raw highlight -> AccountYourColosseumPage.qml's {title,label,value} presentation contract.
function formatHighlight(highlight) {
    return {
        "title": (highlight && highlight.title) ? String(highlight.title) : "",
        "label": highlightLabel(highlight),
        "value": highlight ? highlightValue(highlight) : DASH
    }
}

function formatHighlights(highlights) {
    if (!Array.isArray(highlights))
        return []
    return highlights.map(formatHighlight)
}

// ---- Recent Activity row -> {date,title,meta,world} --------------------------------------

// projectMonth()'s own "YYYY-MM-DD" localDate -> "Aug 16". Deliberately string slicing, NOT
// `new Date(localDate)` — re-parsing through Date would reintroduce the machine-timezone
// dependency the projector already resolved into that local date string (section 21).
function shortDateText(localDate) {
    if (typeof localDate !== "string" || localDate.length !== 10)
        return DASH
    var month = Number(localDate.slice(5, 7))
    var day = Number(localDate.slice(8, 10))
    if (!(month >= 1 && month <= 12) || !(day >= 1 && day <= 31))
        return DASH
    return MONTH_ABBR[month - 1] + " " + day
}

function worldText(world) {
    if (world === "theatre")
        return "Theatre"
    if (world === "tankoban")
        return "Tankoban"
    if (world === "biblio")
        return "Biblio"
    return DASH
}

// verb/completed -> the feed's short past-tense line. "Finished" wins over the verb whenever
// this moment includes the item's completion (activity-reference.js's own moment.completed
// flag), matching the locked mock's "Finished Episode 8" example.
function activityMetaText(row) {
    if (!row)
        return ""
    if (row.completed)
        return row.itemLabel ? ("Finished " + row.itemLabel) : "Finished"
    if (row.verb === "watched") {
        return Number(row.watchSeconds || 0) > 0
            ? ("Watched " + durationText(row.watchSeconds))
            : (row.itemLabel || "Watched")
    }
    if (row.verb === "listened") {
        return Number(row.listenSeconds || 0) > 0
            ? ("Listened " + durationText(row.listenSeconds))
            : (row.itemLabel || "Listened")
    }
    // verb === "read" (Tankoban/comics fixed pages or Biblio reflowable progress).
    if (Number(row.pagesRead || 0) > 0)
        return "Read " + pagesText(row.pagesRead)
    if (Number(row.progressMicros || 0) > 0)
        return "Read " + percentText(row.progressMicros)
    return row.itemLabel || "Read"
}

function formatActivityRow(row) {
    return {
        "date": shortDateText(row ? row.localDate : ""),
        "title": (row && row.title) ? String(row.title) : "",
        "meta": activityMetaText(row),
        "world": worldText(row ? row.world : "")
    }
}

function formatRecentActivity(rows) {
    if (!Array.isArray(rows))
        return []
    return rows.map(formatActivityRow)
}
