.pragma library
.import "UniverseContentApi.js" as ContentApi

// Static atlas geometry and Index payloads. Story/provider metadata remains in
// OnePieceEastBlueData.js; these records only bind the fitted plate to native UI.
var canonMarkers = [
    { id: "romance", x: 0.11131, y: 0.35208, badge: "../assets/universes/one-piece/east-blue-markers/romance.png", poster: "../assets/universes/one-piece/east-blue/canon/romance-dawn.png" },
    { id: "orange", x: 0.41500, y: 0.31667, badge: "../assets/universes/one-piece/east-blue-markers/orange.png", poster: "../assets/universes/one-piece/east-blue/canon/orange-town.png" },
    { id: "syrup", x: 0.61500, y: 0.41200, badge: "../assets/universes/one-piece/east-blue-markers/syrup.png", poster: "../assets/universes/one-piece/east-blue/canon/syrup-village.png" },
    { id: "baratie", x: 0.35000, y: 0.64000, badge: "../assets/universes/one-piece/east-blue-markers/baratie.png", poster: "../assets/universes/one-piece/east-blue/canon/baratie.png" },
    { id: "arlong", x: 0.80000, y: 0.35000, badge: "../assets/universes/one-piece/east-blue-markers/arlong.png", poster: "../assets/universes/one-piece/east-blue/canon/arlong-park.png" },
    { id: "loguetown", x: 0.75000, y: 0.69000, badge: "../assets/universes/one-piece/east-blue-markers/loguetown.png", poster: "../assets/universes/one-piece/east-blue/canon/loguetown.png" }
];

function badgeSource(id) {
    var data = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-east-blue") : {};
    if (data.artByArc && data.artByArc[id]) return ContentApi.resolveAssets(data.artByArc[id]).badgeSource;
    for (var i = 0; i < canonMarkers.length; ++i)
        if (canonMarkers[i].id === id)
            return canonMarkers[i].badge;
    return "";
}

function bannerSource(id) {
    var data = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-east-blue") : {};
    if (data.artByArc && data.artByArc[id]) return ContentApi.resolveAssets(data.artByArc[id]).posterSource;
    for (var i = 0; i < canonMarkers.length; ++i)
        if (canonMarkers[i].id === id)
            return "../assets/universes/one-piece/east-blue/banners/" + id + ".png";
    return "";
}

var nonCanonEntries = [
    { id: "ganzack", title: "One Piece: Defeat Him! The Pirate Ganzack!", year: "1998", kindLabel: "OVA", placement: "Between Orange Town and Syrup Village", poster: "../assets/universes/one-piece/east-blue/noncanon/01-ganzack.png", entry: { id: "tt1012788", type: "movie", title: "One Piece: Defeat Him! The Pirate Ganzack!", year: "1998" } },
    { id: "one-piece-movie", title: "One Piece: The Movie", year: "2000", kindLabel: "MOVIE 1", placement: "Between Syrup Village and Baratie", poster: "../assets/universes/one-piece/east-blue/noncanon/02-one-piece-movie.png", entry: { id: "tt0814243", type: "movie", title: "One Piece: The Movie", year: "2000" } },
    { id: "oceans-navel", title: "Adventure in the Ocean's Navel", year: "2000", kindLabel: "TV SPECIAL 1", placement: "Near Loguetown · between episodes 52 and 53", poster: "../assets/universes/one-piece/east-blue/noncanon/03-oceans-navel.png", entry: { id: "tt0975705", type: "movie", title: "Adventure in the Ocean's Navel", year: "2000" } },
    { id: "jangos-dance-carnival", title: "Jango's Dance Carnival", year: "2001", kindLabel: "THEATRICAL SHORT", placement: "After Arlong Park · before Reverse Mountain", poster: "../assets/universes/one-piece/east-blue/noncanon/04-jangos-dance-carnival.png", entry: null },
    { id: "clockwork-island", title: "Clockwork Island Adventure", year: "2001", kindLabel: "MOVIE 2", placement: "After Loguetown · before Reverse Mountain", poster: "../assets/universes/one-piece/east-blue/noncanon/05-clockwork-island.png", entry: { id: "tt0832449", type: "movie", title: "Clockwork Island Adventure", year: "2001" } }
];

function canonMarker(id) {
    for (var i = 0; i < canonMarkers.length; ++i)
        if (canonMarkers[i].id === id) return canonMarkers[i];
    return canonMarkers[0];
}

function nonCanonEntry(id) {
    for (var i = 0; i < nonCanonEntries.length; ++i)
        if (nonCanonEntries[i].id === id) return nonCanonEntries[i];
    return null;
}

// Bundled values above are the offline starter snapshot.
function syncContent() {
    if (typeof ContentApi === "undefined") return;
    var data = ContentApi.document("tribute-one-piece-east-blue").atlas;
    if (!data || !Object.keys(data).length) return;
    data = ContentApi.resolveAssets(data);
    if (data.canonMarkers !== undefined && (Array.isArray(canonMarkers) ? Array.isArray(data.canonMarkers) : typeof data.canonMarkers === typeof canonMarkers)) canonMarkers = data.canonMarkers;
    if (data.nonCanonEntries !== undefined && (Array.isArray(nonCanonEntries) ? Array.isArray(data.nonCanonEntries) : typeof data.nonCanonEntries === typeof nonCanonEntries)) nonCanonEntries = data.nonCanonEntries;
}
if (typeof ContentApi !== "undefined") ContentApi.register(syncContent);
