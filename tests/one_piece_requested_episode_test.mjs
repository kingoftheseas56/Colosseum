import fs from 'fs';

const src = fs.readFileSync('qml/TheatreSeries.qml', 'utf8');
let failed = 0;
function expect(needle, label) {
    if (src.includes(needle)) console.log('ok  ' + label);
    else { console.log('FAIL ' + label + ' -> missing ' + needle); failed++; }
}

expect('itemData.requestedSeason', 'series detail reads requested season');
expect('itemData.requestedEpisode', 'series detail reads requested episode');
expect('seasonExists(requestedSeason)', 'requested season is validated against provider seasons');
expect('page.episodeIndex(requestedEpisode)', 'requested episode resolves against visible episode model');
expect('page.scrollToEpisodeIndex(requestedIdx)', 'requested episode scrolls into view');

process.exit(failed ? 1 : 0);
