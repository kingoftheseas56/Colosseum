.pragma library

// Chart composition, never official coordinates, distances, island dimensions or bearings.
var designWidth = 1680;
var designHeight = 960;

var locations = [
    { id: "red-line", x: 0.060, y: 0.48, kind: "boundary", domain: "red-line", label: "", labelX: 0.06, labelY: 0.2, size: 22, w: 120, h: 900, detail: "Continental boundary" },
    { id: "sabaody", x: 0.128, y: 0.49, kind: "mangrove", domain: "paradise", label: "Sabaody", labelX: 0.128, labelY: 0.532, size: 12, w: 50, h: 56, minor: true, detail: "" },
    { id: "fish-man-island", x: 0.2, y: 0.75, kind: "fishman", domain: "under-red-line", projection: "depth-section", depthMeters: 10000, arcId: "fish-man-island", label: "FISH-MAN ISLAND", labelX: 0.2, labelY: 0.824, size: 17, w: 140, h: 96, detail: "Ryugu Kingdom", sublabels: ["Ryugu Palace", "Sea Forest", "Gyoncorde Plaza"] },
    { id: "new-world-surface", x: 0.253, y: 0.53, kind: "waypoint", domain: "new-world", label: "Surface passage", labelX: 0.253, labelY: 0.565, size: 11, w: 0, h: 0, minor: true, detail: "" },
    { id: "punk-hazard", x: 0.3, y: 0.43, kind: "punk-hazard", domain: "new-world", arcId: "punk-hazard", label: "PUNK HAZARD", labelX: 0.3, labelY: 0.506, size: 19, w: 144, h: 118, detail: "Burning land · Frozen land · Dividing lake", sublabels: ["Burning land", "Frozen land", "Laboratory", "Central lake"] },
    { id: "dressrosa", x: 0.405, y: 0.66, kind: "dressrosa", domain: "new-world", arcId: "dressrosa", label: "DRESSROSA", labelX: 0.405, labelY: 0.733, size: 20, w: 140, h: 112, detail: "King's Plateau · Corrida Colosseum", sublabels: ["King's Plateau", "Corrida Colosseum", "Flower Hill", "Green Bit"] },
    { id: "green-bit", x: 0.405, y: 0.54, kind: "forest", domain: "new-world", arcId: "dressrosa", visitedByLuffy: false, label: "Green Bit", labelX: 0.447, labelY: 0.525, size: 11, w: 50, h: 48, minZoom: 1.25, minor: true, detail: "North of Dressrosa · Iron bridge" },
    { id: "zou", x: 0.495, y: 0.325, kind: "zunesha", domain: "new-world", arcId: "zou", moving: true, carrier: "Zunesha", positionBasis: "voyage-encounter", label: "ZOU", labelX: 0.495, labelY: 0.413, size: 20, w: 150, h: 132, detail: "Moving with Zunesha", sublabels: ["Mokomo Dukedom", "Whale Tree", "Zunesha"] },
    { id: "totto-land", x: 0.62, y: 0.696, kind: "region", domain: "new-world", arcId: "whole-cake-island", label: "TOTTO LAND", labelX: 0.623, labelY: 0.525, size: 15, w: 302, h: 269, detail: "Big Mom's archipelago" },
    { id: "whole-cake-island", x: 0.635, y: 0.64, kind: "whole-cake", domain: "new-world", parentId: "totto-land", arcId: "whole-cake-island", positionBasis: "arrival-era-landmark", label: "WHOLE CAKE ISLAND", labelX: 0.635, labelY: 0.712, size: 17, w: 112, h: 108, detail: "Whole Cake Chateau · Sweet City", sublabels: ["Whole Cake Chateau", "Seducing Woods", "Sweet City", "Cacao Island"] },
    { id: "cacao", x: 0.575, y: 0.8, kind: "cacao", domain: "new-world", parentId: "totto-land", arcId: "whole-cake-island", label: "Cacao Island", labelX: 0.575, labelY: 0.842, size: 11, w: 56, h: 44, minZoom: 1.25, minor: true, detail: "Chocolatown · Arrival and escape rendezvous" },
    { id: "wano-basin", x: 0.8725, y: 0.5825, kind: "region", domain: "wano-inland-sea", label: "", labelX: 0.865, labelY: 0.26, size: 20, w: 311, h: 389, detail: "Raised freshwater basin within Wano's outer walls" },
    { id: "wano-waterfall", x: 0.758, y: 0.724, kind: "waterfall", domain: "wano-boundary", arcId: "wano-country", label: "Climbing Koi Waterfall", labelX: 0.753, labelY: 0.777, size: 10, w: 44, h: 66, minZoom: 1.25, minor: true, detail: "" },
    { id: "kuri-beach", x: 0.807, y: 0.568, kind: "waypoint", domain: "wano-mainland", parentId: "wano", arcId: "wano-country", label: "Kuri Beach", labelX: 0.795, labelY: 0.582, size: 11, minZoom: 1.5, minor: true, w: 0, h: 0, detail: "" },
    { id: "wano", x: 0.865, y: 0.5, kind: "wano", domain: "wano-mainland", parentId: "wano-basin", arcId: "wano-country", label: "WANO COUNTRY", labelX: 0.865, labelY: 0.263, size: 24, w: 200, h: 156, detail: "Closed country · Raised inland sea", sublabels: ["Mount Fuji", "Flower Capital", "Kuri", "Udon", "Tokage Port", "Onigashima"] },
    { id: "tokage-port", x: 0.87, y: 0.605, kind: "waypoint", domain: "wano-mainland", parentId: "wano", arcId: "wano-country", label: "Tokage Port", labelX: 0.895, labelY: 0.612, size: 11, minZoom: 1.5, minor: true, w: 0, h: 0, detail: "" },
    { id: "onigashima", x: 0.866, y: 0.695, kind: "onigashima", domain: "wano-inland-sea", parentId: "wano-basin", arcId: "wano-country", positionBasis: "pre-raid", label: "ONIGASHIMA", labelX: 0.866, labelY: 0.755, size: 16, w: 90, h: 78, detail: "" }
];

