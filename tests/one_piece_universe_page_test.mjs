import fs from 'fs';

let failed = 0;
const ok = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq = (a, b, m) => JSON.stringify(a) === JSON.stringify(b)
    ? ok(`${m} -> ${JSON.stringify(a)}`)
    : bad(`${m} -> ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);

console.log('One Piece East Blue arc contract');
const dataPath = 'qml/OnePieceEastBlueData.js';
if (!fs.existsSync(dataPath)) {
    bad('OnePieceEastBlueData.js exists');
} else {
    let src = fs.readFileSync(dataPath, 'utf8').replace(/^\.pragma library\s*$/m, '');
    const mod = {};
    new Function('module', src + '\nmodule.arcs=arcs;module.arc=arc;')(mod);
    eq(mod.arcs.map(a => a.id),
       ['romance','orange','syrup','baratie','arlong','loguetown'],
       'arc order');
    eq(mod.arcs.map(a => a.anime),
       ['1-3','4-8','9-18','19-30','31-44','45, 48-53'],
       'anime ranges');
    eq(mod.arcs.map(a => a.chapters),
       ['1-7','8-21','22-41','42-68','69-95','96-100'],
       'chapter ranges');
    eq(mod.arcs.map(a => a.volumes),
       ['1','1-3','3-5','5-8','8-11','11-12'],
       'overlapping volume membership');
    eq(mod.arcs.map(a => a.episodeCount),
       [3,5,10,12,14,7],
       'episode counts');
    eq(mod.arcs.map(a => a.chapterCount),
       [7,14,20,27,27,5],
       'chapter counts');
    const focusValid = mod.arcs.every(a =>
        a.focusX >= 0 && a.focusX <= 1 && a.focusY >= 0 && a.focusY <= 1);
    eq(focusValid, true, 'focus coordinates are normalized');
    eq(mod.arc('arlong').title, 'Arlong Park', 'arc lookup');
    eq(mod.arc('missing').id, 'romance', 'unknown arc falls back to first arc');
    eq(mod.arcs.some(a => Object.prototype.hasOwnProperty.call(a, 'poseAngle')), false, 'atlas data has no Log Pose-only angle state');
    eq(mod.arcs.some(a => Object.prototype.hasOwnProperty.call(a, 'blurb')), false, 'atlas data carries no legacy AI blurb fields');
    eq(mod.arcs.every(a => typeof a.summary === 'string' && a.summary.length > 20), true, 'every arc has a concise source-grounded summary');
    eq(mod.arc('romance').markerY <= 0.31, true, 'Romance Dawn clears the bottom-left shell controls');
    eq(mod.arc('romance').geoY <= 0.44, true, 'Dawn Island label stays inside the shell-safe atlas zone');
    eq(mod.arc('loguetown').markerX <= 0.86, true, 'Loguetown reserves a Reverse Mountain gutter');
    eq(mod.arcs.map(a => a.liveActionSeason), [1,1,1,1,1,2], 'live-action seasons map to arcs');
    eq(mod.arcs.map(a => a.liveActionEpisodes), ['1','2','3-4','5-6','7-8','1'], 'live-action episodes map to arcs');
}

console.log('\nEast Blue assets');
const assetFiles = [
    'assets/universes/one-piece/east-blue-relief.png',
    'assets/universes/one-piece/east-blue-markers/romance.png',
    'assets/universes/one-piece/east-blue-markers/orange.png',
    'assets/universes/one-piece/east-blue-markers/syrup.png',
    'assets/universes/one-piece/east-blue-markers/baratie.png',
    'assets/universes/one-piece/east-blue-markers/arlong.png',
    'assets/universes/one-piece/east-blue-markers/loguetown.png'
];
for (const file of assetFiles)
    eq(fs.existsSync(file) && fs.statSync(file).size > 0, true, `${file} exists`);
console.log('\nArc marker source contract');
const markerPath = 'qml/OnePieceArcMarker.qml';
eq(fs.existsSync(markerPath), true, 'OnePieceArcMarker.qml exists');
if (fs.existsSync(markerPath)) {
    const markerSrc = fs.readFileSync(markerPath, 'utf8');
    for (const needle of [
        'activeFocusOnTab: true',
        'HoverHandler',
        'Keys.onReturnPressed',
        'Keys.onEnterPressed',
        'property bool reducedMotion'
    ]) eq(markerSrc.includes(needle), true, `marker contains ${needle}`);
    eq(markerSrc.includes('border.width: root.activeFocus ? 1 : 0'), false, 'marker has no rectangular focus overlay');
    eq(markerSrc.includes('id: breathRing'), false, 'marker has no oversized selection halo');
}
console.log('\nSea gate source contract');
const gatePath = 'qml/OnePieceSeaGate.qml';
eq(fs.existsSync(gatePath), true, 'OnePieceSeaGate.qml exists');
if (fs.existsSync(gatePath)) {
    const gateSrc = fs.readFileSync(gatePath, 'utf8');
    for (const needle of ['REVERSE MOUNTAIN','ENTER GRAND LINE','signal activated()','activeFocusOnTab: true'])
        eq(gateSrc.includes(needle), true, `sea gate contains ${needle}`);
    eq(gateSrc.includes('scale: root.hot ?'), false, 'sea gate keeps a fixed footprint on hover/focus');
    eq(gateSrc.includes('border.width: root.activeFocus ? 1 : 0'), false, 'sea gate has no persistent focus overlay');
    eq(gateSrc.includes('root.focus = false'), true, 'sea gate clears focus before page turn');
}
console.log('\nArc dock source contract');
const dockPath = 'qml/OnePieceArcDock.qml';
eq(fs.existsSync(dockPath), true, 'OnePieceArcDock.qml exists');
if (fs.existsSync(dockPath)) {
    const dockSrc = fs.readFileSync(dockPath, 'utf8');
    for (const needle of [
        'required property Item backdrop',
        'signal animeRequested()',
        'signal onePaceRequested()',
        'signal mangaRequested(bool colorEdition)',
        'signal liveActionRequested()',
        'signal specialRequested(string id)',
        'Glass {',
        'backdrop: root.backdrop'
    ]) eq(dockSrc.includes(needle), true, `dock contains ${needle}`);
    eq(dockSrc.includes('root.arc.blurb'), false, 'dock renders no legacy AI blurb');
    eq(dockSrc.includes('root.arc.summary'), true, 'dock renders source-grounded arc summary');
    eq(dockSrc.includes(' SELECTED'), false, 'dock has no redundant selected-state caption');
    eq(dockSrc.includes('EXPLORE THIS ARC'), true, 'dock uses media carousel heading');
    eq(dockSrc.includes('FeaturedCarousel {'), true, 'dock reuses the exact shared world carousel');
    eq(dockSrc.includes('FOUR ROUTES INTO THIS ARC'), false, 'dock drops obsolete four-route grid heading');
    eq(dockSrc.includes('tt11757066'), true, 'carousel includes Episode of East Blue');
    eq(dockSrc.includes('tt2598466'), true, 'carousel can include Episode of Nami for Arlong Park');
    eq(dockSrc.includes('tt11737520'), true, 'carousel includes live-action adaptation');
    eq(dockSrc.includes('interval: 5000'), true, 'carousel auto-advances every five seconds');
    eq(dockSrc.includes('repeat: true'), true, 'carousel auto-advance repeats');
    eq(dockSrc.includes('onArcChanged:'), true, 'arc changes reset the shared carousel');
    eq(dockSrc.includes('text: \"‹\"'), false, 'dock has no custom previous arrow');
    eq(dockSrc.includes('text: \"›\"'), false, 'dock has no custom next arrow');
    eq(dockSrc.includes('property int activeIndex'), false, 'dock has no duplicate carousel index model');
    eq(dockSrc.includes('visible: Math.abs(wrapped) <= 1'), false, 'dock has no custom wrapped-slide renderer');
    eq(dockSrc.includes('mediaHero'), false, 'dock has no custom hero delegate');
    eq(dockSrc.includes('ListView {'), false, 'dock avoids ListView auto-advance hang');
    eq(dockSrc.includes('detail: \"Original anime · Episodes '), false, 'carousel does not repeat anime episode ranges');
    eq(dockSrc.includes('detail: \"Vol. '), false, 'carousel does not repeat manga ranges');
    eq(dockSrc.includes('kicker: \"\"'), true, 'One Piece shared carousel hides redundant kicker copy');
    eq(dockSrc.includes('secondaryLabel: \"\"'), true, 'One Piece shared carousel requests one action only');
    for (const title of ['Anime','One Pace','Manga','Colored Manga','Live Action'])
        eq(dockSrc.includes(`title: \"${title}\"`), true, `carousel uses universe-context title ${title}`);
    for (const oldTitle of ['One Piece Manga','One Piece Color'])
        eq(dockSrc.includes(`title: \"${oldTitle}\"`), false, `carousel drops redundant title ${oldTitle}`);
}
const featuredCarouselSrc = fs.readFileSync('qml/FeaturedCarousel.qml', 'utf8');
eq(featuredCarouselSrc.includes('SwipeView {'), true, 'shared world carousel remains SwipeView-backed');
const carouselSlideSrc = fs.readFileSync('qml/CarouselSlide.qml', 'utf8');
eq(carouselSlideSrc.includes('visible: slideRoot.secondaryLabel.length > 0'), true, 'shared slide hides secondary action when label is blank');
console.log('\nEast Blue map source contract');
const mapPath = 'qml/OnePieceEastBlueMap.qml';
eq(fs.existsSync(mapPath), true, 'OnePieceEastBlueMap.qml exists');
if (fs.existsSync(mapPath)) {
    const mapSrc = fs.readFileSync(mapPath, 'utf8');
    for (const needle of [
        'import QtQuick.Shapes',
        'east-blue-relief.png',
        'ShapePath.DashLine',
        'dashPattern',
        'dashOffset',
        'NumberAnimation',
        'property bool reducedMotion',
        'Scale',
        'OnePieceArcMarker',
        'OnePieceSeaGate',
        'signal paradiseRequested()'
    ]) eq(mapSrc.includes(needle), true, `map contains ${needle}`);
    eq(mapSrc.includes('id: logPose'), false, 'map has no Log Pose component');
    eq(mapSrc.includes('LOG POSE'), false, 'map has no Log Pose label');
    eq(mapSrc.includes('OnePieceArcDock'), false, 'map owns no media dock overlay');
}

