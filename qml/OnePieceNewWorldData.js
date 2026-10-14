.pragma library
.import "UniverseContentApi.js" as ContentApi

// Match the community arc-index convention used by the Paradise prototype.
// Official story-page groupings differ at transitions; retain both, not a silent reconciliation.
var groupingSource = "https://one-piece.com/story/index.html";

var providerSelectors = {
    animeRootId: "kitsu:12",
    onePaceExtensionId: "com.onepace.fedew",
    onePaceMetaId: "pp_onepace",
    mangaMalId: 13,
    mangaEntryId: "30013",
    colorMangaEntryId: "one-piece-color",
    liveActionRootId: "tt11737520"
};

var onePacePrefixByArc = {
    "fish-man-island": "FI_",
    "punk-hazard": "PH_",
    "dressrosa": "DR_",
    "zou": "ZO_",
    "whole-cake-island": "WC_",
    "wano-country": "WA_"
};

function badgeSource(id) {
    return "../assets/universes/one-piece/new-world/badges/" + id + ".png";
}
function bannerSource(id) {
    return "../assets/universes/one-piece/new-world/banners/" + id + ".png";
}
function onePacePrefixesForArc(id) {
    var prefix = onePacePrefixByArc[id];
    return prefix ? [prefix] : [];
}
function relatedMediaForArc(id) {
    if (id !== "fish-man-island") return [];
    return [{
        id: "tt33992229",
        type: "series",
        title: "One Piece Log: Fish-Man Island Saga",
        year: "2024",
        episode: "",
        thumbnail: "https://m.media-amazon.com/images/M/MV5BYzRiYTcwM2YtMTE1OC00MGFkLTlmZjctMjI0YjQ5MWY1MTAwXkEyXkFqcGc@._V1_SX250.jpg"
    }];
}
function mappingStatus(id) {
    return {
        anime: "supported-current-kitsu-metadata",
        onePace: "supported-current-one-pace-metadata",
        manga: "supported-existing-tankoban-catalog-mal-13",
        coloredManga: "supported-existing-universe-entry",
        liveAction: "absent-current-cinemeta-metadata",
        relatedMedia: id === "fish-man-island" ? "curated" : "none-curated"
    };
}
function record(id, order, title, locationId, locationIds, place, saga, summary,
                anime, chapters, volumes, sourcePage, officialChapters, officialAnime, note) {
    return {
        id: id, order: order, title: title, locationId: locationId, locationIds: locationIds,
        place: place, geoLabel: place, sagaLabel: saga,
        seaLabel: id === "fish-man-island" ? "Red Line passage" : "New World",
        summary: summary, anime: anime, chapters: chapters, volumes: volumes,
        source: "https://onepiece.fandom.com/wiki/" + sourcePage,
        rangeConvention: "community-arc-index",
        officialGrouping: { chapters: officialChapters, anime: officialAnime, source: groupingSource },
        mappingNote: note,
        seaId: "new-world",
        onePacePrefixes: onePacePrefixesForArc(id), liveActionSeason: 0, liveActionEpisodes: "",
        heroSource: bannerSource(id), markerSource: badgeSource(id),
        relatedMedia: relatedMediaForArc(id),
        providerSelectors: providerSelectors,
        mediaMappingStatus: mappingStatus(id)
    };
}

