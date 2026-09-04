.pragma library

// Cartography for the East Blue chart.
//
// Identity lives elsewhere: OnePieceEastBlueData.js owns the arc list, its
// summaries, and the anime / One Pace / live-action / chapter / volume specs
// that OnePieceCatalogApi.js reads. This module owns only *where things sit*
// and *how the chart is drawn*. Join the two on `arcId`.
//
// Anchors are read off the official East Blue chart, which overturns the
// earlier guessed coordinates in three ways worth knowing:
//   1. The voyage runs east to west. Dawn is in the far east; Conomi and
//      Polestar are western neighbours, not opposite corners.
//   2. The Red Line shows on both edges, because the chart is a cylinder laid
//      flat. Reverse Mountain is the south-west threshold, not a right-hand
//      wedge.
//   3. The sea carries side geography that is drawn but carries no arc.
//
// x runs from the west edge, y from the north edge, both normalised to the
// chart stage. rx / ry are half-extents. `seed` and `cluster` drive the
// procedural coastline so a given island keeps its silhouette between runs.

var PIN_ARC = "arc";              // an arc badge; the only pin with a watch action
var PIN_SECONDARY = "secondary";  // named geography with no arc of its own
var PIN_LORE = "lore";            // a place with a story but nothing to watch
var PIN_GATE = "gate";            // the way out of this sea

var places = [
    { id: "dawn", arcId: "romance", ord: 1, pinClass: PIN_ARC, label: true,
      name: "Dawn Island", interior: "Windmill Village",
      tagline: "Guns aren't for threats",
      cx: 0.884, cy: 0.238, rx: 0.044, ry: 0.037, seed: 2207, cluster: 2,
      land: "#668b4e", ridge: "#b89562", coast: "#c9d1a4" },

    { id: "organ", arcId: "orange", ord: 2, pinClass: PIN_ARC, label: true,
      name: "Organ Islands", interior: "Orange Town",
      tagline: "Red Nose",
      cx: 0.566, cy: 0.294, rx: 0.077, ry: 0.043, seed: 5581, cluster: 4,
      land: "#8b7751", ridge: "#c39862", coast: "#d6c498" },

    { id: "gecko", arcId: "syrup", ord: 3, pinClass: PIN_ARC, label: true,
      name: "Gecko Islands", interior: "Syrup Village",
      tagline: "Usopp lies, make him captain",
      cx: 0.444, cy: 0.348, rx: 0.043, ry: 0.029, seed: 7723, cluster: 3,
      land: "#718b61", ridge: "#92a783", coast: "#c4cfb7" },

    { id: "baratie", arcId: "baratie", ord: 4, pinClass: PIN_ARC, label: true,
      name: "Baratie", interior: "Sambas Region", ship: true,
      tagline: "Luffy refuses Sanji's refusal",
      cx: 0.556, cy: 0.610, rx: 0.024, ry: 0.024, seed: 4111, cluster: 1,
      land: "#7b6a4a", ridge: "#b08a5c", coast: "#d6c69a" },

    { id: "conomi", arcId: "arlong", ord: 5, pinClass: PIN_ARC, label: true,
      name: "Conomi Islands", interior: "Cocoyasi Village",
      tagline: "Gomu Gomu no Pinwheel",
      cx: 0.288, cy: 0.324, rx: 0.053, ry: 0.044, seed: 9131, cluster: 5,
      land: "#4f8a62", ridge: "#8cab67", coast: "#d7c997" },

    { id: "polestar", arcId: "loguetown", ord: 6, pinClass: PIN_ARC, label: true,
      name: "Polestar Islands", interior: "Loguetown",
      tagline: "He Smiled",
      cx: 0.158, cy: 0.638, rx: 0.037, ry: 0.049, seed: 3319, cluster: 3,
      land: "#6f746e", ridge: "#9b7562", coast: "#c7bea7" },

    // Named geography that belongs to an arc it does not own, or to none.
    { id: "yotsuba", arcId: null, pinClass: PIN_SECONDARY, label: true,
      name: "Yotsuba Island Region", interior: "Shells Town",
      cx: 0.738, cy: 0.160, rx: 0.071, ry: 0.056, seed: 6101, cluster: 5,
      land: "#5f6f5a", ridge: "#8b8f76", coast: "#b6b8a0" },

    { id: "oykot", arcId: null, pinClass: PIN_SECONDARY, label: true,
      name: "Oykot Kingdom", interior: "",
      cx: 0.352, cy: 0.624, rx: 0.050, ry: 0.041, seed: 4409, cluster: 1,
      land: "#7d8358", ridge: "#a9a077", coast: "#c8c39c" },

    { id: "shimotsuki", arcId: null, pinClass: PIN_LORE, label: true,
      name: "Shimotsuki Village", interior: "",
      cx: 0.898, cy: 0.478, rx: 0.023, ry: 0.021, seed: 8815, cluster: 1,
      land: "#6d7f5c", ridge: "#98a179", coast: "#c2c49e" },

    // Drawn, unnamed at overview scale. Their labels are for zoom.
    { id: "cozia", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Cozia Island",
      cx: 0.178, cy: 0.208, rx: 0.017, ry: 0.015, seed: 3701, cluster: 1,
      land: "#77805e", ridge: "#a09873", coast: "#c3bf9b" },
    { id: "mirrorball", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Mirror Ball Island",
      cx: 0.434, cy: 0.228, rx: 0.014, ry: 0.012, seed: 2903, cluster: 1,
      land: "#74805f", ridge: "#9d9a76", coast: "#c0bd9c" },
    { id: "rareanimals", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Island of Rare Animals",
      cx: 0.586, cy: 0.368, rx: 0.013, ry: 0.011, seed: 5107, cluster: 1,
      land: "#6f8159", ridge: "#9aa072", coast: "#bec19a" },
    { id: "goat", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Goat Island",
      cx: 0.815, cy: 0.201, rx: 0.015, ry: 0.013, seed: 6203, cluster: 1,
      land: "#728059", ridge: "#9d9d74", coast: "#c0be99" },
    { id: "kumate", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Kumate Island",
      cx: 0.781, cy: 0.420, rx: 0.014, ry: 0.013, seed: 7411, cluster: 1,
      land: "#6b7a57", ridge: "#969b71", coast: "#bbbd97" },
    { id: "sixis", arcId: null, pinClass: PIN_SECONDARY, label: false, name: "Sixis Island",
      cx: 0.787, cy: 0.626, rx: 0.014, ry: 0.012, seed: 9601, cluster: 1,
      land: "#707d5b", ridge: "#9a9c76", coast: "#bec09b" }
];