console.log('\nOne Piece page source contract');
const pagePath = 'qml/OnePieceUniversePage.qml';
eq(fs.existsSync(pagePath), true, 'OnePieceUniversePage.qml exists');
if (fs.existsSync(pagePath)) {
    const pageSrc = fs.readFileSync(pagePath, 'utf8');
    for (const needle of [
        'import "UniverseExtApi.js" as UniverseApi',
        'property string extensionId',
        'property string universeName',
        'property bool reducedMotion',
        'UniverseApi.load(root.extensionId',
        'OnePieceEastBlueMap',
        'signal watchRequested(var payload)',
        'signal seriesRequested(var entry)',
        'signal onePaceRequested(var arc)',
        'signal paradiseRequested()',
        'Progress.recent("", 100)',
        'ContinueTile',
        'signal continueResumeRequested(var entry)',
        'signal continueDetailRequested(var entry)',
        'onParadiseRequested: root.paradiseRequested()',
        'onLiveActionRequested:',
        'onSpecialRequested:' ,
        'OnePieceArcDock',
        'OnePieceArcCatalogue'
    ]) eq(pageSrc.includes(needle), true, `page contains ${needle}`);
    eq(pageSrc.includes('arc.liveActionSeason'), true, 'live-action route uses mapped season');
    eq(pageSrc.includes('arc.liveActionEpisodes'), true, 'live-action route uses mapped episode range');
    eq(pageSrc.includes('height: 520'), true, 'arc media widget uses taller presentation');
    eq(pageSrc.includes('function openArcCatalogue'), true, 'format cards open the One Piece arc catalogue');
    eq(pageSrc.includes('onEpisodeRequested: function(entry) { root.watchRequested(entry) }'), true, 'arc catalogue forwards provider episode selection');
    eq(pageSrc.includes('onMangaVolumeRequested:'), true, 'arc catalogue forwards manga-volume selection');
    eq(pageSrc.includes('property var installedExtensions: []'), true, 'One Piece page accepts installed extensions');
    eq(pageSrc.includes('onOnePaceRequested: root.openArcCatalogue("pace")'), true, 'One Pace carousel opens the arc catalogue');
    eq(pageSrc.includes('installedExtensions: root.installedExtensions'), true, 'arc catalogue receives installed extensions');
}


