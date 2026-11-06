.pragma library
.import "../TheatreApi.js" as TheatreApi

function specimen(id, title, season, pigment) {
    return {
        id: id,
        title: title,
        season: season,
        cover: TheatreApi.posterFor(id),
        pigment: "materials/blood-" + String(pigment).padStart(2, "0") + ".png"
    };
}

var items = [
    specimen("tt0773262", "Dexter", 1, 1),
    specimen("tt1595870", "Dexter: Early Cuts", 0, 2),
    specimen("tt0773262", "Dexter", 2, 3),
    specimen("tt0773262", "Dexter", 3, 4),
    specimen("tt0773262", "Dexter", 4, 5),
    specimen("tt0773262", "Dexter", 5, 6),
    specimen("tt0773262", "Dexter", 6, 7),
    specimen("tt0773262", "Dexter", 7, 8),
    specimen("tt0773262", "Dexter", 8, 9),
    specimen("tt14164730", "Dexter: New Blood", 1, 10),
    specimen("tt32252772", "Dexter: Original Sin", 1, 11),
    specimen("tt33043892", "Dexter: Resurrection", 1, 12),
    specimen("tt33043892", "Dexter: Resurrection", 2, 13)
];

function theatreItem(index) {
    var item = items[index];
    if (!item) return null;
    var route = { id: item.id, title: item.title, type: "series", cover: item.cover };
    if (item.season > 0) route.requestedSeason = item.season;
    return route;
}
