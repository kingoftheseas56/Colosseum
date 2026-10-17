.pragma library
.import "UniverseContentApi.js" as ContentApi

var SEA_ID = "paradise";
var SEA_LABEL = "Paradise";
var BADGE_ROOT = "../assets/universes/one-piece/paradise/badges/";
var BANNER_ROOT = "../assets/universes/one-piece/paradise/banners/";

var onePacePrefixByArc = {
    "reverse-mountain": "RM_",
    "whisky-peak": "WH_",
    "little-garden": "LI_",
    "drum-island": "DI_",
    "arabasta": "AL_",
    "jaya": "JA_",
    "skypiea": "SK_",
    "long-ring-long-land": "LR_",
    "water-7": "WS_",
    "enies-lobby": "EN_",
    "post-enies-lobby": "PEN_",
    "thriller-bark": "TB_",
    "sabaody": "SAB_",
    "amazon-lily": "AM_",
    "impel-down": "IM_",
    "marineford": "MA_",
    "post-war": "PW_",
    "return-to-sabaody": "RTS_"
};

function onePacePrefixesForArc(id) {
    var prefix = onePacePrefixByArc[id];
    return prefix ? [prefix] : [];
}

function copyRows(rows) {
    var out = [];
    for (var i = 0; i < (rows || []).length; ++i) {
        var row = {};
        for (var key in rows[i]) row[key] = rows[i][key];
        out.push(row);
    }
    return out;
}

function relatedMediaFor(id) {
    if (id === "arabasta")
        return [{ id: "tt1037116", type: "movie",
                  title: "Episode of Alabasta: The Desert Princess and the Pirates", year: "2007",
                  episode: "",
                  thumbnail: "https://live.metahub.space/background/medium/tt1037116/img" }];
    if (id === "skypiea")
        return [{ id: "tt11744496", type: "movie", title: "Episode of Skypiea", year: "2018",
                  episode: "",
                  thumbnail: "https://live.metahub.space/background/medium/tt11744496/img" }];
    return [];
}
function laneStatus(liveActionSeason, relatedRows, onePacePrefixes) {
    return {
        anime: "mapped",
        manga: "mapped",
        colorManga: "mapped",
        onePace: onePacePrefixes.length ? "mapped-current-addon-metadata" : "unavailable",
        liveAction: liveActionSeason > 0 ? "mapped" : "unavailable",
        relatedMedia: relatedRows.length ? "curated" : "unmapped"
    };
}

function record(id, order, title, markerId, locationId, place, sagaLabel, summary,
                anime, chapters, volumes, sourcePage, liveActionSeason,
                liveActionEpisodes, note) {
    var related = relatedMediaFor(id);
    var onePacePrefixes = onePacePrefixesForArc(id);
    return {
        id: id, order: order, title: title, markerId: markerId, locationId: locationId,
        place: place, geoLabel: place, sagaLabel: sagaLabel,
        seaId: SEA_ID, seaLabel: SEA_LABEL,
        summary: summary, anime: anime, chapters: chapters, volumes: volumes,
        source: "https://onepiece.fandom.com/wiki/" + sourcePage,
        mappingNote: note || "Broadcast arc grouping; individual episodes may contain anime-original scenes.",
        onePacePrefixes: onePacePrefixes, liveActionSeason: liveActionSeason || 0,
        liveActionEpisodes: liveActionEpisodes || "",
        heroSource: BANNER_ROOT + id + ".png",
        markerSource: BADGE_ROOT + id + ".png",
        relatedMedia: copyRows(related),
        mediaMappingStatus: laneStatus(liveActionSeason || 0, related, onePacePrefixes),
        providerSelectors: {
            anime: { rootId: "kitsu:12", episodeSpec: anime },
            onePace: {
                extensionId: "com.onepace.fedew",
                metaId: "pp_onepace",
                videoIdPrefixes: onePacePrefixes
            },
            liveAction: {
                rootId: "tt11737520",
                season: liveActionSeason || 0,
                episodeSpec: liveActionEpisodes || ""
            },
            manga: {
                malId: 13,
                volumeSpec: volumes,
                regularEntryId: "30013",
                colorEntryId: "one-piece-color"
            }
        }
    };
}