console.log('\nOne Piece arc catalogue contract');
const catalogueApiPath = 'qml/OnePieceCatalogApi.js';
eq(fs.existsSync(catalogueApiPath), true, 'OnePieceCatalogApi.js exists');
if (fs.existsSync(catalogueApiPath)) {
    const apiSrc = fs.readFileSync(catalogueApiPath, 'utf8');
    for (const needle of [
        'kitsu:12',
        'https://kitsu.io/api/edge/anime/12/episodes',
        'tt11737520',
        'function numbersFromSpec',
        'function loadAnimeEpisodes',
        'function loadLiveActionEpisodes',
        'function volumeNumbers'
    ]) eq(apiSrc.includes(needle), true, `arc catalogue API contains ${needle}`);
}
const cataloguePagePath = 'qml/OnePieceArcCatalogue.qml';
eq(fs.existsSync(cataloguePagePath), true, 'OnePieceArcCatalogue.qml exists');
if (fs.existsSync(cataloguePagePath)) {
    const catalogueSrc = fs.readFileSync(cataloguePagePath, 'utf8');
    for (const needle of [
        'signal episodeRequested(var entry)',
        'signal mangaVolumeRequested(bool colorEdition, string volumeNumber)',
        'CatalogApi.loadAnimeEpisodes(root.arc',
        'CatalogApi.loadOnePaceEpisodes(root.installedExtensions, root.arc',
        'CatalogApi.loadLiveActionEpisodes(root.arc',
        'tankobanCatalogRef.volumes',
        'title: "Anime"',
        'title: "One Pace"',
        'title: "Manga"',
        'title: "Colored Manga"',
        'title: "Live Action"',
        'sub: root.animeLoading ? "Anime Kitsu · loading"',
        'sub: root.onePaceLoading ? "One Pace Addon · loading"',
        'root.countLabel(root.volumeItems.length, "volume") + " · Tankoban"',
        'root.countLabel(root.volumeItems.length, "volume") + " · WeebCentral"',
        'sub: root.liveLoading ? "Cinemeta · loading"'
    ]) eq(catalogueSrc.includes(needle), true, `arc catalogue contains ${needle}`);
}

