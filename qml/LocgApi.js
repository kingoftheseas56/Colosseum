// LocgApi.js — League of Comic Geeks (leagueofcomicgeeks.com/comic/get_comics): the
// AniList-model metadata catalogue brain for the comics lane. This file is PARSERS ONLY
// (Task 2) — fetch verbs (spaced queue, caches, validation) land in Task 3.
// Contract captured live 2026-07-09 — tests/fixtures/locg/FINDINGS.md is the markup
// contract; the 4 fixtures in tests/fixtures/locg/ are the real JSON this parses.
//
// TWO distinct <li> item shapes (parser must branch on shape, never assume one):
//   1. series/search shape (search.json, popular.json): plain <li>, no attributes.
//      Series id ONLY in the href (/comics/series/<id>/<slug>), publisher is the FIRST
//      bare <span class=""> in the copy-really-small div, second such span holds the
//      year (" &nbsp;·&nbsp; <year>"). No genre anywhere.
//   2. releases/issue shape (releases.json, and series.json's issue list): <li
//      class="issue" data-comic="<id>" data-pulls="<n>" data-community="<n>" ...> — id/
//      pulls/rating are plain attributes (no text scraping). Publisher is a literal
//      <div class="publisher color-offset">. Cover data-src carries a cache-busting
//      query string. Date rides <span class="date" data-date="<unix>">Human</span>.
.pragma library

function decodeEntities(s) {
    return String(s).replace(/&amp;/g, "&").replace(/&#0?39;/g, "'").replace(/&quot;/g, '"')
                    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&nbsp;/g, " ")
                    .replace(/&#183;/g, "·");
}

// Series/search shape: search.json + popular.json (list=search&list_option=series).
// Returns [{ id:"locg:<id>", title, cover, publisher, startYear }]. Never throws —
// non-matching input (garbage HTML, wrong shape) yields [].
function parseSeriesList(fragment) {
    var out = [];
    if (!fragment || typeof fragment !== "string") return out;
    try {
        var liRe = /<li>([\s\S]*?)<\/li>/g;
        var m;
        while ((m = liRe.exec(fragment)) !== null) {
            var block = m[1];
            var idm = block.match(/\/comics\/series\/(\d+)\//) || block.match(/data-id="(\d+)"/);
            if (!idm) continue;
            var id = idm[1];

            var titleM = block.match(/<div class="title color-primary">[\s\S]*?<a[^>]*>([\s\S]*?)<\/a>/);
            var title = titleM ? decodeEntities(titleM[1].trim()) : "";

            var coverM = block.match(/data-src="([^"]+)"/);
            var cover = coverM ? coverM[1] : "";

            var publisher = "";
            var startYear = 0;
            var copyM = block.match(/<div class="copy-really-small[^"]*">([\s\S]*?)<\/div>/);
            if (copyM) {
                var spans = [];
                var spanRe = /<span class="">([\s\S]*?)<\/span>/g;
                var sm;
                while ((sm = spanRe.exec(copyM[1])) !== null) spans.push(decodeEntities(sm[1].trim()));
                if (spans.length > 0) publisher = spans[0];
                if (spans.length > 1) {
                    var yearM = spans[1].match(/(\d{4})/);
                    if (yearM) startYear = parseInt(yearM[1], 10);
                }
            }

            out.push({
                id: "locg:" + id,
                title: title,
                cover: cover,
                publisher: publisher,
                startYear: startYear
            });
        }
    } catch (e) { return []; }
    return out;
}

// Releases/issue shape: releases.json (and reused for series.json's per-issue list).
// Returns [{ id:"locg:<id>", title, cover, publisher, date, pulls (number), rating (number) }].
// Never throws — non-matching input yields [].
function parseReleases(fragment) {
    var out = [];
    if (!fragment || typeof fragment !== "string") return out;
    try {
        var liRe = /<li class="issue[^"]*"([^>]*)>([\s\S]*?)<\/li>/g;
        var m;
        while ((m = liRe.exec(fragment)) !== null) {
            var attrs = m[1];
            var block = m[2];

            var idm = attrs.match(/data-comic="(\d+)"/);
            if (!idm) continue;
            var id = idm[1];

            var pullsM = attrs.match(/data-pulls="(\d+)"/);
            var pulls = pullsM ? parseInt(pullsM[1], 10) : 0;

            var ratingM = attrs.match(/data-community="(\d+)"/);
            var rating = ratingM ? parseInt(ratingM[1], 10) : 0;

            var titleM = block.match(/<div class="title color-primary"[^>]*>[\s\S]*?<a[^>]*>([\s\S]*?)<\/a>/);
            var title = titleM ? decodeEntities(titleM[1].trim()) : "";

            var pubM = block.match(/<div class="publisher color-offset">([\s\S]*?)<\/div>/);
            var publisher = pubM ? decodeEntities(pubM[1].trim()) : "";

            var coverM = block.match(/data-src="([^"]+)"/);
            var cover = coverM ? coverM[1] : "";

            var dateM = block.match(/<span class="date" data-date="(\d+)">([\s\S]*?)<\/span>/);
            var date = dateM ? decodeEntities(dateM[2].trim()) : "";
            var dateUnix = dateM ? parseInt(dateM[1], 10) : 0;

            out.push({
                id: "locg:" + id,
                title: title,
                cover: cover,
                publisher: publisher,
                date: date,
                dateUnix: dateUnix,
                pulls: pulls,
                rating: rating
            });
        }
    } catch (e) { return []; }
    return out;
}

