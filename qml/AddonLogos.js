// AddonLogos.js — the bundled official-logo match table, ported from Harbor's
// src/components/addon-logo.tsx BUNDLED array and extended with the curated
// add-ons Harbor doesn't ship (logos pulled from each add-on's own manifest by
// scripts/fetch_addon_logos.py). logoFor(id, name) returns a local asset path
// under assets/addon-logos/, or "" when we ship no logo — in which case the
// AddonLogo component draws the honest letter square instead.
//
// Order matters: first match wins, so put specific matchers before generic ones.
// Paths are relative to AddonLogo.qml (in qml/), which is where they're drawn.
.pragma library

var DIR = "../assets/addon-logos/";

// Each entry: { file, m(idLower, name) }. `id` arrives already lower-cased.
var TABLE = [
    // ---- play sources / debrid (Harbor's bundled set) ----
    { file: "torrentio.png",   m: function (id, n) { return id.indexOf("torrentio") >= 0 || /torrentio/i.test(n); } },
    { file: "torbox.png",      m: function (id, n) { return id === "tb-library" || id.indexOf("tb-") === 0 || /torbox/i.test(id) || /\btorbox\b/i.test(n); } },
    { file: "realdebrid.png",  m: function (id, n) { return id === "rd-library" || id.indexOf("rd-") === 0 || /real.?debrid/i.test(id) || /real.?debrid/i.test(n); } },
    { file: "alldebrid.webp",  m: function (id, n) { return id === "ad-library" || id.indexOf("ad-") === 0 || /alldebrid/i.test(id) || /all.?debrid/i.test(n); } },
    { file: "premiumize.png",  m: function (id, n) { return id === "pm-library" || id.indexOf("pm-") === 0 || /premiumize/i.test(id) || /premiumize/i.test(n); } },
    { file: "debridlink.png",  m: function (id, n) { return id === "dl-library" || id.indexOf("dl-") === 0 || /debrid.?link/i.test(id) || /debrid.?link/i.test(n); } },
    { file: "comet.png",       m: function (id, n) { return id.indexOf("comet") >= 0 || /^comet\b/i.test(n); } },
    { file: "mediafusion.png", m: function (id, n) { return id.indexOf("mediafusion") >= 0 || /mediafusion/i.test(n); } },
    { file: "aiostreams.png",  m: function (id, n) { return id.indexOf("aiostreams") >= 0 || /aio.?streams/i.test(n); } },
    { file: "peerflix.png",    m: function (id, n) { return id.indexOf("peerflix") >= 0 || /peerflix/i.test(n); } },
    { file: "notorrent.png",   m: function (id, n) { return id.indexOf("notorrent") >= 0 || /no.?torrent/i.test(n); } },
    { file: "webstreamr.png",  m: function (id, n) { return id.indexOf("webstreamr") >= 0 || /web.?streamr/i.test(n); } },

    // ---- torrent indexers (Harbor's bundled set, for community / install-by-link) ----
    { file: "thepiratebay.png", m: function (id, n) { return id === "tpb" || id.indexOf("piratebay") >= 0 || /pirate.?bay/i.test(n); } },
    { file: "x1337.jpg",       m: function (id, n) { return id === "x1337" || /1337/.test(id) || /1337x/i.test(n); } },
    { file: "yts.png",         m: function (id, n) { return id === "yts" || /^yts/i.test(n); } },
    { file: "eztv.png",        m: function (id, n) { return id === "eztv" || /^eztv/i.test(n); } },
    { file: "bitsearch.png",   m: function (id, n) { return id === "bitsearch" || /bitsearch/i.test(n); } },
    { file: "rutor.ico",       m: function (id, n) { return id === "rutor" || /rutor/i.test(n); } },
    { file: "nyaa.png",        m: function (id, n) { return id === "nyaa" || /nyaa/i.test(n); } },
    { file: "knaben.ico",      m: function (id, n) { return id === "knaben" || /knaben/i.test(n); } },
    { file: "easynews.png",    m: function (id, n) { return id.indexOf("easynews") >= 0 || /easy.?news/i.test(n); } },

    // ---- catalogs (curated; logos from each add-on's manifest) ----
    { file: "netflix-catalog.png", m: function (id, n) { return id.indexOf("netflix") >= 0 || /netflix/i.test(n); } },
    { file: "flixpatrol.png",  m: function (id, n) { return id.indexOf("flixpatrol") >= 0 || /flixpatrol/i.test(n); } },
    { file: "aiolists.png",    m: function (id, n) { return id.indexOf("aiolists") >= 0 || /aio.?lists/i.test(n); } },
    { file: "marvel.png",      m: function (id, n) { return id.indexOf("marvel") >= 0 || /marvel/i.test(n); } },
    { file: "dc.png",          m: function (id, n) { return id.indexOf("dcaddon") >= 0 || /\bdc universe\b/i.test(n); } },
    { file: "morelikethis.png", m: function (id, n) { return id.indexOf("morelikethis") >= 0 || /more.?like.?this/i.test(n); } },
    { file: "streaming-catalogs.png", m: function (id, n) { return id.indexOf("streaming-catalogs") >= 0 || /streaming.catalog/i.test(n); } },

    // ---- subtitles / details / extras ----
    { file: "opensubtitles.png", m: function (id, n) { return id.indexOf("opensubtitles") >= 0 || /opensubtitles/i.test(n); } },
    { file: "subsource.png",   m: function (id, n) { return id.indexOf("subsource") >= 0 || /subsource/i.test(n); } },
    { file: "subdl.jpg",       m: function (id, n) { return id.indexOf("subdl") >= 0 || /\bsubdl\b/i.test(n); } },
    { file: "ratings.jpg",     m: function (id, n) { return id.indexOf("rtngz") >= 0 || n === "Ratings"; } },
    { file: "streailer.png",   m: function (id, n) { return id.indexOf("streailer") >= 0 || /streailer/i.test(n); } },
    { file: "meteor.png",      m: function (id, n) { return id.indexOf("meteor") >= 0 || /\bmeteor\b/i.test(n); } },
    { file: "anime-kitsu.png", m: function (id, n) { return id.indexOf("anime-kitsu") >= 0 || /anime.?kitsu/i.test(n); } },

    // ---- house wells + house catalogues (site marks, pulled by scripts/fetch_site_marks.py) ----
    // Matched on the SITE name, deliberately, so both of a site's roles share one mark:
    // WeebCentral appears twice in Tankoban (locked catalogue + removable chapter-pages
    // well) and GetComics twice in comics — same logo, different job. (2026-07-25)
    { file: "weebcentral.png",  m: function (id, n) { return id.indexOf("weebcentral") >= 0 || /weeb.?central/i.test(n); } },
    { file: "getcomics.png",    m: function (id, n) { return id.indexOf("getcomics") >= 0 || /get.?comics/i.test(n); } },
    { file: "libgen.ico",       m: function (id, n) { return id.indexOf("libgen") >= 0 || /lib.?gen/i.test(n); } },
    { file: "audiobookbay.png", m: function (id, n) { return id.indexOf("audiobookbay") >= 0 || /audio.?book.?bay/i.test(n); } },
    { file: "applebooks.ico",   m: function (id, n) { return id.indexOf("applebooks") >= 0 || /apple ?books/i.test(n); } },
    // NOT listed: "Torrent Indexers". It is OUR composite of four sites, not a site, so
    // it has no official iconography to borrow — it keeps the honest letter square.
    // ExtTorrents and Torrents-CSV publish no square mark either (ExtTorrents' only
    // logo is a 73x29 wordmark); they keep letters inside the Settings sheet.

    // ---- local / built-in ----
    { file: "local-files.png", m: function (id, n) { return id === "org.stremio.local" || /local.?files/i.test(id) || /^local files\b/i.test(n); } }
];

// Return the bundled logo asset path for an add-on, or "" if we ship none.
function logoFor(id, name) {
    var i = (id || "").toLowerCase();
    var nm = name || "";
    for (var k = 0; k < TABLE.length; k++) {
        try { if (TABLE[k].m(i, nm)) return DIR + TABLE[k].file; }
        catch (e) { /* a bad matcher never breaks the row */ }
    }
    return "";
}
