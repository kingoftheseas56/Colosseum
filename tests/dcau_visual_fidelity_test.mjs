import fs from 'fs';
import crypto from 'crypto';

let failed = 0;
const ok = m => console.log('  ok   ' + m);
const need = (text, needle, message) => {
  if (text.includes(needle)) ok(message);
  else { console.log(`  FAIL ${message}: missing ${needle}`); failed++; }
};
const reject = (text, needle, message) => {
  if (!text.includes(needle)) ok(message);
  else { console.log(`  FAIL ${message}: found ${needle}`); failed++; }
};

const metroPath = 'assets/universes/dcau/environments/metropolis-v11.svg';
const futurePath = 'assets/universes/dcau/environments/future-v11.svg';
const gothamPath = 'assets/universes/dcau/environments/gotham-v11-bg.png';
const justicePath = 'assets/universes/dcau/environments/justice-v11-bg.png';
const metroPortalPath = 'assets/universes/dcau/portals/metropolis.jpg';
const metroPortalHash = crypto.createHash('sha256').update(fs.readFileSync(metroPortalPath)).digest('hex');
if (metroPortalHash === '21d5be9c50c2fb4adc805c032fa7ac10c40ec5c60785d227330a0554e891b458') ok('Metropolis portal retains the v11 oracle-derived art');
else { console.log('  FAIL Metropolis portal asset diverged from the v11 oracle-derived art'); failed++; }
const metroQml = fs.readFileSync('qml/DCAUEnvironmentMetropolis.qml', 'utf8');
const futureQml = fs.readFileSync('qml/DCAUEnvironmentFutureGotham.qml', 'utf8');
const gothamQml = fs.readFileSync('qml/DCAUEnvironmentGotham.qml', 'utf8');
const spaceQml = fs.readFileSync('qml/DCAUEnvironmentSpace.qml', 'utf8');
const worldQml = fs.readFileSync('qml/DCAUWorldPage.qml', 'utf8');
const metro = fs.readFileSync(metroPath, 'utf8');
const future = fs.readFileSync(futurePath, 'utf8');
need(metroQml, 'metropolis-v11.svg', 'Metropolis QML consumes the locked v11 scene');
need(futureQml, 'future-v11.svg', 'Future Gotham QML consumes the locked v11 scene');
need(gothamQml, 'gotham-v11-bg.png', 'Gotham QML consumes the locked v11 scene');
need(spaceQml, 'justice-v11-bg.png', 'Watchtower QML consumes the locked v11 scene');
reject(metroQml, 'Repeater {', 'Metropolis is not reduced to repeated building rectangles');
reject(futureQml, 'Repeater {', 'Future Gotham is not reduced to repeated building rectangles');
reject(gothamQml, 'Repeater {', 'Gotham is not reduced to repeated building rectangles');
reject(spaceQml, 'Repeater {', 'Watchtower is not reduced to a synthetic star repeater');

need(metro, 'M448 900V592h112v-90', 'Metropolis keeps the Daily Planet tower geometry');
need(metro, 'M175 530h206v16H175z', 'Metropolis keeps the v11 skybridge');
need(metro, 'opacity=".34"', 'Metropolis keeps the far skyline depth');
need(metro, 'opacity=".60"', 'Metropolis keeps the mid skyline depth');
need(metro, 'filter="url(#blur8)"', 'Metropolis keeps the soft cloud bank');
need(metro, 'fill="url(#glow1)"', 'Metropolis keeps the atmospheric sky glow');

need(future, 'M396 900 L734 548 H866 L1206 900 Z', 'Future Gotham keeps the central canyon');
need(future, 'M596 516h415l96 44-96 22H596l-92-22z', 'Future Gotham keeps the suspended rail structure');
need(future, 'class="pulseSign"', 'Future Gotham keeps neon signage');
need(future, 'points="946,144 974,144 1030,666 890,666"', 'Future Gotham keeps the main scan beam');
need(future, 'fill="url(#moon)"', 'Future Gotham keeps the moon treatment');
need(future, 'fill="url(#vig)"', 'Future Gotham keeps the vignette');

for (const p of [gothamPath, justicePath]) {
  if (fs.existsSync(p) && fs.statSync(p).size > 100000) ok(`${p} retains full-scene raster detail`);
  else { console.log(`  FAIL ${p} missing or collapsed`); failed++; }
}
need(worldQml, 'y: 128', 'DCAU board starts at the v11 128px top inset without an extra spacer gap');
need(worldQml, 'contentHeight: 128 + content.implicitHeight + 50', 'DCAU board keeps v11 50px bottom padding');
need(worldQml, 'height: 278', 'Tankoban rail keeps v11 278px strip height');
need(worldQml, 'x: 6', 'DCAU rails keep the v11 6px leading inset');
need(worldQml, 'contentWidth: tankobanRow.width + 32', 'Tankoban rail keeps 6px leading + 26px trailing space');
need(worldQml, 'contentWidth: theatreRow.width + 32', 'Theatre rail keeps 6px leading + 26px trailing space');
need(worldQml, 'navigable: false', 'DCAU shelf headers omit non-v11 chevrons');
reject(worldQml, 'Item { width: 1; height: 128 }', 'DCAU board does not manufacture top padding as a positioned child');
reject(worldQml, 'Item { width: 1; height: 24 }', 'DCAU board does not manufacture bottom padding as a positioned child');