console.log('\nOne Piece volume card Tankoban-size contract');
const volumeCardSrc = fs.readFileSync('qml/OnePieceVolumeCard.qml', 'utf8');
eq(volumeCardSrc.includes('readonly property int bookHeight: 276'), true, 'volume card uses Tankoban max cover height');
eq(volumeCardSrc.includes('readonly property int bookWidth: 184'), true, 'volume card uses Tankoban 2:3 cover width');
eq(volumeCardSrc.includes('scale: root.activeFocus || hover.hovered ? 1.10 : 1.0'), true, 'volume card uses Tankoban selected cover scale');
eq(volumeCardSrc.includes('CataloguePosterCard {'), false, 'volume card does not shrink back to gallery poster geometry');
eq(volumeCardSrc.includes('text: "Volume " + root.entry.number'), true, 'volume identity sits below Tankoban-size cover');

console.log('\nMain One Piece routing contract');
const mainSrc = fs.readFileSync('qml/Main.qml', 'utf8');
eq(mainSrc.includes('com.colosseum.universe.onepiece'), true, 'Main recognizes One Piece universe id');
eq(mainSrc.includes('OnePieceUniversePage.qml'), true, 'Main routes One Piece to bespoke page');
eq(mainSrc.includes('UniverseExtensionPage.qml'), true, 'Main preserves generic universe fallback');
eq(mainSrc.includes('item.continueResumeRequested.connect(win.resumeContinue)'), true, 'Main wires One Piece Continue resume');
eq(mainSrc.includes('item.continueDetailRequested.connect(win.detailContinue)'), true, 'Main wires One Piece Continue detail');
eq(mainSrc.includes('item.onePaceRequested.connect(win.openOnePaceArc)'), true,'Main wires One Pace route');
eq(mainSrc.includes('item.installedExtensions = Qt.binding(function() { return win.installedExtensions })'), true, 'Main binds installed extensions into One Piece');
eq(mainSrc.includes('function openWeebCentralSeries'), true, 'Main exposes narrow WeebCentral series route');
eq(mainSrc.includes('seriesLayer.legacyWeebCentral'), true, 'Main tracks legacy WeebCentral mode');
eq(mainSrc.includes('MangaSeriesThumbnailMock.qml'), true, 'legacy WeebCentral mode uses provider-backed series surface');
eq(mainSrc.includes('win.openWeebCentralSeries(e.title || "", requestedVolumeNumber)'), true, 'One Piece color entry uses narrow WeebCentral route with exact-volume landing');

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
