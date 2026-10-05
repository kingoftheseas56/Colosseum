.pragma library

// Interpretive chart composition. No official coordinates, bearings, distances or scale.
var designWidth = 1672;
var designHeight = 941;

var locations = [
    { id: "wano-context", x: 0.095, y: 0.615, kind: "wano-context", domain: "new-world",
      label: "Wano", labelX: 0.095, labelY: 0.682, size: 14, w: 98, h: 83, minor: true,
      detail: "Departure context from the preceding chart" },

    { id: "egghead", x: 0.315, y: 0.555, kind: "egghead", domain: "new-world", arcId: "egghead",
      label: "EGGHEAD", labelX: 0.315, labelY: 0.735, size: 29, w: 282, h: 258,
      detail: "Future Island · winter island altered by Island A.C.",
      sublabels: ["Fabiriophase", "Labophase", "Frontier Dome"] },

    { id: "fabiriophase", x: 0.315, y: 0.635, kind: "factory-layer", domain: "egghead",
      parentId: "egghead", arcId: "egghead", layer: "factory",
      label: "Fabiriophase", labelX: 0.315, labelY: 0.682, size: 12, w: 170, h: 68, minor: true,
      detail: "Factory and Future City level" },

    { id: "labophase", x: 0.315, y: 0.445, kind: "laboratory-layer", domain: "egghead",
      parentId: "egghead", arcId: "egghead", layer: "laboratory",
      label: "Labophase", labelX: 0.315, labelY: 0.342, size: 13, w: 186, h: 92, minor: true,
      detail: "Laboratory on an Island Cloud" },

    { id: "frontier-dome", x: 0.315, y: 0.505, kind: "security-ring", domain: "egghead",
      parentId: "egghead", arcId: "egghead", layer: "defense",
      label: "Frontier Dome", labelX: 0.315, labelY: 0.525, size: 10, w: 210, h: 118, minor: true,
      detail: "Laser defense around the Labophase" },

    { id: "sleeping-mist", x: 0.493, y: 0.505, kind: "region", domain: "sea-route",
      label: "Sleeping mist", labelX: 0.493, labelY: 0.555, size: 11, w: 225, h: 150, minor: true,
      detail: "Interpretive placement of the strange mist encountered en route to Elbaf" },

    { id: "elbaf", x: 0.755, y: 0.495, kind: "adam-tree", domain: "new-world", arcId: "elbaf",
      tree: "Adam", label: "ELBAF", labelX: 0.755, labelY: 0.092, size: 34, w: 445, h: 700,
      detail: "Warland Kingdom · structured vertically around the Adam Tree",
      sublabels: ["Underworld", "Sun World", "Heaven / Astral World"] },

    { id: "land-of-gods", x: 0.625, y: 0.795, kind: "diorama", domain: "elbaf-underworld",
      parentId: "elbaf", arcId: "elbaf", label: "Land of Gods", labelX: 0.625, labelY: 0.848,
      size: 11, w: 105, h: 72, minor: true, detail: "Road's enclosed diorama in the Underworld" },

    { id: "elbaf-underworld", x: 0.755, y: 0.765, kind: "underworld", domain: "elbaf",
      parentId: "elbaf", arcId: "elbaf", tier: 1,
      label: "UNDERWORLD", labelX: 0.755, labelY: 0.865, size: 17, w: 310, h: 178,
      detail: "Snowy mountains and giant beasts beneath Adam's canopy" },

    { id: "sun-world", x: 0.755, y: 0.465, kind: "sun-world", domain: "elbaf",
      parentId: "elbaf", arcId: "elbaf", tier: 2,
      label: "SUN WORLD", labelX: 0.755, labelY: 0.552, size: 18, w: 340, h: 190,
      detail: "Settlements spread across Adam's lower canopy" },

    { id: "heaven-world", x: 0.755, y: 0.205, kind: "heaven-world", domain: "elbaf",
      parentId: "elbaf", arcId: "elbaf", tier: 3,
      label: "HEAVEN / ASTRAL WORLD", labelX: 0.755, labelY: 0.245, size: 14, w: 280, h: 105,
      detail: "Upper canopy; intentionally sparse because little is established" },

    { id: "owl-library", x: 0.684, y: 0.455, kind: "library", domain: "sun-world",
      parentId: "elbaf", arcId: "elbaf", label: "Owl Library", labelX: 0.675, labelY: 0.507,
      size: 10, w: 68, h: 58, minZoom: 1.25, minor: true, detail: "Branch Route 8" },

    { id: "walrus-school", x: 0.727, y: 0.493, kind: "school", domain: "sun-world",
      parentId: "elbaf", arcId: "elbaf", label: "Walrus School", labelX: 0.718, labelY: 0.528,
      size: 10, w: 62, h: 50, minZoom: 1.25, minor: true, detail: "Warrior's Spring" },

    { id: "warriors-spring", x: 0.704, y: 0.515, kind: "spring", domain: "sun-world",
      parentId: "elbaf", arcId: "elbaf", label: "Warrior's Spring", labelX: 0.68, labelY: 0.55,
      size: 9, w: 75, h: 30, minZoom: 1.5, minor: true, detail: "Waterfalls and shoreline on Branch Route 8" },

    { id: "aurust-castle", x: 0.835, y: 0.405, kind: "castle", domain: "sun-world",
      parentId: "elbaf", arcId: "elbaf", label: "Aurust Castle", labelX: 0.848, labelY: 0.442,
      size: 10, w: 74, h: 72, minZoom: 1.25, minor: true, detail: "Royal castle by the Western Village" }
];

