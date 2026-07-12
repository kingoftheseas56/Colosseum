// AbbApi.js — AudioBookBay scraper, ported from Tankoban 2's AbbScraper (C++ → QML JS).
// Discovery is Apple (BiblioApi.searchAudiobooks); THIS is delivery: given a book's
// title/author, find its audiobook torrent on ABB and hand a magnet/infoHash to the
// native Audio engine. Base URL oscillates on takedowns — ONE constant to bump.
// Regexes are verbatim from AbbScraper.cpp (verified against the 2026-04-22 probe fixtures).
.pragma library

var ABB = "https://audiobookbay.lu";
var ABB_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

function requestText(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        done(xhr.responseText);
    };
    xhr.open("GET", url);
    try { xhr.setRequestHeader("User-Agent", ABB_UA); } catch (e) {}
    xhr.send();
}

function decode(s) {
    return String(s || "")
        .replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&apos;/g, "'")
        .replace(/&#039;/g, "'").replace(/&#8211;/g, "-").replace(/&#8212;/g, "-")
        .replace(/&#8230;/g, "...").replace(/&nbsp;/g, " ")
        .replace(/&lt;/g, "<").replace(/&gt;/g, ">").trim();
}

// Split search HTML into per-post blocks. EXACT match on `<div class="post">` — the
// honeypot decoys are `<div class="post re-ab" ...>`, which this excludes cleanly.
function parseSearch(html) {
    var text = String(html || "");
    var starts = [], re = /<div class="post">/g, m;
    while ((m = re.exec(text)) !== null) starts.push(m.index);
    if (starts.length === 0) return [];
    var out = [];
    var titleRe = /<h2><a href="([^"]+)"[^>]*>([^<]+)<\/a><\/h2>/;
    var coverRe = /<img src="([^"]+)"[^>]*width="250"/;
    var langRe  = /Language:\s*([^<]+?)(?:<span|<br)/i;
    var fmtRe   = /Format:\s*<span[^>]*>([^<]+)<\/span>/i;
    var sizeRe  = /File Size:\s*<span[^>]*>([^<]+)<\/span>\s*([GMK]?Bs?)/i;
    var postedRe= /Posted:\s*([^<]+?)<br/i;
    var slugRe  = /\/abss\/([^/]+)\/?/;
    for (var i = 0; i < starts.length; i++) {
        var block = text.substring(starts[i], (i + 1 < starts.length) ? starts[i + 1] : text.length);
        var tm = titleRe.exec(block);
        if (!tm) continue;
        var href = tm[1].trim();
        var detailUrl = (href.indexOf("http") === 0) ? href : ABB + href;
        var sm = slugRe.exec(detailUrl);
        var slug = sm ? sm[1] : "";
        if (!slug) continue;
        var wt = decode(tm[2]);
        var title = wt, author = "";
        var dash = wt.lastIndexOf(" - ");
        if (dash > 0 && dash < wt.length - 3) {
            var after = wt.substring(dash + 3).trim();
            if (after && after.length <= 60 && after.indexOf("/") < 0 && after.indexOf("[") < 0 && !/^[0-9]/.test(after)) {
                author = after; title = wt.substring(0, dash).trim();
            }
        }
        var cm = coverRe.exec(block), lm = langRe.exec(block), fm = fmtRe.exec(block),
            zm = sizeRe.exec(block), pm = postedRe.exec(block);
        out.push({
            slug: slug, detailUrl: detailUrl, title: title, author: author,
            cover: cm ? cm[1].trim() : "",
            language: lm ? decode(lm[1]) : "",
            format: fm ? fm[1].trim() : "",
            size: zm ? (zm[1].trim() + " " + zm[2].trim()) : "",
            posted: pm ? pm[1].trim() : ""
        });
    }
    return out;
}

// Detail page → { infoHash, contents }. infoHash = the 40-hex SHA1 for the magnet.
function parseDetail(html) {
    var text = String(html || "");
    var hm = /<td>Info Hash:<\/td>\s*<td[^>]*>\s*([0-9a-fA-F]{40})\s*<\/td>/i.exec(text);
    var infoHash = hm ? hm[1].toLowerCase() : "";
    return { infoHash: infoHash, contents: parseFileSummary(text) };
}

// "Contents: N × .m4b, M × .mp3" — audio extensions first. Display + audio-file count.
function parseFileSummary(text) {
    var mk = /<tr>\s*<td[^>]*>\s*This is a (Multifile|Singlefile) Torrent\s*<\/td>\s*<\/tr>/i.exec(text);
    if (!mk) return "";
    var tail = text.substring(mk.index + mk[0].length, mk.index + mk[0].length + 32000);
    var fileRe = /<tr>\s*<td[^>]*>\s*([^<]+?)\.([a-zA-Z0-9]+)\s+([0-9.]+\s*[GMK]?Bs?)\s*<\/td>\s*<\/tr>/gi;
    var counts = {}, m, total = 0;
    while ((m = fileRe.exec(tail)) !== null && total < 200) {
        var ext = m[2].toLowerCase(); counts[ext] = (counts[ext] || 0) + 1; total++;
    }
    var order = ["m4b", "mp3", "m4a", "flac", "ogg", "wav"], parts = [], seen = {};
    for (var i = 0; i < order.length; i++) if (counts[order[i]]) { parts.push(counts[order[i]] + " × ." + order[i]); seen[order[i]] = 1; }
    var rest = Object.keys(counts).filter(function(k){ return !seen[k]; }).sort();
    for (var j = 0; j < rest.length; j++) parts.push(counts[rest[j]] + " × ." + rest[j]);
    return parts.length ? "Contents: " + parts.join(", ") : "";
}

// VERBATIM from AbbScraper::constructMagnet — ABB's own 7-tracker list (typo'd port
// kept; libtorrent ignores invalid trackers, matching ABB maximizes peer parity).
function constructMagnet(infoHash, title) {
    var dn = title ? encodeURIComponent(title) : "audiobookbay";
    return "magnet:?xt=urn:btih:" + infoHash + "&dn=" + dn
        + "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.open-internet.nl%3A6969%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A69691337%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.baravik.org%3A6970%2Fannounce"
        + "&tr=http%3A%2F%2Fretracker.telecom.by%3A80%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce";
}

// full delivery flow: search ABB for a book → rows. Caller picks a row, then calls
// fetchInfoHash(slug) to resolve the torrent hash. done({ rows }) or done(null) on miss.
function resolveAudiobook(title, author, done) {
    var term = (title || "") + (author ? " " + author : "");
    if (!term.trim()) { done(null); return; }
    requestText(ABB + "/?s=" + encodeURIComponent(term), function(html) {
        if (!html) { done(null); return; }
        var rows = parseSearch(html);
        if (rows.length === 0) { done(null); return; }
        done({ rows: rows });
    });
}

function fetchInfoHash(slug, done) {
    requestText(ABB + "/abss/" + slug + "/", function(html) {
        if (!html) { done(null); return; }
        done(parseDetail(html));
    });
}
