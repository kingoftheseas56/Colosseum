// Download grouping — pure groupJobs() logic tests (2026-08-05 Task 1).
//
// Extracts the REAL groupJobs()/isLiveState() functions out of DownloadsPage.qml (both are
// portable plain JS — no QML-only calls) and executes them in Node, so this proves the actual
// production algorithm, not a hand-duplicated copy that could silently drift from it.
//
// Covers: manga volume batches and multi-part comics collapse into one grouped row (Hemanth's
// option B, 2026-08-05 — display via the shipped season-grouping pattern, no new UI); the group
// title never reads "— Season 0" for a non-Theatre group; Theatre's own season titling is
// untouched; and the negative controls — a solo volume / single-archive comic renders as a
// single row, byte-identical to today.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import assert from 'node:assert';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const source = readFileSync(join(root, 'qml/DownloadsPage.qml'), 'utf8');

// Brace-counting extractor: a single regex can't safely bound a function body that itself
// contains nested { }, and both target functions do.
function extractFunction(src, name) {
    const startMatch = src.match(new RegExp(`function ${name}\\s*\\([^)]*\\)\\s*\\{`));
    if (!startMatch) throw new Error(`could not find function ${name} in DownloadsPage.qml`);
    const bodyStart = startMatch.index + startMatch[0].length;
    let depth = 1, i = bodyStart;
    for (; i < src.length && depth > 0; i++) {
        if (src[i] === '{') depth++;
        else if (src[i] === '}') depth--;
    }
    if (depth !== 0) throw new Error(`unbalanced braces extracting ${name}`);
    return src.slice(startMatch.index, i);
}

const isLiveStateSrc = extractFunction(source, 'isLiveState');
const groupJobsSrc = extractFunction(source, 'groupJobs');
// eslint-disable-next-line no-new-func
const { isLiveState, groupJobs } = new Function(
    `${isLiveStateSrc}\n${groupJobsSrc}\nreturn { isLiveState, groupJobs };`
)();

// ---- fixtures ----------------------------------------------------------------------------

function mangaVolume(n, groupKey) {
    return {
        world: 'tankoban', id: `vol-${n}`, seriesTitle: 'One Piece',
        title: `One Piece — Vol. ${n}`, state: 'downloading',
        received: 10, total: 100, groupKey, groupUnit: 'volumes'
    };
}

function comicPart(n, groupKey) {
    return {
        world: 'tankoban', id: `chew-part-${n}`, seriesTitle: 'Chew',
        title: 'Chew — Chew #1 – 60 + TPB Vol. 1 – 12 + Extras (2009-2017)',
        state: 'downloading', received: 10, total: 100, groupKey, groupUnit: 'parts'
    };
}

function theatreEpisode(season, ep) {
    return {
        world: 'theatre', id: `tt123:${season}:${ep}`, groupKey: `tt123:s${season}`,
        seriesTitle: 'Dune', title: `Dune — S${season}E${ep}`, season,
        state: 'downloading', received: 10, total: 100
    };
}

// ---- 1. manga batch groups into one row ---------------------------------------------------

{
    const rows = Array.from({ length: 10 }, (_, i) => mangaVolume(i + 1, 'abc123infohash'));
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 1, '10 volumes sharing a groupKey must collapse to ONE group');
    assert.strictEqual(groups[0].count, 10, 'group must count all 10 member rows');
    assert.strictEqual(groups[0].single, false);
    assert.ok(!groups[0].title.includes('Season'), `manga group title must never say Season: "${groups[0].title}"`);
    assert.ok(groups[0].title.includes('One Piece'), `manga group title must name the series: "${groups[0].title}"`);
    assert.ok(groups[0].title.includes('10'), `manga group title must state the count: "${groups[0].title}"`);
}

// ---- 2. multi-part comic groups into one row ----------------------------------------------

{
    const rows = Array.from({ length: 7 }, (_, i) => comicPart(i + 1, 'https://getcomics.org/chew-1-60'));
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 1, '7 parts sharing a groupKey (post URL) must collapse to ONE group');
    assert.strictEqual(groups[0].count, 7);
    assert.strictEqual(groups[0].single, false);
    assert.ok(!groups[0].title.includes('Season'), `comics group title must never say Season 0: "${groups[0].title}"`);
    assert.ok(groups[0].title.includes('7'), `comics group title must state the part count: "${groups[0].title}"`);
}