const tankCardPath = 'qml/DCAUTankCard.qml';
const theatreCardPath = 'qml/DCAUTheatreCard.qml';
if (fs.existsSync(tankCardPath)) {
  const q = fs.readFileSync(tankCardPath, 'utf8');
  need(q, 'radius: 14', 'DCAU Tankoban keeps the v11 14px art crop');
  need(q, 'font.pixelSize: 14', 'DCAU Tankoban keeps the v11 14px caption');
  need(q, 'lineHeight: 16', 'DCAU Tankoban keeps the v11 16px caption leading');
} else { console.log('  FAIL DCAUTankCard.qml missing'); failed++; }
if (fs.existsSync(theatreCardPath)) {
  const q = fs.readFileSync(theatreCardPath, 'utf8');
  need(q, 'radius: 16', 'DCAU Theatre keeps the v11 16px art crop');
  need(q, '? -8 : 0', 'DCAU Theatre keeps the v11 8px hover lift');
  need(q, 'font.pixelSize: 14', 'DCAU Theatre keeps the v11 14px title');
  need(q, 'anchors.topMargin: 12', 'DCAU Theatre keeps the v11 12px title gap');
} else { console.log('  FAIL DCAUTheatreCard.qml missing'); failed++; }
need(worldQml, 'DCAUTankCard', 'DCAU world uses its locked v11 Tankoban card grammar');
need(worldQml, 'DCAUTheatreCard', 'DCAU world uses its locked v11 Theatre card grammar');
reject(worldQml, 'PortraitTile {', 'DCAU world no longer inherits generic Tankoban gallery radii');
reject(worldQml, 'CataloguePosterCard {', 'DCAU world no longer inherits generic Theatre gallery radii');

if (metro.length > 7000) ok('Metropolis scene retains detailed vector density');
else { console.log('  FAIL Metropolis scene vector density collapsed'); failed++; }
if (future.length > 8000) ok('Future Gotham scene retains detailed vector density');
else { console.log('  FAIL Future Gotham scene vector density collapsed'); failed++; }


const universeQml = fs.readFileSync('qml/DCAUUniversePage.qml', 'utf8');
const portalQml = fs.readFileSync('qml/DCAUWorldPortal.qml', 'utf8');
const dataQml = fs.readFileSync('qml/DCAUUniverseData.js', 'utf8');
need(universeQml, 'DCAULandingBackdrop', 'DCAU landing restores the v11 background wash');
need(universeQml, 'y: 30', 'DCAU Continue begins at the v11 30px top inset');
need(universeQml, 'continueCol.y + continueCol.implicitHeight + 18 : 30', 'portal carousel begins immediately after v11 Continue margin');
need(universeQml, 'parent.height - y - 24', 'portal carousel keeps the v11 24px bottom inset');
need(universeQml, 'spacing: 20', 'DCAU Continue rail keeps v11 20px gap');
need(universeQml, 'contentWidth: continueRow.width + 26', 'DCAU Continue rail keeps v11 26px trailing space');
need(universeQml, 'DCAUPortalArrow', 'DCAU landing restores v11 carousel arrows');
need(universeQml, 'portalStage.width * 0.24', 'desktop portals keep v11 24vw width contract');
need(universeQml, 'portalStage.width * 0.48', 'compact portals keep v11 48vw width contract');
need(portalQml, 'property string ordinal', 'DCAU portals restore the v11 01-04 ordinal');
need(portalQml, 'font.pixelSize: 9', 'DCAU portal ordinal keeps v11 9px type');
need(portalQml, 'font.letterSpacing: 1.62', 'DCAU portal ordinal keeps v11 .18em tracking');
need(portalQml, 'anchors.leftMargin: 22', 'DCAU portal label keeps v11 22px inset');
need(portalQml, 'anchors.bottomMargin: 18', 'DCAU portal label keeps v11 18px bottom inset');
need(portalQml, 'scale: hover.hovered ? 1.018 : 1.0', 'DCAU portal art keeps v11 hover zoom');
reject(portalQml, 'onHoveredChanged: if (hovered) root.selectionRequested()', 'hover does not change the v11 active portal');
need(dataQml, 'title: "Justice League"', 'third landing portal keeps the v11 Justice League label');

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
