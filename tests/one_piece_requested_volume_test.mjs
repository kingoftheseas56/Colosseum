import fs from 'fs';

const main = fs.readFileSync('qml/Main.qml', 'utf8');
const manga = fs.readFileSync('qml/MangaSeries.qml', 'utf8');
const color = fs.readFileSync('qml/MangaSeriesThumbnailMock.qml', 'utf8');
let failed = 0;
function has(src, needle, label) {
    if (src.includes(needle)) console.log('ok  ' + label);
    else { console.log('FAIL ' + label + ' -> missing ' + needle); failed++; }
}

has(main, 'function openSeries(title, malId, requestedVolumeNumber)', 'Main accepts catalogue volume number');
has(main, 'property string requestedVolumeNumber', 'series loader stores requested volume number');
has(main, 'item.requestedVolumeNumber = seriesLayer.requestedVolumeNumber', 'series loader forwards requested volume number');
has(manga, 'property string requestedVolumeNumber: ""', 'manga exposes requested volume seam');
has(manga, 'function openRequestedVolume()', 'manga resolves requested volume');
has(manga, 'String(rows[i].number) === page.requestedVolumeNumber', 'manga resolves against canonical shelf rows');
has(manga, 'page._readVolume(String(rows[i].id))', 'manga opens exact requested volume');
has(color, 'property string requestedVolumeNumber: ""', 'colored manga exposes requested volume seam');
has(color, 'function openRequestedVolume()', 'colored manga resolves requested volume');
has(color, 'String(rows[i].number) === page.requestedVolumeNumber', 'colored manga resolves against canonical shelf rows');
has(color, 'readingRoom.library.chooseSource(String(rows[i].id))', 'colored manga acquires exact requested volume when needed');

process.exit(failed ? 1 : 0);
