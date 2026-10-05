.pragma library

// An interpretive, non-metric projection. These are composition coordinates,
// not asserted canonical bearings, island sizes, or navigable sailing tracks.
var designWidth = 1680;
var designHeight = 960;

var locations = [
    { id: "reverse-mountain", x: 0.088, y: 0.5, kind: "mountain", domain: "red-line", label: "REVERSE\nMOUNTAIN", labelX: 0.095, labelY: 0.6, size: 20, w: 82, h: 132, detail: "" },
    { id: "twin-cape", x: 0.15, y: 0.51, kind: "cape", domain: "paradise", label: "TWIN CAPE", labelX: 0.148, labelY: 0.563, size: 13, w: 58, h: 62, detail: "Crocus · Laboon" },
    { id: "cactus-island", x: 0.23, y: 0.4, kind: "cactus", domain: "paradise", label: "WHISKEY PEAK", labelX: 0.23, labelY: 0.457, size: 16, w: 88, h: 78, detail: "Cactus Island" },
    { id: "little-garden", x: 0.3, y: 0.64, kind: "prehistoric", domain: "paradise", label: "LITTLE GARDEN", labelX: 0.3, labelY: 0.695, size: 15, w: 98, h: 74, detail: "Dorry · Brogy" },
    { id: "drum-island", x: 0.36, y: 0.4, kind: "drum", domain: "paradise", label: "DRUM ISLAND", labelX: 0.36, labelY: 0.466, size: 16, w: 90, h: 100, detail: "Drum Rockies · Sakura Kingdom" },
    { id: "sandy-island", x: 0.425, y: 0.62, kind: "desert", domain: "paradise", label: "ARABASTA", labelX: 0.425, labelY: 0.701, size: 21, w: 150, h: 136, detail: "Sandy Island", sublabels: ["Nanohana", "Erumalu", "Yuba", "Rainbase", "Alubarna"] },
    { id: "jaya", x: 0.517, y: 0.49, kind: "jaya", domain: "paradise", label: "JAYA", labelX: 0.517, labelY: 0.547, size: 18, w: 82, h: 76, detail: "Mock Town · Cricket's home" },
    { id: "skypiea", x: 0.515, y: 0.195, kind: "sky", domain: "sky", projection: "altitude-inset", label: "SKYPIEA", labelX: 0.515, labelY: 0.073, size: 22, w: 156, h: 78, detail: "White Sea · White-White Sea", sublabels: ["Angel Island", "Upper Yard", "Shandora", "Giant Jack"] },
    { id: "long-ring-long-land", x: 0.615, y: 0.392, kind: "ring", domain: "paradise", label: "LONG RING\nLONG LAND", labelX: 0.615, labelY: 0.445, size: 14, w: 92, h: 70, detail: "Davy Back Fight" },
    { id: "water-7", x: 0.666, y: 0.642, kind: "water-city", domain: "paradise", label: "WATER 7", labelX: 0.666, labelY: 0.702, size: 19, w: 96, h: 90, detail: "Galley-La · Sea train", sublabels: ["Water 7", "Post-Enies Lobby"] },
    { id: "enies-lobby", x: 0.73, y: 0.36, kind: "judicial", domain: "paradise", label: "ENIES LOBBY", labelX: 0.73, labelY: 0.425, size: 17, w: 100, h: 92, detail: "Judicial Island · Gates of Justice" },
    { id: "florian-triangle", x: 0.805, y: 0.595, kind: "region", domain: "paradise", label: "", labelX: 0.805, labelY: 0.7, size: 16, w: 230, h: 145, detail: "Fogbound waters" },
    { id: "thriller-bark", x: 0.818, y: 0.6, kind: "ship", domain: "paradise", label: "THRILLER BARK", labelX: 0.818, labelY: 0.657, size: 15, w: 90, h: 92, detail: "Mobile island-ship" },
    { id: "flying-fish-base", x: 0.835, y: 0.486, kind: "platform", domain: "paradise", label: "Flying Fish Riders' Base", labelX: 0.835, labelY: 0.517, size: 10, w: 48, h: 30, detail: "Duval · Camie's rescue", minor: true },
    { id: "sabaody", x: 0.873, y: 0.355, kind: "mangrove", domain: "paradise", label: "SABAODY", labelX: 0.873, labelY: 0.418, size: 19, w: 96, h: 100, detail: "Mangrove archipelago · Reunion" },
    { id: "marineford", x: 0.875, y: 0.795, kind: "fortress", domain: "paradise", label: "MARINEFORD", labelX: 0.875, labelY: 0.85, size: 16, w: 86, h: 76, detail: "Marine Headquarters during the Summit War" },
    { id: "amazon-lily", x: 0.645, y: 0.835, kind: "kuja", domain: "calm-belt", label: "AMAZON LILY", labelX: 0.645, labelY: 0.885, size: 16, w: 88, h: 68, detail: "Kuja Kingdom · Recovery after the war" },
    { id: "rusukaina", x: 0.525, y: 0.835, kind: "wild", domain: "calm-belt", label: "RUSUKAINA", labelX: 0.525, labelY: 0.88, size: 14, w: 74, h: 60, detail: "Rayleigh · Two-year training" },
    { id: "impel-down", x: 0.77, y: 0.835, kind: "prison", domain: "calm-belt", label: "IMPEL DOWN", labelX: 0.77, labelY: 0.888, size: 15, w: 60, h: 72, detail: "Underwater prison · Gates of Justice" }
];