var arcs = [
    record("fish-man-island", 25, "Fish-Man Island", "fish-man-island", ["fish-man-island"],
        "Ryugu Kingdom · Beneath the Red Line", "Fish-Man Island",
        "The reunited crew descends to the Ryugu Kingdom. Luffy meets Shirahoshi and confronts Hody Jones before the voyage rises into the New World.",
        "523-541,543-574", "603-653", "61-66", "Fish-Man_Island_Arc", "598-653", "517-574",
        "Original broadcast numbering, not the later condensed remaster. The official story page also includes Return to Sabaody, already represented on Paradise."),
    record("punk-hazard", 26, "Punk Hazard", "punk-hazard", ["punk-hazard"],
        "Punk Hazard · Burning and frozen lands", "Dressrosa",
        "A distress call draws the Straw Hats to an island divided between fire and ice. Caesar's laboratory and an alliance with Law set a new course.",
        "579-589,591-625", "654-699", "66-70", "Punk_Hazard_Arc", "654-700", "579-628",
        "The community index excludes the crossover and Caesar Retrieval episodes. The official grouping includes chapter 700 and runs through episode 628."),
    record("dressrosa", 27, "Dressrosa", "dressrosa", ["dressrosa", "green-bit"],
        "Dressrosa · Green Bit", "Dressrosa",
        "In Doflamingo's kingdom, the alliance uncovers the truth behind the toys and the SMILE trade. Luffy's fight draws the country into open rebellion.",
        "629-746", "700-801", "70-80", "Dressrosa_Arc", "701-801", "629-746",
        "Chapter 700 belongs here in the community index but to Punk Hazard on the official story page. Green Bit is supporting geography, not a claimed Luffy landing. Luffy later reaches Zou on the Going Luffy-senpai, not aboard the advance-party Sunny."),
    record("zou", 28, "Zou", "zou", ["zou"],
        "Mokomo Dukedom · On Zunesha", "Whole Cake Island",
        "On the back of the wandering elephant Zunesha, the crew meets the Mink Tribe, learns the importance of the Road Poneglyphs, and divides its forces.",
        "751-779", "802-824", "80-82", "Zou_Arc", "802-822", "751-779",
        "The official story index ends Zou at chapter 822; the community arc index includes 823-824. The plotted point is the encounter during this voyage, never a permanent island coordinate."),
    record("whole-cake-island", 29, "Whole Cake Island", "whole-cake-island", ["whole-cake-island", "cacao", "totto-land"],
        "Whole Cake Island · Totto Land", "Whole Cake Island",
        "Luffy's group enters Big Mom's archipelago to bring Sanji home. Cacao Island frames their arrival and escape, with the mirror world part of Luffy's return route.",
        "783-877", "825-902", "82-90", "Whole_Cake_Island_Arc", "823-902", "783-877",
        "The official grouping starts at chapter 823; the community index starts at 825. Chateau art depicts the arrival-era landmark. The mirror connector is narrative transit, not a sea passage or a claim that the Sunny entered the mirror world."),
    record("wano-country", 31, "Wano Country", "wano", ["wano", "onigashima", "wano-waterfall", "kuri-beach", "tokage-port"],
        "Wano Country · Onigashima", "Wano Country",
        "The separate crews converge in the closed country. From Kuri to the raid on Onigashima, the alliance fights Kaido and Orochi before the Straw Hats depart Wano.",
        "890-894,897-906,908-1028,1031-1085", "909-1057", "90-105", "Wano_Country_Arc", "909-1057", "892-1088",
        "This chart stops at the Wano conclusion, chapter 1057 and the community arc's episode 1085. The official television grouping runs 892-1088. Onigashima is shown at its pre-raid inland-sea position; its later movement toward the capital is not a second island. Arc 30 is the contextual Levely interlude, not a missing voyage port.")
];

// Read-only marginal context. No coordinate, geographic seal, OPEN ARC or route edge.
var worldContext = {
    id: "levely", order: 30, title: "Levely / Reverie", place: "Mary Geoise",
    visitedByLuffy: false, interactive: false,
    anime: "878-889", chapters: "903-908", volumes: "90",
    source: "https://onepiece.fandom.com/wiki/Levely_Arc",
    officialGrouping: { anime: "878-891", chapters: "903-908", source: groupingSource },
    summary: "While Luffy sails toward Wano, the rulers gather at Mary Geoise. This is world context, not a Straw Hat voyage stop.",
    seaId: "new-world",
    seaLabel: "Red Line / World Government",
    heroSource: bannerSource("levely"),
    markerSource: "",
    onePacePrefixes: ["REV_"],
    liveActionSeason: 0,
    liveActionEpisodes: "",
    relatedMedia: [],
    providerSelectors: providerSelectors,
    mediaMappingStatus: {
        anime: "context-only-not-routable",
        onePace: "verified-prefix-context-only-not-routable",
        manga: "context-only-not-routable",
        coloredManga: "context-only-not-routable",
        liveAction: "absent-current-cinemeta-metadata",
        relatedMedia: "none-curated"
    }
};

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
        if (arcs[i].locationId === id || arcs[i].locationIds.indexOf(id) >= 0) result.push(arcs[i]);
    return result;
}

// Bundled values above are the offline starter snapshot.
function syncContent() {
    var data = ContentApi.document("tribute-one-piece-new-world").story;
    if (!data || !Object.keys(data).length) return;
    data = ContentApi.resolveAssets(data);
    if (data.groupingSource !== undefined && (Array.isArray(groupingSource) ? Array.isArray(data.groupingSource) : typeof data.groupingSource === typeof groupingSource)) groupingSource = data.groupingSource;
    if (data.providerSelectors !== undefined && (Array.isArray(providerSelectors) ? Array.isArray(data.providerSelectors) : typeof data.providerSelectors === typeof providerSelectors)) providerSelectors = data.providerSelectors;
    if (data.onePacePrefixByArc !== undefined && (Array.isArray(onePacePrefixByArc) ? Array.isArray(data.onePacePrefixByArc) : typeof data.onePacePrefixByArc === typeof onePacePrefixByArc)) onePacePrefixByArc = data.onePacePrefixByArc;
    if (data.arcs !== undefined && (Array.isArray(arcs) ? Array.isArray(data.arcs) : typeof data.arcs === typeof arcs)) arcs = data.arcs;
    if (data.worldContext !== undefined && (Array.isArray(worldContext) ? Array.isArray(data.worldContext) : typeof data.worldContext === typeof worldContext)) worldContext = data.worldContext;
}
ContentApi.register(syncContent);
