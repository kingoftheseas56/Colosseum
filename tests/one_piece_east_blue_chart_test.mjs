import fs from 'fs';

function load(path, exports) {
    const src = fs.readFileSync(path, 'utf8').replace(/^\.pragma library\s*$/m, '');
    const mod = {};
    new Function('module', src + '\n' + exports.map(n => `module.${n}=${n};`).join('\n'))(mod);
    return mod;
}

const chart = load('qml/OnePieceEastBlueChart.js', [
    'places', 'unnamedLand', 'reverseMountain', 'route', 'globeBounds',
    'place', 'placeForArc', 'arcPlaces', 'globeAnchor', 'routePoints', 'taglineForArc',
    'PIN_ARC', 'PIN_SECONDARY', 'PIN_LORE', 'PIN_GATE'
]);
const data = load('qml/OnePieceEastBlueData.js', ['arcs', 'arc']);

let failed = 0;
function ok(cond, label, detail) {
    if (cond) console.log('ok  ' + label);
    else { console.log('FAIL ' + label, detail === undefined ? '' : detail); failed++; }
}
function eq(actual, expected, label) {
    ok(JSON.stringify(actual) === JSON.stringify(expected), label, `${JSON.stringify(actual)} != ${JSON.stringify(expected)}`);
}

// --- the chart and the catalogue must agree on what an arc is --------------
const arcIds = data.arcs.map(a => a.id).sort();
const chartArcIds = chart.arcPlaces().map(p => p.arcId).sort();
eq(chartArcIds, arcIds, 'every catalogue arc has exactly one place on the chart');

for (const a of data.arcs) {
    const p = chart.placeForArc(a.id);
    ok(p !== null, `arc ${a.id} resolves to a place`);
    ok(p && p.ord === a.order, `arc ${a.id} keeps its voyage order`, p && p.ord);
    ok(p && typeof p.tagline === 'string' && p.tagline.length > 0, `arc ${a.id} carries a tagline`);
}

// --- only arc pins may offer a watch action -------------------------------
for (const p of chart.places) {
    const isArc = p.pinClass === chart.PIN_ARC;
    ok(isArc === (p.arcId !== null),
       `${p.id}: an arc id and an arc pin go together`, `${p.pinClass}/${p.arcId}`);
}
ok(chart.place('shimotsuki').pinClass === chart.PIN_LORE,
   'Shimotsuki Village is a lore pin, so it can never promise an episode');

// --- the official chart's east-to-west reading ----------------------------
const dawn = chart.place('dawn'), polestar = chart.place('polestar'), conomi = chart.place('conomi');
ok(dawn.cx > 0.8, 'Dawn Island is in the far east', dawn.cx);
ok(polestar.cx < 0.25 && conomi.cx < 0.35, 'Conomi and Polestar are western neighbours',
   `${conomi.cx}/${polestar.cx}`);
ok(Math.abs(conomi.cx - polestar.cx) < 0.2, 'Conomi and Polestar share a longitude band');
ok(chart.reverseMountain.cx < 0.1 && chart.reverseMountain.cy > 0.6,
   'Reverse Mountain is the south-west threshold');

// --- no arc island is drawn on top of another ------------------------------
// Small side islands may legitimately sit against a larger neighbour -- the
// official chart puts Goat Island right off the Yotsuba region -- so the rule
// is that no two *arc* places may collide, and nothing may be buried inside
// another island's core.
const arcs = chart.arcPlaces();
for (let i = 0; i < arcs.length; ++i) {
    for (let j = i + 1; j < arcs.length; ++j) {
        const a = arcs[i], b = arcs[j];
        const overlap = Math.abs(a.cx - b.cx) < (a.rx + b.rx) && Math.abs(a.cy - b.cy) < (a.ry + b.ry);
        ok(!overlap, `${a.id} and ${b.id} do not overlap`);
    }
}
for (let i = 0; i < chart.places.length; ++i) {
    for (let j = 0; j < chart.places.length; ++j) {
        if (i === j) continue;
        const small = chart.places[i], big = chart.places[j];
        if (small.rx >= big.rx) continue;
        const buried = Math.abs(small.cx - big.cx) < big.rx * 0.6 && Math.abs(small.cy - big.cy) < big.ry * 0.6;
        ok(!buried, `${small.id} is not buried inside ${big.id}`);
    }
}
const rm = chart.reverseMountain;
for (const p of chart.places) {
    const overlap = Math.abs(p.cx - rm.cx) < (p.rx + rm.w / 2) && Math.abs(p.cy - rm.cy) < (p.ry + rm.h / 2);
    ok(!overlap, `Reverse Mountain does not sit on ${p.id}`);
}

// --- the voyage is derived from the anchors the badges use ----------------
const pts = chart.routePoints(1000, 520);
eq(pts.map(p => p.id), chart.route, 'every leg of the route resolves to a place');
for (const p of pts) {
    const src = p.id === 'reverse' ? rm : chart.place(p.id);
    eq([p.x, p.y], [src.cx * 1000, src.cy * 520], `${p.id} route point is its own anchor`);
}

// --- globe anchors exist now so the globe is not a retrofit ---------------
const b = chart.globeBounds;
for (const p of chart.places) {
    const g = chart.globeAnchor(p.id);
    ok(g && g.lat <= b.latNorth && g.lat >= b.latSouth, `${p.id} globe latitude is inside East Blue`, g);
    ok(g && g.lon >= b.lonWest && g.lon <= b.lonEast, `${p.id} globe longitude is inside East Blue`, g);
}
eq(chart.globeAnchor('reverse'), { lat: 0, lon: 0 },
   'the gate sits where the Grand Line crosses the Red Line');
ok(chart.globeAnchor('dawn').lon > chart.globeAnchor('conomi').lon,
   'east on the chart is east on the globe');

eq(chart.taglineForArc('syrup'), 'Usopp lies, make him captain', 'tagline joins on arc id');
eq(chart.taglineForArc('nope'), '', 'an unknown arc has no tagline rather than a broken one');

console.log(failed ? `\n${failed} failed` : '\nall passed');
process.exit(failed ? 1 : 0);