// Seals sit beside the drawn landmark rather than replacing its silhouette.
var canonMarkers = [
    { id: "reverse-mountain", locationId: "twin-cape", x: 0.194, y: 0.5, arcIds: ["reverse-mountain"] },
    { id: "whisky-peak", locationId: "cactus-island", x: 0.235, y: 0.307, arcIds: ["whisky-peak"] },
    { id: "little-garden", locationId: "little-garden", x: 0.302, y: 0.548, arcIds: ["little-garden"] },
    { id: "drum-island", locationId: "drum-island", x: 0.367, y: 0.3, arcIds: ["drum-island"] },
    { id: "arabasta", locationId: "sandy-island", x: 0.355, y: 0.622, arcIds: ["arabasta"] },
    { id: "jaya", locationId: "jaya", x: 0.518, y: 0.402, arcIds: ["jaya"] },
    { id: "skypiea", locationId: "skypiea", x: 0.66, y: 0.205, arcIds: ["skypiea"] },
    { id: "long-ring-long-land", locationId: "long-ring-long-land", x: 0.663, y: 0.377, arcIds: ["long-ring-long-land"] },
    { id: "water-7", locationId: "water-7", x: 0.634, y: 0.557, arcIds: ["water-7", "post-enies-lobby"] },
    { id: "enies-lobby", locationId: "enies-lobby", x: 0.747, y: 0.263, arcIds: ["enies-lobby"] },
    { id: "thriller-bark", locationId: "thriller-bark", x: 0.767, y: 0.593, arcIds: ["thriller-bark"] },
    { id: "sabaody", locationId: "sabaody", x: 0.88, y: 0.257, arcIds: ["sabaody", "return-to-sabaody"] },
    { id: "amazon-lily", locationId: "amazon-lily", x: 0.694, y: 0.84, arcIds: ["amazon-lily"] },
    { id: "impel-down", locationId: "impel-down", x: 0.824, y: 0.815, arcIds: ["impel-down"] },
    { id: "marineford", locationId: "marineford", x: 0.869, y: 0.712, arcIds: ["marineford"] },
    { id: "post-war", locationId: "rusukaina", x: 0.575, y: 0.825, arcIds: ["post-war"] }
];

var primaryVoyage = ["reverse-mountain", "twin-cape", "cactus-island", "little-garden", "drum-island", "sandy-island", "jaya", "skypiea", "long-ring-long-land", "water-7", "enies-lobby", "water-7", "florian-triangle", "thriller-bark", "flying-fish-base", "sabaody"];
var secondaryVoyage = ["sabaody", "amazon-lily", "impel-down", "marineford", "amazon-lily", "marineford", "amazon-lily", "rusukaina", "sabaody"];