var arcs = [
    record("reverse-mountain", 7, "Reverse Mountain", "reverse-mountain", "twin-cape",
        "Twin Cape · Reverse Mountain", "Arabasta",
        "Beyond Reverse Mountain, the crew meets Crocus and the whale Laboon, and learns how to navigate the Grand Line.",
        "62-63", "101-105", "12", "Reverse_Mountain_Arc", 2, "2"),

    record("whisky-peak", 8, "Whiskey Peak", "whisky-peak", "cactus-island",
        "Whiskey Peak · Cactus Island", "Arabasta",
        "An extravagant welcome conceals Baroque Works bounty hunters. The crew becomes involved in Vivi's journey home.",
        "64-67", "106-114", "12-13", "Whisky_Peak_Arc", 2, "3",
        "Displayed as Whiskey Peak to match the atlas; the reference wiki uses Whisky Peak."),

    record("little-garden", 9, "Little Garden", "little-garden", "little-garden",
        "Little Garden", "Arabasta",
        "On a prehistoric island, the crew meets the dueling giants Dorry and Brogy while Baroque Works closes in.",
        "70-77", "115-129", "13-15", "Little_Garden_Arc", 2, "4-5"),

    record("drum-island", 10, "Drum Island", "drum-island", "drum-island",
        "Drum Island · Sakura Kingdom", "Arabasta",
        "Nami's illness sends the crew searching for a doctor. Beneath the Drum Rockies, Luffy meets Chopper and Doctor Kureha.",
        "78-91", "130-154", "15-17", "Drum_Island_Arc", 2, "6-8"),
    record("arabasta", 11, "Arabasta", "arabasta", "sandy-island",
        "Arabasta Kingdom · Sandy Island", "Arabasta",
        "Vivi returns to a kingdom on the edge of civil war. The Straw Hats cross the desert to confront Crocodile's plans.",
        "92-130", "155-217", "17-24", "Arabasta_Arc", 0, ""),

    record("jaya", 12, "Jaya", "jaya", "jaya",
        "Mock Town · Jaya", "Sky Island",
        "A Log Pose points skyward. At Jaya, Luffy encounters Bellamy and Blackbeard, while Cricket helps the crew seek a route into the clouds.",
        "144-152", "218-236", "24-25", "Jaya_Arc", 0, ""),

    record("skypiea", 13, "Skypiea", "skypiea", "skypiea",
        "White-White Sea · Upper Yard", "Sky Island",
        "The Knock Up Stream carries the crew to the sky. Their voyage reaches Upper Yard, Shandora and Enel's domain.",
        "153-195", "237-302", "26-32", "Skypiea_Arc", 0, ""),

    record("long-ring-long-land", 14, "Long Ring Long Land", "long-ring-long-land",
        "long-ring-long-land", "Long Ring Long Land", "Water 7",
        "Foxy challenges the crew to a Davy Back Fight. Their encounter with Admiral Aokiji follows before the journey to Water 7.",
        "207-219,227-228", "303-321", "32-34", "Long_Ring_Long_Land_Arc", 0, "",
        "Includes the Aokiji continuation in episodes 227-228; the chart follows manga geography."),
    record("water-7", 15, "Water 7", "water-7", "water-7",
        "Water 7", "Water 7",
        "The crew seeks repairs for the Going Merry in a city of shipwrights. Robin's disappearance and the ship's fate divide the Straw Hats.",
        "229-263", "322-374", "34-39", "Water_7_Arc", 0, ""),

    record("enies-lobby", 16, "Enies Lobby", "enies-lobby", "enies-lobby",
        "Enies Lobby · Judicial Island", "Water 7",
        "The Straw Hats follow the sea train to the judicial island, declare their resolve to rescue Robin, and face CP9.",
        "264-290,293-302,304-312", "375-430", "39-44", "Enies_Lobby_Arc", 0, ""),

    record("post-enies-lobby", 17, "Post-Enies Lobby", "water-7", "water-7",
        "Water 7 · Return voyage", "Water 7",
        "Back in Water 7, the crew recovers, receives new bounties, and prepares to depart aboard the Thousand Sunny.",
        "313-325", "431-441", "45-46", "Post-Enies_Lobby_Arc", 0, ""),

    record("thriller-bark", 18, "Thriller Bark", "thriller-bark", "thriller-bark",
        "Thriller Bark · Florian Triangle", "Thriller Bark",
        "In the Florian Triangle, the crew meets Brook and boards Moria's enormous ship, where stolen shadows animate the dead.",
        "337-381", "442-489", "46-50", "Thriller_Bark_Arc", 0, "",
        "Thriller Bark is a mobile island-ship; the chart position represents this visit."),
    record("sabaody", 19, "Sabaody Archipelago", "sabaody", "sabaody",
        "Sabaody Archipelago · Red Line", "Summit War",
        "After rescuing Camie from Duval's base, the crew seeks ship coating at Sabaody. The confrontation there ends with their separation.",
        "385-405", "490-513", "50-53", "Sabaody_Archipelago_Arc", 0, ""),

    record("amazon-lily", 20, "Amazon Lily", "amazon-lily", "amazon-lily",
        "Amazon Lily · Calm Belt", "Summit War",
        "Sent away from Sabaody by Kuma, Luffy lands in the Kuja kingdom and meets Boa Hancock. News of Ace changes his course.",
        "408-421", "514-524", "53-54", "Amazon_Lily_Arc", 0, "",
        "Episodes 418-421 also follow the separated crew; they do not add Paradise destinations to Luffy's chart."),

    record("impel-down", 21, "Impel Down", "impel-down", "impel-down",
        "Impel Down · Calm Belt", "Summit War",
        "With Hancock's help, Luffy enters the underwater prison to reach Ace, gathering unlikely allies during his descent and escape.",
        "422-425,430-452", "525-549", "54-56", "Impel_Down_Arc", 0, ""),

    record("marineford", 22, "Marineford", "marineford", "marineford",
        "Marineford · Former Marine Headquarters", "Summit War",
        "Luffy arrives at the war between the Marines and Whitebeard's forces, determined to reach Ace before his execution.",
        "457-489", "550-580", "56-59", "Marineford_Arc", 0, "",
        "This is the Marineford of the Summit War, not the later headquarters in the New World."),
    record("post-war", 23, "Post-War", "post-war", "rusukaina",
        "Amazon Lily · Marineford · Rusukaina", "Summit War",
        "Luffy recovers near Amazon Lily, returns briefly to Marineford, and begins training with Rayleigh on Rusukaina.",
        "490-491,493-516", "581-597", "59-61", "Post-War_Arc", 0, "",
        "Rusukaina anchors the present-day recovery and training chapter. The childhood flashback is in East Blue."),

    record("return-to-sabaody", 24, "Return to Sabaody", "sabaody", "sabaody",
        "Sabaody Archipelago · Reunion", "Fish-Man Island",
        "After two years, the Straw Hats return to the same archipelago and prepare to descend toward Fish-Man Island.",
        "517-522", "598-602", "61", "Return_to_Sabaody_Arc", 0, "",
        "Included as the chart's departure epilogue; Fish-Man Island and the New World are outside this atlas.")
];

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
        if (arcs[i].locationId === id) result.push(arcs[i]);
    return result;
}

