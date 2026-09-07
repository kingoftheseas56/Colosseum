import fs from 'node:fs';

const source = fs.readFileSync('qml/MangaChapterSeriesView.qml', 'utf8');
let failures = 0;
function check(ok, message) {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures += 1;
}

check(source.includes('import QtQuick.Controls'),
  'chapter view imports the Controls popup surface');
check(/Popup\s*\{[\s\S]*?id:\s*pagePopup/.test(source),
  'page selector menu is a real Popup');
check(/id:\s*pagePopup[\s\S]*?parent:\s*pageSelector/.test(source),
  'popup coordinates are owned by the page selector');
check(/closePolicy:\s*Popup\.CloseOnEscape\s*\|\s*Popup\.CloseOnPressOutsideParent/.test(source),
  'popup owns Escape and outside-click dismissal');
check(!/x:\s*pageSelector\.mapToItem\(root/.test(source)
      && !/y:\s*pageSelector\.mapToItem\(root/.test(source),
  'menu placement no longer binds a root-level rectangle to mapped Flickable coordinates');
check(!/visible:\s*root\.pageMenuOpen[\s\S]{0,100}z:\s*190[\s\S]{0,100}anchors\.fill:\s*parent/.test(source),
  'the page no longer needs a full-screen click-catcher behind the menu');

if (failures) process.exit(1);
console.log('\nPASS — chapter page menu is selector-anchored popup content');
