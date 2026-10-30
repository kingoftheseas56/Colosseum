// One prepared landing request primes exact child pages. No Jikan fan-out on visits.
.pragma library
.import "CatalogDelivery.js" as Delivery
.import "MangaBrowseRules.js" as Rules
.import "ExplicitContentPolicy.js" as Policy

function load(catalog, showExplicit, done, retry) {
    var stopped = false, cancels = [], definitions = Rules.defaultRows(), pages = {}, errors = [];
    function cancel() {
        stopped = true;
        for (var i = 0; i < cancels.length; ++i) if (cancels[i]) cancels[i]();
        cancels = [];
    }
    function finish() {
        if (!stopped) done({rows:Rules.present(definitions, pages, showExplicit, Policy.visible),
            error:errors.length ? errors[0] : "", partial:errors.length > 0});
    }
    function readPages() {
        var remaining = definitions.length;
        definitions.forEach(function(def) {
            if (stopped) return;
            if (retry && typeof catalog.retryDiscoverPageFiltered === "function")
                catalog.retryDiscoverPageFiltered(def.catalogId, def.filters, showExplicit, 0, Rules.PREVIEW_ROW_CAP);
            cancels.push(Delivery.waitQuery(catalog,
                function() { return catalog.discoverPageFiltered(def.catalogId, def.filters, showExplicit, 0, Rules.PREVIEW_ROW_CAP); },
                function() { return catalog.discoverPageFilteredState(def.catalogId, def.filters, showExplicit, 0, Rules.PREVIEW_ROW_CAP); },
                function(outcome) {
                    if (stopped) return;
                    if (outcome.hasData) pages[def.key] = outcome.value;
                    if (!outcome.ok) errors.push(outcome.error || outcome.reason || "Catalogue unavailable");
                    --remaining;
                    if (remaining === 0) finish();
                }));
        });
    }
    if (!catalog || typeof catalog.discoverPageFiltered !== "function"
            || typeof catalog.discoverPageFilteredState !== "function") {
        errors.push("Manga catalogue unavailable"); finish(); return cancel;
    }
    var local = typeof catalog.ready === "function" && catalog.ready();
    if (local) { readPages(); return cancel; }
    if ((!catalog.remoteEnabled && !(typeof catalog.available === "function" && catalog.available())) || typeof catalog.landing !== "function" || typeof catalog.landingState !== "function") {
        errors.push("Manga catalogue unavailable"); finish(); return cancel;
    }
    if (retry && typeof catalog.retryLanding === "function") catalog.retryLanding("manga");
    cancels.push(Delivery.waitQuery(catalog,
        function() { return catalog.landing("manga"); },
        function() { return catalog.landingState("manga"); },
        function(outcome) {
            if (stopped) return;
            if (!outcome.hasData) {
                // Older publications have no manga bundle. Leave existing top/mosaic intact;
                // never probe eight unsupported routes or silently relabel popular results.
                errors.push(outcome.error || outcome.reason || "Manga shelves not published yet");
                finish(); return;
            }
            if (!outcome.ok) errors.push(outcome.error || "Showing cached manga shelves");
            readPages();
        }));
    return cancel;
}
