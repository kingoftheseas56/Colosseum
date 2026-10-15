import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

let failed=0;
const ok=m=>console.log('  ok   '+m);
const bad=m=>{console.log('  FAIL '+m);failed++;};
const check=(v,m)=>v?ok(m):bad(m);
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const page=fs.readFileSync(path.join(root,'qml','OnePieceUniversePage.qml'),'utf8');

console.log('One Piece native East Blue universe page contract');
for(const needle of [
  'property string extensionId',
  'property string universeName',
  'property bool reducedMotion',
  'property var installedExtensions',
  'signal arcRequested(var arc)',
  'signal mediaRequested(var entry)',
  'signal backRequested()',
  'readonly property bool atlasTransientOpen',
  'function requestEscape()',
  'OnePieceEastBlueAtlas {',
  'anchors.fill: parent',
  'onArcRequested: function(arc)',
  'onMediaRequested: function(entry)',
  'root.arcRequested(arc)',
  'root.mediaRequested(entry)'
]) check(page.includes(needle),`page contains ${needle}`);
check(page.includes('atlasTransientOpen: atlas.transientOpen'), 'universe forwards atlas transient state');
check(page.includes('return atlas.requestEscape()'), 'universe forwards atlas Escape request');
for(const forbidden of [
  'OnePieceWorld3D {',
  'OnePieceEastBlueMap {',
  'OnePieceArcDock {',
  'OnePieceArcCatalogue {',
  'ContinueTile {',
  'function scrollArcDockIntoView()',
  'function scrollEastBlueIntoView()',
  'id: arcDockScroll',
  'Progress.recent("", 100)'
]) check(!page.includes(forbidden),`page removes ${forbidden}`);

check(!page.includes('contentHeight:'),'universe page is not a vertical mega-scroll');
check(page.includes('OnePieceEastBlueAtlas {'),'universe hosts the approved native East Blue composition');
check(!page.includes('world3d.focusRegion'),'legacy globe boot is removed');
check(!page.includes('function arcById(arcId)'),'legacy globe identity resolver is removed');
check(page.includes('onMinimizeRequested') || page.includes('signal minimizeRequested()'),'shell minimize remains available');
check(page.includes('onFullscreenRequested') || page.includes('signal fullscreenRequested()'),'shell fullscreen remains available');
check(page.includes('onCloseRequested') || page.includes('signal closeRequested()'),'shell close remains available');

console.log(failed?`\n${failed} FAILED`:'\nall green');
process.exit(failed?1:0);
