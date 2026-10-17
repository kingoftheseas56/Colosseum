.pragma library
.import "UniverseContentApi.js" as ContentApi

var groupingSource = "https://one-piece.com/story/index.html";

var snapshot = {
    date: "2026-10-04",
    latestObservedMangaChapter: 1194,
    latestObservedMangaDate: "2026-09-27",
    latestObservedAnimeEpisode: 1180,
    latestObservedAnimeDate: "2026-09-27",
    note: "Elbaf is ongoing. Open-ended official ranges are authoritative; observed-through values are a dated browsing snapshot."
};

var providerSelectors = {
    animeRootId: "kitsu:12",
    onePaceExtensionId: "com.onepace.fedew",
    onePaceMetaId: "pp_onepace",
    mangaMalId: 13,
    mangaEntryId: "30013",
    colorMangaEntryId: "one-piece-color",
    liveActionRootId: "tt11737520"
};

function badgeSource(id) {
    return "../assets/universes/one-piece/final-sea/badges/" + id + ".png";
}

function bannerSource(id) {
    return "../assets/universes/one-piece/final-sea/banners/" + id + ".png";
}
function relatedMediaForArc(id) {
    return [];
}

function mappingStatus(id) {
    if (id === "egghead") {
        return {
            anime: "supported-closed-kitsu-range",
            onePace: "supported-current-one-pace-metadata",
            manga: "supported-existing-tankoban-catalog-mal-13",
            coloredManga: "supported-existing-universe-entry",
            liveAction: "absent-current-cinemeta-metadata",
            relatedMedia: "none-curated",
            artwork: "supported-local-wiki-assets"
        };
    }
    return {
        anime: "supported-snapshot-bounded-kitsu-range",
        onePace: "absent-current-one-pace-metadata",
        manga: "supported-published-volume-snapshot",
        coloredManga: "supported-published-volume-snapshot",
        liveAction: "absent-current-cinemeta-metadata",
        relatedMedia: "none-curated",
        artwork: "supported-local-wiki-assets"
    };
}

function record(id, order, title, locationId, locationIds, place, summary,
                anime, chapters, volumes, status, officialChapters, officialAnime,
                providerAnimeSpec, providerVolumeSpec, note) {
    return {
        id: id,
        order: order,
        title: title,
        locationId: locationId,
        locationIds: locationIds,
        place: place,
        geoLabel: place,
        sagaLabel: "Final Saga",
        seaId: "final-sea",
        seaLabel: "Final Sea",
        canonicalSea: "New World",
        summary: summary,
        anime: anime,
        chapters: chapters,
        volumes: volumes,
        status: status,
        officialGrouping: {
            chapters: officialChapters,
            anime: officialAnime,
            source: groupingSource
        },
        mappingNote: note,
        source: id === "egghead"
                ? "https://one-piece.com/story/egghead/index.html"
                : groupingSource,
        providerAnimeSpec: providerAnimeSpec,
        providerVolumeSpec: providerVolumeSpec,
        onePacePrefixes: id === "egghead" ? ["EH_"] : [],
        liveActionSeason: 0,
        liveActionEpisodes: "",
        heroSource: bannerSource(id),
        markerSource: badgeSource(id),
        relatedMedia: relatedMediaForArc(id),
        providerSelectors: providerSelectors,
        mediaMappingStatus: mappingStatus(id)
    };
}

var arcs = [
    record(
        "egghead", 32, "Egghead", "egghead",
        ["egghead", "fabiriophase", "labophase", "frontier-dome"],
        "Egghead · Future Island",
        "After leaving Wano, the Straw Hats reach Vegapunk's island laboratory. The voyage crosses the factory level and cloud-borne laboratory as the Egghead Incident engulfs the island.",
        "1089-1155", "1058-1126", "105-111", "complete",
        "1058-1126", "1089-1155",
        "1089-1155", "105-111",
        "Official ONE PIECE.com grouping. Internal island layers are geography, not separate story arcs."
    ),
    record(
        "elbaf", 33, "Elbaf", "elbaf",
        ["elbaf", "land-of-gods", "elbaf-underworld", "sun-world", "heaven-world",
         "owl-library", "walrus-school", "aurust-castle", "warriors-spring"],
        "Elbaf · Warland Kingdom",
        "The crew reaches the land of the Giants, where the immense Adam Tree divides the country into stacked worlds. The current voyage spans the Underworld and the populated Sun World while the story continues.",
        "1156+", "1127+", "111+", "ongoing",
        "1127-", "1156-",
        "1156-1180", "111-115",
        "ONGOING as of 2026-10-04. Do not convert the dated observed-through snapshot into an arc endpoint."
    )
];

arcs[1].snapshotThroughChapter = snapshot.latestObservedMangaChapter;
arcs[1].snapshotThroughAnime = snapshot.latestObservedAnimeEpisode;

function arc(id) {
    if (typeof id !== "string" || !id.length) return null;
    for (var i = 0; i < arcs.length; ++i)
        if (arcs[i].id === id) return arcs[i];
    return null;
}

function arcsForLocation(id) {
    var result = [];
    if (typeof id !== "string" || !id.length) return result;
    for (var i = 0; i < arcs.length; ++i)
        if (arcs[i].locationId === id || arcs[i].locationIds.indexOf(id) >= 0)
            result.push(arcs[i]);
    return result;
}

// Bundled values above are the offline starter snapshot.
function syncContent() {
    if (typeof ContentApi === "undefined") return;
    var data = ContentApi.document("tribute-one-piece-final-sea").story;
    if (!data || !Object.keys(data).length) return;
    data = ContentApi.resolveAssets(data);
    if (data.groupingSource !== undefined && (Array.isArray(groupingSource) ? Array.isArray(data.groupingSource) : typeof data.groupingSource === typeof groupingSource)) groupingSource = data.groupingSource;
    if (data.snapshot !== undefined && (Array.isArray(snapshot) ? Array.isArray(data.snapshot) : typeof data.snapshot === typeof snapshot)) snapshot = data.snapshot;
    if (data.providerSelectors !== undefined && (Array.isArray(providerSelectors) ? Array.isArray(data.providerSelectors) : typeof data.providerSelectors === typeof providerSelectors)) providerSelectors = data.providerSelectors;
    if (data.arcs !== undefined && (Array.isArray(arcs) ? Array.isArray(data.arcs) : typeof data.arcs === typeof arcs)) arcs = data.arcs;
}
if (typeof ContentApi !== "undefined") ContentApi.register(syncContent);
