// CalendarApi.js — pure derivations for the Theatre Calendar (Stage 3, spec §5).
// Your saved series' episode schedule → the Coming-up rail, the month grid, the
// day list, and the "airs tonight" living line. Fetch-free and QML-free (all
// inputs passed in) so the offscreen harness proves it. A .pragma library can't
// see context properties: the PAGE passes Collection.items / Progress data in.
//
// Day boundaries are LOCAL (a user's "tonight" is their tonight), computed via
// Date part math — the harness anchors fixtures to local-day offsets so this
// stays deterministic across timezones.
.pragma library

var WEEKDAYS = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
var MONTHS = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];
var LIVING_WINDOW_MS = 48 * 3600 * 1000;

// ---- entry helpers ----
function videosOf(entry) {
    return (entry && entry.payload && entry.payload.libCalendar) ? entry.payload.libCalendar : [];
}
function silenced(entry) {
    return !!(entry && entry.payload && entry.payload.libNotif === false);
}
function isSeries(entry) {
    return entry && entry.type === "series";
}
// the series entries a user actually sees on the Calendar: series, saved, not silenced
function activeSeries(entries) {
    var out = [];
    for (var i = 0; i < (entries || []).length; i++) {
        var e = entries[i];
        if (isSeries(e) && !silenced(e) && videosOf(e).length) out.push(e);
    }
    return out;
}

// ---- day math (LOCAL) ----
function _dayStart(ms) { var d = new Date(ms); d.setHours(0, 0, 0, 0); return d.getTime(); }
function _dayDiff(ms, nowMs) { return Math.round((_dayStart(ms) - _dayStart(nowMs)) / 86400000); }
function _sameLocalDay(aMs, bMs) { return _dayStart(aMs) === _dayStart(bMs); }

function dayLabel(releasedMs, nowMs) {
    var diff = _dayDiff(releasedMs, nowMs);
    if (diff === 0) return "Today";
    if (diff === 1) return "Tomorrow";
    if (diff === -1) return "Yesterday";
    if (diff > 1 && diff <= 7) return WEEKDAYS[new Date(releasedMs).getDay()];
    var d = new Date(releasedMs);
    return MONTHS[d.getMonth()] + " " + d.getDate();
}

// ---- chip state ----
// "watched-skip" (already seen — Calendar hides it), "aired" (released, waiting),
// "future" (not yet released). watchedIds is a { episodeId: true } map.
function chipState(video, watchedIds, nowMs) {
    if (video && watchedIds && watchedIds[video.id]) return "watched-skip";
    var ms = Date.parse(video && video.released);
    if (isNaN(ms)) return "future";
    return ms <= nowMs ? "aired" : "future";
}

// every visible (non-watched, date-parseable) video across active series, decorated
function _visibleVideos(entries, watchedIds, nowMs) {
    var out = [];
    var series = activeSeries(entries);
    for (var i = 0; i < series.length; i++) {
        var e = series[i];
        var vids = videosOf(e);
        for (var j = 0; j < vids.length; j++) {
            var v = vids[j];
            var ms = Date.parse(v.released);
            if (isNaN(ms)) continue;
            var st = chipState(v, watchedIds, nowMs);
            if (st === "watched-skip") continue;
            out.push({ seriesId: String(e.id), title: e.title || "", cover: e.cover || "",
                       season: v.season, episode: v.episode, epTitle: v.title || "",
                       released: v.released, releasedMs: ms, state: st });
        }
    }
    return out;
}

// ---- Coming-up rail ----
// future + aired-unwatched, sorted by release ASC, sliced to limit; day-labelled.
function comingUp(entries, nowMs, limit, watchedIds) {
    var rows = _visibleVideos(entries, watchedIds, nowMs);
    rows.sort(function(a, b) { return a.releasedMs - b.releasedMs; });
    var out = [];
    var cap = (limit && limit > 0) ? limit : 20;
    for (var i = 0; i < rows.length && out.length < cap; i++) {
        var r = rows[i];
        out.push({ seriesId: r.seriesId, title: r.title, cover: r.cover, season: r.season,
                   episode: r.episode, epTitle: r.epTitle, released: r.released,
                   dayLabel: dayLabel(r.releasedMs, nowMs), state: r.state });
    }
    return out;
}

// ---- the living line ("2 of yours air tonight") ----
// counts SERIES (not episodes) with a FUTURE airing within 48h; "" when none.
function livingLine(entries, nowMs) {
    var series = activeSeries(entries);
    var qualifying = [];         // {seriesId, nearestMs}
    for (var i = 0; i < series.length; i++) {
        var vids = videosOf(series[i]);
        var nearest = -1;
        for (var j = 0; j < vids.length; j++) {
            var ms = Date.parse(vids[j].released);
            if (isNaN(ms)) continue;
            if (ms > nowMs && (ms - nowMs) <= LIVING_WINDOW_MS && (nearest < 0 || ms < nearest))
                nearest = ms;
        }
        if (nearest > 0) qualifying.push(nearest);
    }
    if (!qualifying.length) return "";
    qualifying.sort(function(a, b) { return a - b; });
    var diff = _dayDiff(qualifying[0], nowMs);
    var when = diff <= 0 ? "tonight" : diff === 1 ? "tomorrow" : WEEKDAYS[new Date(qualifying[0]).getDay()];
    var n = qualifying.length;
    var verb = n === 1 ? "airs" : "air";
    var noun = n === 1 ? "1 of yours" : (n + " of yours");
    return { count: n, label: noun + " " + verb + " " + when };
}

