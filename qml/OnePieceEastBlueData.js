.pragma library

// Summaries are concise paraphrases of One Piece Wiki arc overviews, verified 2026-09-01.
var arcs = [
    {
        id: "romance", order: 1, title: "Romance Dawn",
        place: "Foosha Village · Dawn Island", geoLabel: "Dawn Island",
        summary: "Luffy begins his journey, meets Koby, and reaches Shells Town, where he encounters Zoro and Captain Morgan.",
        anime: "1-3", episodeCount: 3, onePacePrefixes: ["RO_"],
        liveActionSeason: 1, liveActionEpisodes: "1",
        chapters: "1-7", chapterCount: 7, volumes: "1", focusX: 0.10, focusY: 0.30,
        markerX: 0.10, markerY: 0.30, geoX: 0.16, geoY: 0.42
    },
    {
        id: "orange", order: 2, title: "Orange Town",
        place: "Orange Town · Organ Islands", geoLabel: "Organ Islands",
        summary: "Luffy and Zoro arrive in Orange Town, occupied by Buggy's crew, and meet Nami, a thief who targets pirates.",
        anime: "4-8", episodeCount: 5, onePacePrefixes: ["OR_"],
        liveActionSeason: 1, liveActionEpisodes: "2",
        chapters: "8-21", chapterCount: 14, volumes: "1-3", focusX: 0.28, focusY: 0.34,
        markerX: 0.28, markerY: 0.34, geoX: 0.28, geoY: 0.58
    },
    {
        id: "syrup", order: 3, title: "Syrup Village",
        place: "Syrup Village · Gecko Islands", geoLabel: "Gecko Islands",
        summary: "The crew meets Usopp and learns of Captain Kuro's plot against Kaya.",
        anime: "9-18", episodeCount: 10, onePacePrefixes: ["SY_", "GA_"],
        liveActionSeason: 1, liveActionEpisodes: "3-4",
        chapters: "22-41", chapterCount: 20, volumes: "3-5", focusX: 0.43, focusY: 0.31,
        markerX: 0.43, markerY: 0.31, geoX: 0.43, geoY: 0.55
    },
    {
        id: "baratie", order: 4, title: "Baratie",
        place: "Baratie · East Blue", geoLabel: "Baratie waters",
        summary: "At the floating restaurant Baratie, Luffy meets Sanji while Don Krieg targets the ship.",
        anime: "19-30", episodeCount: 12, onePacePrefixes: ["BA_"],
        liveActionSeason: 1, liveActionEpisodes: "5-6",
        chapters: "42-68", chapterCount: 27, volumes: "5-8", focusX: 0.57, focusY: 0.40,
        markerX: 0.57, markerY: 0.40, geoX: 0.57, geoY: 0.67
    },
    {
        id: "arlong", order: 5, title: "Arlong Park",
        place: "Arlong Park · Conomi Islands", geoLabel: "Conomi Islands",
        summary: "The crew follows Nami to Cocoyasi Village and discovers her connection to Arlong and the Fish-Man Pirates.",
        anime: "31-44", episodeCount: 14, onePacePrefixes: ["AR_"],
        liveActionSeason: 1, liveActionEpisodes: "7-8",
        chapters: "69-95", chapterCount: 27, volumes: "8-11", focusX: 0.74, focusY: 0.31,
        markerX: 0.74, markerY: 0.31, geoX: 0.74, geoY: 0.56
    },
    {
        id: "loguetown", order: 6, title: "Loguetown",
        place: "Loguetown · Polestar Islands", geoLabel: "Polestar Islands",
        summary: "Before entering the Grand Line, the Straw Hats stop in Loguetown, the town where Gold Roger was born and executed.",
        anime: "45, 48-53", episodeCount: 7, onePacePrefixes: ["LO_"],
        liveActionSeason: 2, liveActionEpisodes: "1",
        chapters: "96-100", chapterCount: 5, volumes: "11-12", focusX: 0.84, focusY: 0.20,
        markerX: 0.84, markerY: 0.20, geoX: 0.79, geoY: 0.46
    }
];

function arc(id) {
    for (var i = 0; i < arcs.length; ++i) {
        if (arcs[i].id === id)
            return arcs[i];
    }
    return arcs[0];
}
