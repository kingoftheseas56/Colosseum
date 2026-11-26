// Build the immutable public AudioSilo query graph from the verified CC0 bake.
import { createHash } from 'node:crypto';
import { closeSync, existsSync, openSync, readFileSync, readSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';
import {
    audiobooksMethod, discoverable, item, itemSql, recordingRow, released, releaseOrder, usableCover, workRow
} from '../src/catalogue-audiobooks.ts';
import { packLookupFiles } from './shard_packing.mjs';
import { audioContract } from './public_static.mjs';

const lex = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const ordered = map => [...map].sort((a, b) => lex(a[0], b[0]));
const recordKey = id => String(id).padStart(8, '0');
const hash = raw => createHash('sha256').update(raw).digest('hex');

function fileHash(path) {
    const fd = openSync(path, 'r');
    const out = createHash('sha256');
    const buffer = Buffer.allocUnsafe(4 * 1024 * 1024);
    try {
        for (let n; (n = readSync(fd, buffer, 0, buffer.length, null));) out.update(buffer.subarray(0, n));
        return out.digest('hex');
    } finally {
        closeSync(fd);
    }
}

function verifySource(path, db) {
    const manifest = JSON.parse(readFileSync(path.replace(/\.sqlite$/, '.manifest.json')));
    if (manifest.format !== 'colosseum-audiobooks-cc0-v2' || manifest.fixture !== false ||
        JSON.stringify(manifest.rights) !== '["CC0-1.0"]' || manifest.sqlite_sha256 !== fileHash(path))
        throw Error('AUDIOBOOK_BAKE_NOT_VERIFIED');
    const tables = [
        'works', 'people', 'series', 'work_authors', 'recordings', 'recording_narrators',
        'work_series', 'recording_asins', 'recording_isbns', 'work_genres', 'narrator_shelves',
        'genres_summary', 'search_fts'
    ];
    const actual = db.prepare("SELECT name FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'search\\_fts\\_%' ESCAPE '\\'")
        .all().map(row => row.name).sort();
    if (JSON.stringify(actual) !== JSON.stringify([...tables].sort()) ||
        Object.keys(manifest.tables).length !== tables.length ||
        db.prepare('PRAGMA user_version').get().user_version !== 2)
        throw Error('AUDIOBOOK_BAKE_SCHEMA');
    const rows = {};
    for (const table of tables) {
        const provenance = manifest.tables[table];
        const columns = db.prepare('PRAGMA table_info(' + table + ')').all().map(column => column.name);
        if (JSON.stringify(provenance?.licenses) !== '["CC0-1.0"]' ||
            JSON.stringify(columns.sort()) !== JSON.stringify(Object.keys(provenance.columns).sort()) ||
            Object.values(provenance.columns).some(origins => !origins.length ||
                origins.some(origin => !manifest.cc0_sources.includes(origin.split('.')[0]))))
            throw Error('AUDIOBOOK_BAKE_RIGHTS: ' + table);
        rows[table] = db.prepare('SELECT count(*) AS n FROM ' + table).get().n;
        if (rows[table] !== provenance.rows) throw Error('AUDIOBOOK_BAKE_COUNT: ' + table);
    }
    return {sha256: manifest.sqlite_sha256, snapshot: manifest.source_snapshot_id, rows};
}

function varint(bytes, n) {
    while (n >= 128) {
        bytes.push((n & 127) | 128);
        n >>>= 7;
    }
    bytes.push(n);
}

function searchData(db, releasedWork, put, pack) {
    db.exec('CREATE VIRTUAL TABLE temp.token_instances USING fts5vocab(main, search_fts, instance)');
    const docCount = db.prepare('SELECT count(*) AS n FROM search_fts').get().n;
    const lengths = new Uint32Array(docCount + 1);
    let tokenCount = 0;
    for (const {doc} of db.prepare('SELECT d.rank AS doc FROM token_instances v JOIN doc_order d ON v.doc=d.id').iterate()) {
        if (doc < 1 || doc > docCount) throw Error('AUDIOBOOK_SEARCH_DOCUMENT_ID');
        ++lengths[doc];
        ++tokenCount;
    }
    const postings = new Map();
    let term, bytes = [], id = 0, last = 0, frequency = 0;
    function flushDoc() {
        if (!frequency) return;
        varint(bytes, id - last);
        varint(bytes, frequency);
        varint(bytes, releasedWork.has(id) ? lengths[id] : 0);
        last = id;
        frequency = 0;
    }
    function flushTerm() {
        if (term === undefined) return;
        flushDoc();
        postings.set(term, Buffer.from(bytes).toString('base64'));
        bytes = [];
        id = last = 0;
    }
    for (const row of db.prepare('SELECT v.term,d.rank AS doc FROM token_instances v JOIN doc_order d ON v.doc=d.id ORDER BY v.term,d.rank').iterate()) {
        if (row.term !== term) {
            flushTerm();
            term = row.term;
        }
        if (row.doc !== id) {
            flushDoc();
            id = row.doc;
        }
        ++frequency;
    }
    flushTerm();

    // For a query word the semantic API applies NFKC before unicode61 tokenization.
    // Export the tokenizer's exact non-ASCII letter/number case/diacritic mapping.
    db.exec("CREATE VIRTUAL TABLE temp.query_characters USING fts5(ch, tokenize='unicode61 remove_diacritics 2')");
    db.exec('CREATE VIRTUAL TABLE temp.query_tokens USING fts5vocab(temp, query_characters, instance)');
    const insert = db.prepare('INSERT INTO query_characters(rowid,ch) VALUES (?,?)');
    const character = /[\p{L}\p{N}]/u;
    const tokens = Object.create(null);
    for (let cp = 128; cp <= 0x10ffff; ++cp) {
        const ch = String.fromCodePoint(cp);
        if (character.test(ch)) {
            insert.run(cp, ch);
            tokens[ch] = '';
        }
    }
    for (const {term, doc} of db.prepare('SELECT term,doc FROM query_tokens').iterate()) {
        const ch = String.fromCodePoint(doc);
        if (ch === term) delete tokens[ch];
        else tokens[ch] = term;
    }
    return {
        docs: docCount, tokens: tokenCount, terms: postings.size, tokenizer: put(tokens),
        postings: pack(postings, 80 * 1024, true)
    };
}

export async function prepareAudioCatalogue({ source, output, date }) {
    if (existsSync(output) || !/^\d{4}-\d\d-\d\d$/.test(date) ||
        new Date(date).toISOString().slice(0, 10) !== date)
        throw Error('AUDIOBOOK_OUTPUT_OR_DATE');
    const db = new DatabaseSync(source, {readOnly: true});
    const entries = new Map();
    function putRaw(raw) {
        const digest = hash(raw);
        const path = '/internal/audiobooks/' + digest + '.json';
        if (entries.has(path)) return path;
        const target = join(output, path);
        mkdirSync(dirname(target), {recursive: true});
        writeFileSync(target, raw);
        entries.set(path, Buffer.byteLength(raw));
        return path;
    }
    const put = value => putRaw(JSON.stringify(value));

    // Keep regular shards small. Oversize individual values remain addressable
    // without forcing ordinary reads to carry their bytes.
    function pack(map, budget = 160 * 1024, largeSingles = false) {
        const ranges = [], pending = [];
        function flush() {
            for (const {first,last,body} of packLookupFiles(pending, budget))
                ranges.push([first,last,putRaw(body)]);
            pending.length = 0;
        }
        for (const [key, value] of ordered(map)) {
            const body = JSON.stringify({[key]: value});
            if (largeSingles && Buffer.byteLength(body) > budget) {
                if (Buffer.byteLength(body) > 1024 * 1024) throw Error('AUDIOBOOK_OVERSIZE_VALUE');
                flush();
                ranges.push([key,key,putRaw(body)]);
            } else pending.push([key,value]);
        }
        flush();
        return put(ranges);
    }

    function sequence(rows, pageSize, group = 32) {
        const width = pageSize * group;
        const parts = [];
        for (let start = 0; start < rows.length; start += width)
            parts.push(put(rows.slice(start, start + width)));
        return {length: rows.length, width, parts};
    }

    try {
        const provenance = verifySource(source, db);
        const pinned = sql => sql.replaceAll("date('now')", "date('" + date + "')");
        const statements = new Map();
        const statement = sql => {
            if (!statements.has(sql)) statements.set(sql, db.prepare(pinned(sql)));
            return statements.get(sql);
        };
        const adapter = {prepare(sql) { return {bind(...args) { return {async all() {
            return {success:true, results:statement(sql).all(...args)};
        }}; }}; }};
        const api = (method, ...args) => audiobooksMethod(adapter, method, args);
        const prepare = sql => db.prepare(pinned(sql));
        db.exec(`
            CREATE TEMP TABLE doc_order (id INTEGER PRIMARY KEY, rank INTEGER NOT NULL);
            INSERT INTO doc_order SELECT f.rowid,
                row_number() OVER (ORDER BY w.title COLLATE NOCASE,w.id,f.rowid)
            FROM search_fts f LEFT JOIN works w ON w.id=f.id AND f.kind='work'
        `);
        const works = new Map(), summaries = new Map(), documentIds = new Map();
        const releasedWork = new Set();
        for (const row of prepare(`
            SELECT d.rank AS doc,w.*,
                EXISTS (SELECT 1 FROM recordings r WHERE r.work_id=w.id AND ${released('r')}) AS released
            FROM search_fts f JOIN doc_order d ON f.rowid=d.id JOIN works w ON w.id=f.id
            WHERE f.kind='work' ORDER BY f.rowid`).iterate()) {
            documentIds.set(row.id, row.doc);
            summaries.set(recordKey(row.doc), workRow(row));
            if (row.released) releasedWork.add(row.doc);
            works.set(row.id, await api('work', row.id));
        }
        if (works.size !== provenance.rows.works) throw Error('AUDIOBOOK_WORK_COVERAGE');
        const records = {work: pack(works, 224 * 1024, true), summaries: pack(summaries, 64 * 1024)};
        works.clear();

        for (const [apiName, column] of [['recordingByAsin','asin'], ['recordingByIsbn','isbn']]) {
            const keys = new Map();
            for (const row of prepare(`
                SELECT ${column} AS key, MIN(work_id) AS work_id, MIN(recording_id) AS recording_id
                FROM (SELECT DISTINCT ${column},work_id,recording_id FROM recording_${column}s)
                GROUP BY ${column} HAVING count(*)=1`).iterate()) {
                keys.set(row.key, [row.work_id, row.recording_id]);
            }
            records[apiName] = pack(keys, 160 * 1024);
        }

        const series = new Map(), seriesSearch = new Map();
        const seriesWorks = prepare(`
            SELECT w.id FROM work_series ws JOIN works w ON w.id=ws.work_id
            WHERE ws.series_id=? AND EXISTS (
                SELECT 1 FROM recordings r WHERE r.work_id=w.id AND ${released('r')})
            ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id LIMIT 100`);
        for (const {id} of prepare('SELECT id FROM series ORDER BY id').iterate()) {
            const searchDocs = seriesWorks.all(id).map(row => documentIds.get(row.id));
            series.set(id, await api('series', id));
            seriesSearch.set(id, searchDocs);
        }
        records.series = pack(series, 160 * 1024, true);
        records.seriesSearch = pack(seriesSearch, 36 * 1024);
        records.seriesKeys = put([...seriesSearch.keys()]);

        const people = new Map();
        const personWorks = prepare(`
            SELECT credited.person_id,w.id FROM works w JOIN (
                SELECT a.person_id,a.work_id FROM work_authors a WHERE EXISTS (
                    SELECT 1 FROM recordings r WHERE r.work_id=a.work_id AND ${released('r')})
                UNION
                SELECT n.person_id,n.work_id FROM recording_narrators n JOIN recordings r
                    ON r.work_id=n.work_id AND r.id=n.recording_id WHERE ${released('r')}
            ) credited ON credited.work_id=w.id
            ORDER BY credited.person_id,(w.first_year IS NULL),w.first_year DESC,w.title COLLATE NOCASE,w.id`);
        for (const row of personWorks.iterate()) {
            if (!people.has(row.person_id)) people.set(row.person_id, []);
            people.get(row.person_id).push(summaries.get(recordKey(documentIds.get(row.id))));
        }
        // Pack ordinary credited-work lists together. A file per person with
        // multiple pages exhausts the shared Free asset count on the full bake.
        records.byPerson = pack(new Map([...people].map(([key,rows]) =>
            [key, rows.length <= 480 ? rows : sequence(rows, 24, 20)])), 160 * 1024);

        const latest = [];
        for (const row of prepare(`
            SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id
            WHERE ${released('r')} ORDER BY r.release_year DESC,r.work_id,r.id`).iterate())
            latest.push({...recordingRow(row),title:row.title});

        const shelves = await api('shelves'), genres = await api('genres');
        const shelfRows = new Map(shelves.map(({key}) => [key,[]]));
        const genreRows = new Map();
        const editorialNarrators = new Map();
        for (const row of prepare(`
            SELECT n.work_id,n.recording_id,n.person_id FROM recording_narrators n
            JOIN narrator_shelves s USING(person_id)`).iterate()) {
            const key = JSON.stringify([row.work_id,row.recording_id]);
            if (!editorialNarrators.has(key)) editorialNarrators.set(key, new Set());
            editorialNarrators.get(key).add(row.person_id);
        }
        const workGenres = new Map();
        for (const {work_id,genre} of prepare('SELECT work_id,genre FROM work_genres').iterate()) {
            if (!workGenres.has(work_id)) workGenres.set(work_id, []);
            workGenres.get(work_id).push(genre);
            if (!genreRows.has(genre)) genreRows.set(genre, []);
        }
        const discover = itemSql.replace('FROM recordings r', ' ,r.librivox_id FROM recordings r');
        for (const row of prepare(`${discover} WHERE ${discoverable('r')}
            ORDER BY (${usableCover}) DESC,${releaseOrder}`).iterate()) {
            const card = item(row);
            shelfRows.get('new-releases').push(card);
            if (card.dramatized) shelfRows.get('dramatized').push(card);
            if (row.librivox_id == null || row.librivox_id === '') {
                const narrators = editorialNarrators.get(JSON.stringify([card.workId,card.recordingId]));
                for (const narrator of narrators ?? []) shelfRows.get('narrator:' + narrator).push(card);
            }
            for (const genre of workGenres.get(card.workId) ?? []) genreRows.get(genre).push(card);
        }
        const first = {};
        for (const [key, rows] of shelfRows)
            first[key] = {items:rows.slice(0,20),page:0,hasMore:rows.length>20};
        const layout = {
            date, start:put({shelves,genres,first}), records,
            shelf:pack(new Map([...shelfRows].map(([key,rows]) => [key,sequence(rows,20,64)]))),
            genre:pack(new Map([...genreRows].map(([key,rows]) => {
                const first = rows.slice(0,20), rest = rows.slice(20);
                return [key,{first,more:sequence(rest,20,64)}];
            }))),
            latest:put(sequence(latest,24,64)),
            search:put(searchData(db,releasedWork,put,pack))
        };
        const report = {contract:audioContract, provenance, date, layout:put(layout),
            entries:[...entries].map(([path,bytes]) => ({path,bytes})),
            coverPolicy:'external URLs only'};
        writeFileSync(join(output, 'audiobook-static-report.json'), JSON.stringify(report));
        return report;
    } finally {
        db.close();
    }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const [source,output,date] = process.argv.slice(2);
    if (!source || !output || !date)
        throw Error('Usage: node projection/audiobook_static.mjs verified-audiobooks-v2.sqlite fresh-output YYYY-MM-DD');
    const report = await prepareAudioCatalogue({ source, output, date });
    console.log(JSON.stringify({ date, provenance:report.provenance, files:report.entries.length,
        totalBytes:report.entries.reduce((sum,e)=>sum+e.bytes,0),
        largestFileBytes:Math.max(...report.entries.map(e=>e.bytes)) }));
}