// Bundled values above are the offline starter snapshot.
function syncContent() {
    if (typeof ContentApi === "undefined") return;
    var data = ContentApi.document("tribute-one-piece-paradise").story;
    if (!data || !Object.keys(data).length) return;
    data = ContentApi.resolveAssets(data);
    if (data.SEA_ID !== undefined && (Array.isArray(SEA_ID) ? Array.isArray(data.SEA_ID) : typeof data.SEA_ID === typeof SEA_ID)) SEA_ID = data.SEA_ID;
    if (data.SEA_LABEL !== undefined && (Array.isArray(SEA_LABEL) ? Array.isArray(data.SEA_LABEL) : typeof data.SEA_LABEL === typeof SEA_LABEL)) SEA_LABEL = data.SEA_LABEL;
    if (data.BADGE_ROOT !== undefined && (Array.isArray(BADGE_ROOT) ? Array.isArray(data.BADGE_ROOT) : typeof data.BADGE_ROOT === typeof BADGE_ROOT)) BADGE_ROOT = data.BADGE_ROOT;
    if (data.BANNER_ROOT !== undefined && (Array.isArray(BANNER_ROOT) ? Array.isArray(data.BANNER_ROOT) : typeof data.BANNER_ROOT === typeof BANNER_ROOT)) BANNER_ROOT = data.BANNER_ROOT;
    if (data.onePacePrefixByArc !== undefined && (Array.isArray(onePacePrefixByArc) ? Array.isArray(data.onePacePrefixByArc) : typeof data.onePacePrefixByArc === typeof onePacePrefixByArc)) onePacePrefixByArc = data.onePacePrefixByArc;
    if (data.arcs !== undefined && (Array.isArray(arcs) ? Array.isArray(data.arcs) : typeof data.arcs === typeof arcs)) arcs = data.arcs;
}
if (typeof ContentApi !== "undefined") ContentApi.register(syncContent);
