// Headless logic harness for AddonLogos.logoFor — the bundled-logo match table.
// Run: qml.exe -platform offscreen tests/addon_logos_harness.qml
// Verdict = exit code (0 all pass, 1 any mismatch). Throw would HANG the offscreen
// loop, so every assertion is inside try/catch and the result is Qt.exit()'d.
import QtQuick
import "../qml/AddonLogos.js" as AddonLogos

Item {
    Component.onCompleted: {
        var dir = "../assets/addon-logos/";
        // [id, name, expectedFile ("" = no bundled logo, letter fallback)]
        var cases = [
            // --- curated rails (our store) ---
            ["com.stremio.torrentio.addon", "Torrentio", "torrentio.png"],
            ["com.linvo.cinemeta", "Cinemeta", ""],                 // no manifest logo — letter, like Harbor
            ["community.anime.kitsu", "Anime Kitsu", "anime-kitsu.png"],
            ["org.stremio.opensubtitlesv3", "OpenSubtitles v3", "opensubtitles.png"],
            ["comet.elfhosted.com", "Comet", "comet.png"],
            ["stremio.addons.mediafusion|elfhosted", "MediaFusion", "mediafusion.png"],
            ["com.aiostreams.viren070", "AIOStreams", "aiostreams.png"],
            ["com.keopps.peerflix", "Peerflix", "peerflix.png"],
            ["com.notorrent.addon", "NoTorrent", "notorrent.png"],
            ["webstreamr-mbg", "WebStreamr", "webstreamr.png"],
            ["pw.ers.netflix-catalog", "Netflix Catalog", "netflix-catalog.png"],
            ["default.global.topstreaming.flixpatrol", "FlixPatrol Top 10", "flixpatrol.png"],
            ["org.stremio.aiolists", "AIOLists", "aiolists.png"],
            ["com.joaogonp.marveladdon", "Marvel Universe", "marvel.png"],
            ["com.tapframe.dcaddon", "DC Universe", "dc.png"],
            // --- universes: an installed universe wears the mark of the add-on it grew out of (Task 12) ---
            ["com.colosseum.universe.onepiece", "One Piece", "one-piece.png"],
            ["com.colosseum.universe.dcau", "DC Animated Universe", "dc.png"],
            ["community.morelikethis", "More Like This", "morelikethis.png"],
            ["community.subsource.subtitles", "SubSource", "subsource.png"],
            ["community.subdl.subtitles", "SubDL", "subdl.jpg"],
            ["com.stremio.rtngz", "Ratings", "ratings.jpg"],
            ["org.streailer.trailer", "Streailer", "streailer.png"],
            ["community.meteor", "Meteor", "meteor.png"],
            ["community.usatv", "USA TV", ""],                      // fetch failed — letter, honest
            // --- debrid / torrent (Harbor bundled set, install-by-link / community) ---
            ["rd-library", "Real Debrid", "realdebrid.png"],
            ["tb-library", "TorBox", "torbox.png"],
            ["tpb", "The Pirate Bay", "thepiratebay.png"],
            ["x1337", "1337x", "x1337.jpg"],
            ["org.stremio.local", "Local Files", "local-files.png"],
            // --- name-only match (id gives no hint) ---
            ["some.random.id", "Torrentio Plus", "torrentio.png"],
            // --- true unknown → no logo ---
            ["totally.unknown.addon", "Zzz Nonesuch", ""]
        ];

        var pass = 0, fail = 0;
        for (var i = 0; i < cases.length; i++) {
            var id = cases[i][0], name = cases[i][1], exp = cases[i][2];
            var expPath = exp === "" ? "" : dir + exp;
            var got = "";
            try { got = AddonLogos.logoFor(id, name); }
            catch (e) { console.log("THREW  " + id + " :: " + e); fail++; continue; }
            if (got === expPath) {
                pass++;
            } else {
                fail++;
                console.log("FAIL   " + name + " [" + id + "]");
                console.log("         expected: '" + expPath + "'");
                console.log("         got:      '" + got + "'");
            }
        }
        console.log("=== AddonLogos match table: " + pass + " pass, " + fail + " fail ===");
        Qt.exit(fail === 0 ? 0 : 1);
    }
}
