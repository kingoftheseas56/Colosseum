// Offscreen proof of DiscoverApi's pure derivations. NEVER throw (hangs offscreen);
// collect fails, Qt.exit(fails.length).
import QtQuick
import "../qml/DiscoverApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            var cinemeta = { id: "cinemeta", enabled: true, core: true,
                transportUrl: "https://v3-cinemeta.strem.io/manifest.json",
                manifest: { name: "Cinemeta", catalogs: [
                    { type: "movie", id: "top", name: "Popular",
                      extra: [{ name: "genre", options: ["Action","Drama"] }, { name: "skip" }] },
                    { type: "series", id: "top", name: "Popular",
                      extra: [{ name: "genre", options: ["Action"] }, { name: "skip" }] },
                    { type: "movie", id: "search-only", name: "Search",
                      extra: [{ name: "search", isRequired: true }] }
                ] } };
            var netflix = { id: "nf", enabled: true, core: false,
                transportUrl: "https://nf.example/manifest.json",
                manifest: { name: "Netflix", catalogs: [
                    { type: "movie", id: "nf-pop", name: "Netflix",
                      extra: [{ name: "genre", isRequired: true, options: ["Comedy","Crime"] }] },
                    { type: "series", id: "nf-tv", name: "Netflix", genres: ["Drama"] }
                ] } };
            var disabled = { id: "off", enabled: false, core: false,
                transportUrl: "https://off.example/manifest.json",
                manifest: { name: "Off", catalogs: [{ type: "movie", id: "x", name: "X" }] } };
            var installed = [cinemeta, netflix, disabled];

            // types: union over enabled manifests, movie/series/anime first
            var types = Api.typesFor(installed);
            ok(types.length === 2 && types[0] === "movie" && types[1] === "series",
               "typesFor union+order: " + JSON.stringify(types));

            // catalogs for movie: Cinemeta top + Netflix nf-pop; search-only excluded; disabled excluded
            var cats = Api.catalogsFor(installed, "movie");
            ok(cats.length === 2, "catalogsFor count: " + cats.length);
            ok(cats[0].title === "Popular" && cats[0].addonName === "Cinemeta", "core catalog first");
            ok(cats[1].catalogId === "nf-pop", "addon catalog present");

            // extras: skip/search filtered out; genres[] legacy honored
            var ext0 = Api.extrasFor(cats[0]);
            ok(ext0.length === 1 && ext0[0].name === "genre" && ext0[0].options.length === 2,
               "extrasFor cinemeta: " + JSON.stringify(ext0));
            var catsS = Api.catalogsFor(installed, "series");
            var extNfTv = Api.extrasFor(catsS[1]);
            ok(extNfTv.length === 1 && extNfTv[0].options[0] === "Drama", "legacy genres[] honored");

            // defaults: required extra auto-picks first option; optional stays null
            var defs = Api.defaultSelections(Api.extrasFor(cats[1]));
            ok(defs["genre"] === "Comedy", "required extra auto-picked: " + JSON.stringify(defs));
            var defs0 = Api.defaultSelections(ext0);
            ok(defs0["genre"] === null, "optional extra defaults null");

            // URL building: no extra / with genre / with skip / both
            ok(Api.urlFor(cats[0], {}, 0) ===
               "https://v3-cinemeta.strem.io/catalog/movie/top.json", "bare url");
            ok(Api.urlFor(cats[0], { genre: "Action" }, 0) ===
               "https://v3-cinemeta.strem.io/catalog/movie/top/genre=Action.json", "genre url");
            ok(Api.urlFor(cats[0], { genre: null }, 100) ===
               "https://v3-cinemeta.strem.io/catalog/movie/top/skip=100.json", "skip url");
            ok(Api.urlFor(cats[0], { genre: "Sci-Fi & Fantasy" }, 50) ===
               "https://v3-cinemeta.strem.io/catalog/movie/top/genre=Sci-Fi%20%26%20Fantasy&skip=50.json",
               "encoded genre + skip url");

            // pin resolution: found vs missing addon
            var pinOk = Api.resolvePin(installed,
                { transportUrl: "https://nf.example/manifest.json", type: "movie", catalogId: "nf-pop" });
            ok(pinOk && !pinOk.missing && pinOk.catalog.title === "Netflix", "pin found");
            var pinMiss = Api.resolvePin(installed,
                { transportUrl: "https://gone.example/manifest.json", type: "movie", catalogId: "z",
                  addonName: "Gone" });
            ok(pinMiss.missing === true && pinMiss.addonName === "Gone", "pin missing");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("discover_api_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