// ---- 3. NEGATIVE CONTROL: a solo volume stays a single row, unchanged ---------------------

{
    const rows = [mangaVolume(3, 'own-unique-infohash')];
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 1);
    assert.strictEqual(groups[0].single, true, 'a solo volume must render as a single-row group');
    assert.strictEqual(groups[0].title, 'One Piece — Vol. 3',
        'a solo volume title must be byte-identical to the row title — no grouping decoration');
}

// ---- 4. NEGATIVE CONTROL: a single-archive comic stays a single row, unchanged ------------

{
    const rows = [comicPart(1, 'https://getcomics.org/one-shot-issue')];
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 1);
    assert.strictEqual(groups[0].single, true, 'a single-archive comic must render as a single-row group');
    assert.strictEqual(groups[0].title, rows[0].title,
        'a single-archive comic title must be byte-identical to the row title');
}

// ---- 5. REGRESSION GUARD: Theatre season titling is untouched -----------------------------

{
    const rows = [theatreEpisode(2, 1), theatreEpisode(2, 2), theatreEpisode(2, 3)];
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 1);
    assert.strictEqual(groups[0].title, 'Dune — Season 2',
        'Theatre season grouping/titling must be byte-identical to today');
}

// ---- 6. Different manga batches never collide ----------------------------------------------

{
    const rows = [
        ...Array.from({ length: 3 }, (_, i) => mangaVolume(i + 1, 'batch-A')),
        ...Array.from({ length: 2 }, (_, i) => mangaVolume(i + 4, 'batch-B'))
    ];
    const groups = groupJobs(rows);
    assert.strictEqual(groups.length, 2, 'two distinct batches must never merge into one group');
}

// ---- 7. isLiveState covers manga's real state vocabulary -----------------------------------
//
// activeVolumeJobs() emits "resolving"/"downloading"/"packing"/"ingesting"/"failed" (ground-
// truthed against MangaTankobanService.cpp). isLiveState() only recognized a Theatre-shaped
// subset before this fix — a group entirely mid-pack/mid-ingest scored liveCount===0 with no
// progress bar, "0 of N landed" against real work in flight.

assert.strictEqual(isLiveState('downloading'), true);
assert.strictEqual(isLiveState('done'), false);
assert.strictEqual(isLiveState('packing'), true, 'packing is a real in-flight manga-volume state');
assert.strictEqual(isLiveState('ingesting'), true, 'ingesting is a real in-flight manga-volume state');

// ---- 8. hasKnownTotal — the progress bar's actual gate ------------------------------------
//
// DownloadsPage.qml:747 gates the group progress bar on `liveCount > 0 && hasKnownTotal`, and
// hasKnownTotal is purely derived from summed `total` across rows. This only proves the
// ALGORITHM is correct given rows that carry total/received — it does NOT prove
// LocalDownloads.cpp actually emits those fields for manga/comic rows (a cross-language fact
// this pure-JS harness cannot check). That emission is verified separately by build + eyes-on
// (a real batch must show a visible progress bar, not just a correct title).

{
    const rows = Array.from({ length: 10 }, (_, i) => mangaVolume(i + 1, 'abc123infohash'));
    const groups = groupJobs(rows);
    assert.strictEqual(groups[0].hasKnownTotal, true,
        'a manga batch group with total/received on every row must know its total');
}

// ---- 9. Group member rows are sorted deterministically -------------------------------------
//
// m_acq is a QHash (MangaTankobanService.h) — iteration order is unspecified and can change on
// rehash. Without a stable sort, a batch's expanded fold would visibly reshuffle its volumes on
// every refresh tick. groupJobs() must sort each group's rows by id regardless of input order.

{
    const shuffled = [
        mangaVolume(7, 'batch-sort'), mangaVolume(2, 'batch-sort'), mangaVolume(9, 'batch-sort'),
        mangaVolume(1, 'batch-sort'), mangaVolume(5, 'batch-sort')
    ];
    const groups = groupJobs(shuffled);
    const ids = groups[0].rows.map(r => r.id);
    const sortedIds = [...ids].sort();
    assert.deepStrictEqual(ids, sortedIds,
        `group rows must be sorted by id regardless of input order — got ${JSON.stringify(ids)}`);
}

console.log('DOWNLOAD_GROUPING_TEST_OK');