var routeLegs = [
    { id: "enter", route: "primary", kind: "sailing", from: "reverse-mountain", to: "twin-cape", c1: [0.11, 0.5], c2: [0.13, 0.51] },
    { id: "cape-cactus", route: "primary", kind: "sailing", from: "twin-cape", to: "cactus-island", c1: [0.185, 0.52], c2: [0.195, 0.405] },
    { id: "cactus-garden", route: "primary", kind: "sailing", from: "cactus-island", to: "little-garden", c1: [0.265, 0.395], c2: [0.245, 0.635] },
    { id: "garden-drum", route: "primary", kind: "sailing", from: "little-garden", to: "drum-island", c1: [0.375, 0.655], c2: [0.435, 0.425] },
    { id: "drum-arabasta", route: "primary", kind: "sailing", from: "drum-island", to: "sandy-island", c1: [0.395, 0.325], c2: [0.45, 0.465] },
    { id: "arabasta-jaya", route: "primary", kind: "sailing", from: "sandy-island", to: "jaya", c1: [0.49, 0.675], c2: [0.492, 0.515] },
    { id: "knock-up", route: "primary", kind: "ascent", from: "jaya", to: "skypiea", c1: [0.482, 0.425], c2: [0.49, 0.31] },
    { id: "sky-descent", route: "primary", kind: "descent", from: "skypiea", to: "long-ring-long-land", c1: [0.57, 0.22], c2: [0.605, 0.287] },
    { id: "ring-water", route: "primary", kind: "sailing", from: "long-ring-long-land", to: "water-7", c1: [0.685, 0.387], c2: [0.611, 0.582] },
    { id: "sea-train-out", route: "primary", kind: "sea-train", from: "water-7", to: "enies-lobby", c1: [0.711, 0.567], c2: [0.71, 0.395] },
    { id: "merry-return", route: "primary", kind: "return", from: "enies-lobby", to: "water-7", c1: [0.778, 0.455], c2: [0.729, 0.692] },
    { id: "water-fog", route: "primary", kind: "sailing", from: "water-7", to: "florian-triangle", c1: [0.696, 0.687], c2: [0.777, 0.64] },
    { id: "fog-ship", route: "primary", kind: "sailing", from: "florian-triangle", to: "thriller-bark", c1: [0.817, 0.61], c2: [0.81, 0.615] },
    { id: "ship-base", route: "primary", kind: "sailing", from: "thriller-bark", to: "flying-fish-base", c1: [0.855, 0.6], c2: [0.822, 0.524] },
    { id: "base-sabaody", route: "primary", kind: "sailing", from: "flying-fish-base", to: "sabaody", c1: [0.862, 0.494], c2: [0.848, 0.39] },
    { id: "separation", route: "secondary", kind: "kuma", from: "sabaody", to: "amazon-lily", c1: [0.947, 0.53], c2: [0.744, 0.938] },
    { id: "kuja-prison", route: "secondary", kind: "sailing", from: "amazon-lily", to: "impel-down", c1: [0.684, 0.868], c2: [0.729, 0.863] },
    { id: "prison-war", route: "secondary", kind: "government-current", from: "impel-down", to: "marineford", c1: [0.801, 0.781], c2: [0.869, 0.875] },
    { id: "war-recovery", route: "secondary", kind: "recovery", from: "marineford", to: "amazon-lily", c1: [0.83, 0.839], c2: [0.686, 0.733] },
    { id: "message-return", route: "secondary", kind: "return", from: "amazon-lily", to: "marineford", c1: [0.722, 0.723], c2: [0.851, 0.869] },
    { id: "training", route: "secondary", kind: "sailing", from: "amazon-lily", to: "rusukaina", c1: [0.613, 0.846], c2: [0.555, 0.895] },
    { id: "reunion", route: "secondary", kind: "reunion", from: "rusukaina", to: "sabaody", c1: [0.555, 0.758], c2: [0.896, 0.7] }
];

function location(id) {
    if (typeof id !== "string" || !id.length) return null;
    for (var i = 0; i < locations.length; ++i)
        if (locations[i].id === id) return locations[i];
    return null;
}

function marker(id) {
    if (typeof id !== "string" || !id.length) return null;
    for (var i = 0; i < canonMarkers.length; ++i)
        if (canonMarkers[i].id === id) return canonMarkers[i];
    return null;
}

function markerForArc(id) {
    if (typeof id !== "string" || !id.length) return null;
    for (var i = 0; i < canonMarkers.length; ++i)
        if (canonMarkers[i].arcIds.indexOf(id) !== -1) return canonMarkers[i];
    return null;
}
