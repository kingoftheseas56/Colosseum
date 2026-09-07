import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

let failed=0;
const ok=m=>console.log('  ok   '+m);
const bad=m=>{console.log('  FAIL '+m);failed++;};
const check=(v,m)=>v?ok(m):bad(m);
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const arcPagePath=path.join(root,'qml','OnePieceArcPage.qml');
const cataloguePath=path.join(root,'qml','OnePieceArcCatalogue.qml');

console.log('One Piece dedicated arc page contract');
check(fs.existsSync(arcPagePath),'OnePieceArcPage.qml exists');
const page=fs.existsSync(arcPagePath)?fs.readFileSync(arcPagePath,'utf8'):'';
const catalogue=fs.readFileSync(cataloguePath,'utf8');

for(const needle of [
  'required property var arc',
  'property string extensionId',
  'property var installedExtensions',
  'signal backRequested()',
  'signal watchRequested(var payload)',
  'signal seriesRequested(var entry)',
  'OnePieceArcCatalogue {',
  'UniverseApi.load(root.extensionId',
  'onEpisodeRequested: function(entry)',
  'onMangaVolumeRequested: function(colorEdition, volumeNumber)'
]) check(page.includes(needle),`arc page contains ${needle}`);
for(const needle of [
  'function relatedItemsForArc()',
  'Episode of East Blue',
  'Episode of Nami',
  'relatedItems: root.relatedItemsForArc()',
  'onRelatedRequested: function(entry)',
  'function openCatalogueVolume(volumeNumber, colorEdition)',
  'requestedVolumeNumber',
  'routed.colorEdition = colorEdition === true'
]) check(page.includes(needle),`arc page contains ${needle}`);

for(const needle of [
  'property var relatedItems: []',
  'signal relatedRequested(var entry)',
  'id: relatedBlock',
  'title: "Related Adaptations"',
  'model: root.relatedItems',
  'onActivated: root.relatedRequested(modelData)'
]) check(catalogue.includes(needle),`catalogue contains ${needle}`);

check(catalogue.includes('CatalogApi.loadAnimeEpisodes(root.arc'),'arc page still uses existing Kitsu loader');
check(catalogue.includes('CatalogApi.loadOnePaceEpisodes(root.installedExtensions, root.arc'),'arc page still uses existing One Pace loader');
check(catalogue.includes('CatalogApi.loadLiveActionEpisodes(root.arc'),'arc page still uses existing Cinemeta loader');
check(catalogue.includes('tankobanCatalogRef.volumes'),'arc page still uses existing Tankoban volumes');
check(page.includes('function colorMangaEntry()') && page.includes('root.entry("manga", "one-piece-color")'),'arc page reuses the universe payload identity for WeebCentral colored manga');

console.log(failed?`\n${failed} FAILED`:'\nall green');
process.exit(failed?1:0);