// Series detail: series.json returns a STRUCTURED object — {"series":{"title",
// "publisher_name",...}, list:"<issues HTML>"} — richer than a bare fragment. Also
// tolerates being handed a bare fragment/garbage string (never throws).
// Returns { issues, issueCount, startYear, publisher, title }.
function parseSeriesDetail(json_or_fragment) {
    var empty = { issues: [], issueCount: 0, startYear: 0, publisher: "", title: "" };
    if (!json_or_fragment) return empty;
    try {
        var obj = null;
        if (typeof json_or_fragment === "string") {
            try { obj = JSON.parse(json_or_fragment); } catch (e) { obj = null; }
        } else if (typeof json_or_fragment === "object") {
            obj = json_or_fragment;
        }

        var listFragment = "";
        var seriesObj = null;
        if (obj && typeof obj === "object") {
            if (typeof obj.list === "string") listFragment = obj.list;
            if (obj.series && typeof obj.series === "object") seriesObj = obj.series;
        } else if (typeof json_or_fragment === "string") {
            // not JSON — treat the raw string as the fragment itself (best-effort)
            listFragment = json_or_fragment;
        }

        var issues = parseReleases(listFragment);

        var startYear = 0;
        for (var i = 0; i < issues.length; i++) {
            if (issues[i].dateUnix > 0) {
                var y = new Date(issues[i].dateUnix * 1000).getFullYear();
                if (startYear === 0 || y < startYear) startYear = y;
            }
        }

        var title = "";
        var publisher = "";
        if (seriesObj) {
            if (typeof seriesObj.title === "string") title = decodeEntities(seriesObj.title.trim());
            if (typeof seriesObj.publisher_name === "string") publisher = decodeEntities(seriesObj.publisher_name.trim());
            if (!startYear && typeof seriesObj.start_year !== "undefined") {
                var sy = parseInt(seriesObj.start_year, 10);
                if (!isNaN(sy)) startYear = sy;
            }
        }
        if (!title && issues.length > 0) title = issues[0].title;
        if (!publisher && issues.length > 0) publisher = issues[0].publisher;

        return {
            issues: issues,
            issueCount: issues.length,
            startYear: startYear,
            publisher: publisher,
            title: title
        };
    } catch (e) {
        return empty;
    }
}
