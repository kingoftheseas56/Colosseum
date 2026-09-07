import fs from 'fs';

let failed = 0;
const ok = message => console.log('  ok   ' + message);
const bad = message => { console.log('  FAIL ' + message); failed++; };
const eq = (actual, expected, message) => JSON.stringify(actual) === JSON.stringify(expected)
    ? ok(`${message} -> ${JSON.stringify(actual)}`)
    : bad(`${message} -> ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}`);
const contains = (source, needle, message) => eq(source.includes(needle), true, message);

console.log('One Piece native East Blue atlas contract');

const dataPath = 'qml/OnePieceEastBlueAtlasData.js';
eq(fs.existsSync(dataPath), true, 'atlas data module exists');
if (fs.existsSync(dataPath)) {
    const source = fs.readFileSync(dataPath, 'utf8').replace(/^\.pragma library\s*$/m, '');
    const module = {};
    new Function('module', `${source}\nmodule.canonMarkers=canonMarkers;module.nonCanonEntries=nonCanonEntries;module.canonMarker=canonMarker;module.nonCanonEntry=nonCanonEntry;`)(module);
    eq(module.canonMarkers.map(marker => marker.id),
       ['romance', 'orange', 'syrup', 'baratie', 'arlong', 'loguetown'],
       'canon marker order');
    eq(module.nonCanonEntries.map(entry => entry.id),
       ['ganzack', 'one-piece-movie', 'oceans-navel', 'jangos-dance-carnival', 'clockwork-island'],
       'non-canon entry order');
    eq(module.canonMarkers.every(marker => marker.id && marker.badge && marker.poster && marker.x >= 0 && marker.x <= 1 && marker.y >= 0 && marker.y <= 1), true,
       'canon markers have normalized positions and assets');
    for (const marker of module.canonMarkers) {
        eq(marker.poster !== marker.badge, true,
           `${marker.id} canon poster is distinct from its circular badge`);
        eq(fs.existsSync(marker.poster.replace(/^\.\.\//, '')), true,
           `${marker.id} canon poster is bundled locally`);
    }
    eq(module.canonMarkers.map(marker => [marker.x, marker.y]),
       [[0.88869, 0.35208], [0.56726, 0.31667], [0.4125, 0.38646], [0.63988, 0.62917], [0.33095, 0.34271], [0.2375, 0.69167]],
       'canon markers follow approved SVG transforms');
    eq(module.canonMarkers.every(marker => ['romance', 'orange', 'syrup', 'baratie', 'arlong', 'loguetown'].includes(marker.id)), true,
       'canon marker ids match East Blue arcs');
    eq(module.nonCanonEntries.map(entry => entry.entry && entry.entry.id),
       ['tt1012788', 'tt0814243', 'tt0975705', null, 'tt0832449'],
       'non-canon uses four known Cinemeta identities and no Jango identity');
    eq(module.nonCanonEntries.find(entry => entry.id === 'jangos-dance-carnival').entry, null,
       'Jango remains fail-closed');
    for (const id of ['romance', 'missing']) eq(module.canonMarker(id).id, id === 'missing' ? 'romance' : id, `canonMarker(${id})`);
    eq(module.nonCanonEntry('clockwork-island').entry.id, 'tt0832449', 'nonCanonEntry lookup');
}

const atlasPath = 'qml/OnePieceEastBlueAtlas.qml';
eq(fs.existsSync(atlasPath), true, 'native atlas component exists');
if (fs.existsSync(atlasPath)) {
    const source = fs.readFileSync(atlasPath, 'utf8');
    for (const needle of [
        'property bool reducedMotion',
        'readonly property bool transientOpen',
        'readonly property real badgeDiameter',
        'readonly property real badgeHitDiameter',
        'property real shellChromeInset',
        'shellChromeReservedLeft',
        'function requestEscape()',
        'signal arcRequested(var arc)',
        'signal mediaRequested(var entry)',
        'signal paradiseRequested()',
        'FocusScope',
        'Image.PreserveAspectFit',
        'FontLoader',
        'import QtQuick.Controls.Basic',
        'readonly property bool fontsReady',
        'readonly property bool captureReady',
        'objectName: "eastBlueAtlasDisplayFont"',
        'objectName: "eastBlueAtlasBodyFont"',
        'FontLoader.Ready',
        'contentItem: Text',
        'Fraunces-Regular.ttf',
        'HoverHandler',
        'Timer {',
        'interval: 160',
        'TapHandler',
        'acceptedButtons: Qt.NoButton',
        'focusPolicy: Qt.TabFocus',
        'onActiveFocusChanged',
        'previewMarkerFocused',
        'closePreviewSoon()',
        'status === Image.Error',
        'objectName: "eastBlueIndexButton"',
        'objectName: "eastBlueNonCanonAction-"',
        'Keys.onReturnPressed',
        'Keys.onEnterPressed',
        'OPEN ARC',
        'NON-CANON MATERIAL',
        'Flickable {',
        'clip: true',
        'BACK TO INDEX',
        'paradiseRequested()'
    ]) contains(source, needle, `atlas contains ${needle}`);
    for (const needle of [
        'function updatePreviewGeometry()',
        'marker.mapToItem(root',
        'previewX',
        'previewY',
        'onContentXChanged',
        'onContentYChanged',
        'posterFallback',
        'AtlasData.canonMarker',
        'Image.PreserveAspectCrop',
        'background: Rectangle',
        'indexButton.pressed',
        'indexButton.hovered',
        'eastBlueArcPreviewPoster'
    ]) contains(source, needle, `atlas implements ${needle}`);
    for (const buttonFont of [
        'paradiseButton', 'zoomOutButton', 'zoomResetButton', 'zoomInButton',
        'canonRowButton', 'nonCanonToggleButton', 'nonCanonActionButton',
        'backToIndexRowButton', 'backToIndexButton', 'openArcButton', 'closePreviewButton'
    ]) {
        eq(source.includes(`font: ${buttonFont}.font`), false,
           `${buttonFont} content text does not inherit Button.font`);
    }
    eq((source.match(/font\.family:\s*atlasBodyFont\.name/g) || []).length >= 12, true,
       'native visible labels bind content text to the ready body font');
    eq(source.includes('anchors.rightMargin: root.shellChromeInset'), true, 'Index reserves the shell-control column');
    eq(source.includes('contentItem: Row'), true, 'Index uses a native drawn list icon content item');
    eq(source.includes('Accessible.name: "INDEX"'), true, 'Index exposes an explicit accessible name');
    eq(/objectName: "eastBlueIndexButton"[\s\S]*?height:\s*4[4-9]/.test(source), true, 'Index hit target is at least 44 px high');
    eq(/objectName: "eastBlueToParadise"[\s\S]*?height:\s*4[4-9]/.test(source), true, 'TO PARADISE hit target is at least 44 px high');
    eq(/Button \{[\s\S]*?text:\s*"−";\s*width:\s*4[4-9];\s*height:\s*4[4-9]/.test(source), true, 'zoom-out hit target is at least 44 px');
    eq(/Button \{[\s\S]*?text:\s*"100%";\s*width:\s*5[0-9];\s*height:\s*4[4-9]/.test(source), true, 'zoom reset hit target is at least 44 px');
    eq(/Button \{[\s\S]*?text:\s*"\+";\s*width:\s*4[4-9];\s*height:\s*4[4-9]/.test(source), true, 'zoom-in hit target is at least 44 px');
    eq(/id: openArcButton[\s\S]*?height:\s*44/.test(source), true, 'OPEN ARC hit target is at least 44 px high');
    eq(/id: closePreviewButton[\s\S]*?height:\s*44/.test(source), true, 'CLOSE hit target is at least 44 px high');
    eq(/id: nonCanonActionButton[\s\S]*?height:\s*44/.test(source), true, 'non-canon action hit target is at least 44 px high');
    eq(/id: backToIndexRowButton[\s\S]*?height:\s*44/.test(source), true, 'non-canon row back hit target is at least 44 px high');
    eq(/id: backToIndexButton[\s\S]*?height:\s*44/.test(source), true, 'non-canon panel back hit target is at least 44 px high');
    eq(source.includes('text: "☷  INDEX"'), false, 'Index does not use a Unicode glyph');
    eq(source.includes('context: Qt.WindowShortcut'), false, 'atlas has no parallel Window Escape shortcut');
    eq(source.includes('return true'), true, 'atlas Escape seam reports handled transient');
    eq(source.includes('return false'), true, 'atlas Escape seam fails closed without transient');
    eq(/Timer\s*\{[\s\S]*?interval:\s*1[0-9]{2}/.test(source), true, 'atlas has delayed close timer');
    eq(source.includes('onTapped:'), false, 'badge pointer tap does not navigate');
    eq(source.includes('onClicked: root.arcRequested'), false, 'badge/index pointer action does not emit arcRequested');
    eq(source.includes('root.arcRequested(arc)'), true, 'only OPEN ARC emits selected arc');
    eq(source.includes('source: "../assets/universes/one-piece/east-blue/east-blue-atlas.png"'), true,
       'atlas uses the headless-safe raster plate');
    eq(source.includes('width: root.badgeDiameter'), true, 'badge art uses the scaled visible diameter');
    eq(source.includes('width: root.badgeHitDiameter'), true, 'badge hit target stays independently accessible');
    eq(/fitScale[\s\S]*?Math\.min\(mapViewport\.width\s*\/\s*1680/.test(source), false,
       'atlas fit scale does not shrink to the shortest constrained dimension');
    eq(/y:\s*Math\.min\(0\.8938\s*\*\s*mapStage\.height[\s\S]*?mapViewport\.height\s*-\s*mapStage\.y\s*-\s*height/.test(source), true,
       'TO PARADISE clamps to the visible viewport at short heights');
    eq(source.includes('captureReady'), true, 'capture waits for bundled fonts and plate readiness');
    const normalizedSource = source.replace(/\r\n/g, '\n');
    const previewStart = normalizedSource.indexOf('id: preview\n');
    const previewEnd = normalizedSource.indexOf('\n        Keys.onEscapePressed: root.requestEscape()', previewStart);
    const previewBlock = normalizedSource.slice(previewStart, previewEnd < 0 ? normalizedSource.length : previewEnd);
    eq(previewBlock.includes('anchors.left'), false,
       'preview is not hard-anchored to the bottom-left');
    eq(previewBlock.includes('anchors.bottom'), false,
       'preview does not use fixed bottom anchoring');
}

const atlasAsset = 'assets/universes/one-piece/east-blue/east-blue-atlas.svg';
eq(fs.existsSync(atlasAsset) && fs.statSync(atlasAsset).size > 0, true, 'atlas plate exists');
if (fs.existsSync(atlasAsset)) {
    const plate = fs.readFileSync(atlasAsset, 'utf8').toUpperCase();
    for (const forbidden of ['CALM BELT', 'GRAND LINE', 'TO PARADISE', 'MARGINALIA', 'LEGEND', 'ARC-STRIP', 'ISLAND-HIT'])
        eq(plate.includes(forbidden), false, `plate removes ${forbidden.toLowerCase()}`);
}
const atlasPlate = 'assets/universes/one-piece/east-blue/east-blue-atlas.png';
eq(fs.existsSync(atlasPlate) && fs.statSync(atlasPlate).size > 0, true, 'high-resolution raster atlas plate exists');
if (fs.existsSync(atlasPlate)) {
    const png = fs.readFileSync(atlasPlate);
    eq(png.readUInt32BE(16) >= 3360 && png.readUInt32BE(20) >= 1920, true,
       'raster atlas plate is at least 3360x1920');
}
for (const poster of [
    '01-ganzack.png', '02-one-piece-movie.png', '03-oceans-navel.png',
    '04-jangos-dance-carnival.png', '05-clockwork-island.png'
]) eq(fs.existsSync(`assets/universes/one-piece/east-blue/noncanon/${poster}`), true, `non-canon poster ${poster} exists`);

const pagePath = 'qml/OnePieceUniversePage.qml';
eq(fs.existsSync(pagePath), true, 'universe page exists');
if (fs.existsSync(pagePath)) {
    const source = fs.readFileSync(pagePath, 'utf8');
    contains(source, 'OnePieceEastBlueAtlas {', 'universe page hosts native atlas');
    contains(source, 'onArcRequested: function(arc)', 'universe page forwards canon signal');
    contains(source, 'onMediaRequested: function(entry)', 'universe page forwards media signal');
    eq(source.includes('OnePieceWorld3D {'), false, 'universe page no longer instantiates 3D globe');
    eq(source.includes('root.arcRequested(arc)'), true, 'universe page forwards arc unchanged');
    eq(source.includes('root.mediaRequested(entry)'), true, 'universe page forwards media unchanged');
}

const harnessPath = 'tests/qml/tst_one_piece_east_blue_native_atlas.qml';
eq(fs.existsSync(harnessPath), true,
   'native atlas harness exists under the repo Quick Test discovery directory');
if (fs.existsSync(harnessPath)) {
    const source = fs.readFileSync(harnessPath, 'utf8').replace(/\r\n/g, '\n');
    contains(source, 'standaloneRun', 'harness detects direct qml invocation');
    contains(source, 'Qt.exit(0)', 'harness exits cleanly on direct invocation');
    contains(source, 'HARNESS FAIL: timed out', 'harness has a direct-run timeout guard');
    contains(source, 'EAST_BLUE_ATLAS_HARNESS_PASS', 'harness writes an externally observable pass marker');
    contains(source, 'EAST_BLUE_ATLAS_HARNESS_FAIL', 'harness writes an externally observable failure marker');
    contains(source, 'captureReady', 'harness waits for native atlas capture readiness');
    contains(source, 'east-blue-atlas-preview-arlong-900x600.png',
             'fresh 900x600 evidence keeps a preview open');
    eq(source.includes('result.saveToFile(standaloneFailEvidencePath)'), false,
       'incomplete direct runs never create failure evidence');
    eq(source.includes('standaloneFinished = true\n        standaloneTimeout.stop()\n        atlas.grabToImage'), true,
       'direct pass marks finished before asynchronous evidence capture');
}

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