var regions = [
    { id: "totto-land", x: 0.53, y: 0.556, width: 0.18, height: 0.28, kind: "archipelago", mainLocationId: "whole-cake-island" },
    { id: "wano-basin", x: 0.78, y: 0.38, width: 0.185, height: 0.405, kind: "raised-inland-sea", mainLocationId: "wano" }
];

var canonMarkers = [
    { id: "fish-man-island", locationId: "fish-man-island", x: 0.269, y: 0.75, arcIds: ["fish-man-island"] },
    { id: "punk-hazard", locationId: "punk-hazard", x: 0.367, y: 0.425, arcIds: ["punk-hazard"] },
    { id: "dressrosa", locationId: "dressrosa", x: 0.476, y: 0.65, arcIds: ["dressrosa"] },
    { id: "zou", locationId: "zou", x: 0.564, y: 0.307, arcIds: ["zou"] },
    { id: "whole-cake-island", locationId: "whole-cake-island", x: 0.694, y: 0.64, arcIds: ["whole-cake-island"] },
    { id: "wano-country", locationId: "wano", x: 0.95, y: 0.5, arcIds: ["wano-country"] }
];

var primaryVoyage = ["sabaody", "fish-man-island", "new-world-surface", "punk-hazard", "dressrosa", "zou", "cacao", "whole-cake-island", "cacao", "wano-waterfall", "kuri-beach", "wano", "tokage-port", "onigashima", "wano"];
var secondaryVoyage = ["zou", "wano"];

var routeLegs = [
    { id: "descent", route: "primary", kind: "descent", from: "sabaody", to: "fish-man-island", c1: [0.12, 0.624], c2: [0.145, 0.698], note: "Depth-section connector, not a surface bearing" },
    { id: "emergence", route: "primary", kind: "ascent", from: "fish-man-island", to: "new-world-surface", c1: [0.306, 0.758], c2: [0.278, 0.622], note: "Rise beyond the Red Line" },
    { id: "distress-call", route: "primary", kind: "sailing", from: "new-world-surface", to: "punk-hazard", c1: [0.291, 0.512], c2: [0.254, 0.488], vessel: "Thousand Sunny" },
    { id: "alliance", route: "primary", kind: "sailing", from: "punk-hazard", to: "dressrosa", c1: [0.288, 0.628], c2: [0.362, 0.631], vessel: "Thousand Sunny" },
    { id: "to-zunesha", route: "primary", kind: "sailing", from: "dressrosa", to: "zou", c1: [0.493, 0.64], c2: [0.471, 0.476], vessel: "Going Luffy-senpai", note: "The Sunny advance party left earlier; this line follows Luffy" },
    { id: "sanji-retrieval", route: "primary", kind: "sailing", from: "zou", to: "cacao", c1: [0.607, 0.388], c2: [0.512, 0.655], vessel: "Thousand Sunny" },
    { id: "secret-approach", route: "primary", kind: "sailing", from: "cacao", to: "whole-cake-island", c1: [0.584, 0.692], c2: [0.574, 0.595], vessel: "Thousand Sunny" },
    { id: "mirror-rendezvous", route: "primary", kind: "mirror-transit", from: "whole-cake-island", to: "cacao", c1: [0.622, 0.735], c2: [0.619, 0.821], vessel: "", note: "Luffy through the mirror world; not a navigable sea lane" },
    { id: "escape-to-wano", route: "primary", kind: "sailing", from: "cacao", to: "wano-waterfall", c1: [0.641, 0.9], c2: [0.733, 0.871], vessel: "Thousand Sunny" },
    { id: "waterfall-entry", route: "primary", kind: "waterfall-ascent", from: "wano-waterfall", to: "kuri-beach", c1: [0.774, 0.704], c2: [0.779, 0.598], note: "Climb, separation and Kuri arrival compressed into a story leg" },
    { id: "mainland-story", route: "primary", kind: "inland-journey", from: "kuri-beach", to: "wano", c1: [0.805, 0.488], c2: [0.823, 0.54], note: "Condensed mainland journey; not every road or visit" },
    { id: "raid-preparation", route: "primary", kind: "inland-journey", from: "wano", to: "tokage-port", c1: [0.886, 0.54], c2: [0.901, 0.556] },
    { id: "raid", route: "primary", kind: "raid", from: "tokage-port", to: "onigashima", c1: [0.829, 0.646], c2: [0.834, 0.649], vessel: "Thousand Sunny" },
    { id: "wano-conclusion", route: "primary", kind: "return", from: "onigashima", to: "wano", c1: [0.948, 0.671], c2: [0.941, 0.58], note: "Return of the story to mainland Wano, not Onigashima's airborne trajectory" },
    { id: "advance-party", route: "secondary", kind: "advance-party", from: "zou", to: "wano", c1: [0.638, 0.164], c2: [0.756, 0.275], note: "Parallel Straw Hat advance party. Transport and landing unspecified; not Luffy's route." }
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
        if (canonMarkers[i].arcIds.indexOf(id) >= 0) return canonMarkers[i];
    return null;
}
function markerForLocation(id) {
    var place = location(id);
    return place && place.arcId ? markerForArc(place.arcId) : null;
}