// Land the chart shows but does not identify. Drawn, never labelled, never
// given an action.
var unnamedLand = [
    { cx: 0.360, cy: 0.090, rx: 0.088, ry: 0.046, seed: 1201, cluster: 2 },
    { cx: 0.640, cy: 0.084, rx: 0.045, ry: 0.030, seed: 1307, cluster: 1 }
];

var reverseMountain = {
    id: "reverse", pinClass: PIN_GATE, name: "Reverse Mountain",
    cx: 0.030, cy: 0.790, w: 0.082, h: 0.32
};

// Canonical progression. Ends at the gate, which is not an arc.
var route = ["dawn", "organ", "gecko", "baratie", "conomi", "polestar", "reverse"];

// Globe placement, for the world level that follows the four sea charts.
//
// Convention: the Grand Line is the equator and the Red Line is the prime
// meridian, because on a sphere those two bands are what the world is made of.
// East Blue is the north-east quadrant. Nothing reads these yet -- they exist
// now so the globe is not a retrofit across four seas later.
var globeBounds = { latNorth: 62, latSouth: 8, lonWest: 25, lonEast: 155 };

function place(id) {
    for (var i = 0; i < places.length; ++i)
        if (places[i].id === id) return places[i];
    return null;
}

function placeForArc(arcId) {
    for (var i = 0; i < places.length; ++i)
        if (places[i].arcId === arcId) return places[i];
    return null;
}

function arcPlaces() {
    var out = [];
    for (var i = 0; i < places.length; ++i)
        if (places[i].pinClass === PIN_ARC) out.push(places[i]);
    out.sort(function(a, b) { return a.ord - b.ord; });
    return out;
}

function globeAnchor(id) {
    var p = id === reverseMountain.id ? reverseMountain : place(id);
    if (!p) return null;
    // The gate sits where the two bands cross, by definition.
    if (p.pinClass === PIN_GATE) return { lat: 0, lon: 0 };
    var b = globeBounds;
    return {
        lat: b.latNorth - p.cy * (b.latNorth - b.latSouth),
        lon: b.lonWest + p.cx * (b.lonEast - b.lonWest)
    };
}

// The voyage is derived from the same anchors the badges use, so the line can
// never disagree with the places it joins.
function routePoints(width, height) {
    var w = Number(width) || 1, h = Number(height) || 1;
    var out = [];
    for (var i = 0; i < route.length; ++i) {
        var id = route[i];
        var p = id === reverseMountain.id ? reverseMountain : place(id);
        if (!p) continue;
        out.push({ id: id, x: p.cx * w, y: p.cy * h });
    }
    return out;
}

function taglineForArc(arcId) {
    var p = placeForArc(arcId);
    return p && p.tagline ? p.tagline : "";
}