// ---- month grid ----
function monthGrid(entries, year, month1, nowMs, watchedIds) {
    var vids = _visibleVideos(entries, watchedIds, nowMs);
    // bucket by local day-of-month within (year, month1)
    var byDay = {};
    for (var i = 0; i < vids.length; i++) {
        var d = new Date(vids[i].releasedMs);
        if (d.getFullYear() === year && (d.getMonth() + 1) === month1) {
            var day = d.getDate();
            if (!byDay[day]) byDay[day] = [];
            byDay[day].push({ seriesId: vids[i].seriesId, title: vids[i].title,
                              cover: vids[i].cover, state: vids[i].state });
        }
    }
    var first = new Date(year, month1 - 1, 1);
    var startDow = first.getDay();                       // 0=Sun
    var daysInMonth = new Date(year, month1, 0).getDate();
    var weeks = [], week = [];
    // leading blanks
    for (var b = 0; b < startDow; b++) week.push({ day: 0, inMonth: false, isToday: false, chips: [], overflow: 0 });
    for (var day = 1; day <= daysInMonth; day++) {
        var chips = byDay[day] || [];
        var cellMs = new Date(year, month1 - 1, day, 12, 0, 0, 0).getTime();
        week.push({ day: day, inMonth: true, isToday: _sameLocalDay(cellMs, nowMs),
                    chips: chips.slice(0, 3), overflow: Math.max(0, chips.length - 3) });
        if (week.length === 7) { weeks.push(week); week = []; }
    }
    if (week.length) { while (week.length < 7) week.push({ day: 0, inMonth: false, isToday: false, chips: [], overflow: 0 }); weeks.push(week); }
    return { weeks: weeks, year: year, month: month1, label: MONTHS[month1 - 1] + " " + year };
}

// ---- a single day's episodes ----
function dayList(entries, dateMs, watchedIds, nowMs) {
    var now = (nowMs !== undefined && nowMs !== null) ? nowMs : dateMs;
    var vids = _visibleVideos(entries, watchedIds, now);
    var out = [];
    for (var i = 0; i < vids.length; i++)
        if (_sameLocalDay(vids[i].releasedMs, dateMs))
            out.push({ seriesId: vids[i].seriesId, title: vids[i].title, season: vids[i].season,
                       episode: vids[i].episode, epTitle: vids[i].epTitle, state: vids[i].state });
    out.sort(function(a, b) { return a.releasedMs - b.releasedMs; });
    return out;
}

// ---- fetch support (refreshCalendar) ----
var STAMP_TTL_MS = 6 * 3600 * 1000;
var WINDOW_PAST_MS = 45 * 86400 * 1000;
var WINDOW_FUTURE_MS = 120 * 86400 * 1000;

// a series entry's calendar slice is fresh if stamped within the TTL.
function stampFresh(entry, nowMs) {
    var at = entry && entry.payload && entry.payload.libCalStampAt;
    return !!at && (nowMs - at) < STAMP_TTL_MS;
}

// meta.videos → the compact 5-key slices we persist: only dated episodes inside
// the [now-45d, now+120d] window (the calendar's relevant horizon).
function calendarSlice(videos, nowMs) {
    var out = [];
    for (var i = 0; i < (videos || []).length; i++) {
        var v = videos[i];
        var ms = Date.parse(v && v.released);
        if (isNaN(ms)) continue;
        if (ms < nowMs - WINDOW_PAST_MS || ms > nowMs + WINDOW_FUTURE_MS) continue;
        out.push({ id: String(v.id || ""), season: v.season, episode: v.episode,
                   title: v.title || "", released: v.released });
    }
    return out;
}

// Refresh stale series entries' calendar slices, serially (one meta fetch at a
// time, the Next Up cache style). Movies are ignored (calendar is series-only).
// addFn upserts the patched entry (Collection.add). done() after the last.
// loadMeta(type, id, cb) — cb(meta{videos}). Distinct payload keys from stage 2.
function refreshCalendar(entries, loadMeta, addFn, nowMs, done) {
    var stale = [];
    for (var i = 0; i < (entries || []).length; i++) {
        var e = entries[i];
        if (!isSeries(e) || stampFresh(e, nowMs)) continue;
        stale.push(e);
    }
    if (!stale.length) { if (done) done(); return; }
    var idx = 0;
    function step() {
        if (idx >= stale.length) { if (done) done(); return; }
        var e = stale[idx++];
        loadMeta("series", String(e.id), function(meta) {
            var slice = calendarSlice((meta && meta.videos) || [], nowMs);
            var patched = {};
            for (var k in e) patched[k] = e[k];
            var pay = {};
            if (e.payload) for (var pk in e.payload) pay[pk] = e.payload[pk];
            pay.libCalendar = slice;
            pay.libCalStampAt = nowMs;
            patched.payload = pay;
            addFn(patched);
            step();
        });
    }
    step();
}

// ---- month header count ----
function monthCount(entries, year, month1, nowMs, watchedIds) {
    var vids = _visibleVideos(entries, watchedIds, nowMs);
    var n = 0;
    for (var i = 0; i < vids.length; i++) {
        var d = new Date(vids[i].releasedMs);
        if (d.getFullYear() === year && (d.getMonth() + 1) === month1) n++;
    }
    return n;
}