var regions = [
    { id: "egghead-lab-cloud", x: 0.245, y: 0.365, width: 0.14, height: 0.21, kind: "island-cloud" },
    { id: "sleeping-mist", x: 0.405, y: 0.405, width: 0.18, height: 0.22, kind: "mist" },
    { id: "elbaf-tree-zone", x: 0.575, y: 0.10, width: 0.36, height: 0.80, kind: "vertical-kingdom" }
];

var canonMarkers = [
    { id: "egghead", locationId: "egghead", x: 0.38215, y: 0.47510, arcIds: ["egghead"] },
    { id: "elbaf", locationId: "elbaf", x: 0.86965, y: 0.37725, arcIds: ["elbaf"] }
];

var primaryVoyage = ["wano-context", "egghead", "sleeping-mist", "elbaf"];
var internalVoyage = ["land-of-gods", "elbaf-underworld", "sun-world"];

var routeLegs = [
    { id: "wano-to-egghead", route: "primary", kind: "sailing",
      from: "wano-context", to: "egghead", c1: [0.16, 0.66], c2: [0.24, 0.61],
      note: "Interpretive post-Wano sailing leg" },
    { id: "egghead-departure", route: "primary", kind: "sailing",
      from: "egghead", to: "sleeping-mist", c1: [0.37, 0.57], c2: [0.44, 0.53],
      note: "Departure with the Giant Warrior Pirates; not a scale bearing" },
    { id: "mist-to-elbaf", route: "primary", kind: "great-eirik",
      from: "sleeping-mist", to: "elbaf", c1: [0.55, 0.48], c2: [0.64, 0.46],
      note: "Sea crossing into Elbaf; atmospheric mist placement is interpretive" },

    { id: "diorama-escape", route: "internal", kind: "internal",
      from: "land-of-gods", to: "elbaf-underworld", c1: [0.66, 0.82], c2: [0.71, 0.79],
      note: "Separated-party internal movement, not an ocean route" },
    { id: "underworld-ascent", route: "internal", kind: "ascent",
      from: "elbaf-underworld", to: "sun-world", c1: [0.72, 0.69], c2: [0.73, 0.56],
      note: "Vertical movement through Elbaf's tiered geography" }
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
