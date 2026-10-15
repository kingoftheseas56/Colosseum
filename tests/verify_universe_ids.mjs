// verify_universe_ids.mjs — every entry in every universe payload must carry a REAL, routable
// provider ID. Not "has an id field" — the ID must actually RESOLVE at its provider.
//
// This exists because five DCAU ids were once WRONG rather than missing (one an adult film in a
// Batman row): a wrong id is syntactically perfect and only a fetch can catch it. Hemanth,
// 2026-07-29: "make sure every one of the titles is connected to a respective weebcentral/
// anilist, applebook, cinemeta ID".
//
// NETWORK TEST — not part of the offline suite. Run deliberately:
//   node tests/verify_universe_ids.mjs
// Providers, by section kind / entry.provider:
//   video      → Cinemeta   /meta/<type>/<id>.json          (the exact call the tile's route makes)
//   anilist    → AniList    GraphQL Media(id, type:MANGA)
//   weebcentral→ WeebCentral cover CDN (constructible URL — a 200 proves the series id)
//   applebooks → iTunes     lookup?id=<trackId>
//   getcomics  → GetComics  ?p=<postId>  (title must be non-empty)
import { readFileSync } from 'node:fs';

const sleep = ms => new Promise(r => setTimeout(r, ms));
const load = f => JSON.parse(readFileSync(`assets/universes/${f}.json`, 'utf8')).universe;

async function head(url) {
    try {
        const r = await fetch(url, { method: 'GET', redirect: 'follow',
                                     headers: { 'User-Agent': 'Colosseum/1.0' } });
        return { ok: r.ok, status: r.status, body: r };
    } catch (e) { return { ok: false, status: 0, err: String(e) }; }
}

async function checkVideo(e) {
    const url = `https://v3-cinemeta.strem.io/meta/${e.type}/${e.id}.json`;
    const r = await head(url);
    if (!r.ok) return { ok: false, why: `cinemeta ${r.status || r.err}` };
    let j; try { j = await r.body.json(); } catch { return { ok: false, why: 'cinemeta bad json' }; }
    const name = j && j.meta && j.meta.name;
    if (!name) return { ok: false, why: 'cinemeta returned no meta.name' };
    return { ok: true, resolved: name };
}

async function checkAnilist(e) {
    const q = 'query($id:Int){Media(id:$id,type:MANGA){id title{romaji english} coverImage{extraLarge}}}';
    try {
        const r = await fetch('https://graphql.anilist.co', {
            method: 'POST', headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify({ query: q, variables: { id: Number(e.id) } })
        });
        const j = await r.json();
        const m = j && j.data && j.data.Media;
        if (!m) return { ok: false, why: 'anilist: no Media for that id' };
        const cover = m.coverImage && m.coverImage.extraLarge;
        return { ok: true, resolved: (m.title.english || m.title.romaji), cover: !!cover };
    } catch (err) { return { ok: false, why: 'anilist ' + err }; }
}

async function checkWeebcentral(e) {
    const url = `https://temp.compsci88.com/cover/fallback/${e.id}.jpg`;
    const r = await head(url);
    if (!r.ok) return { ok: false, why: `weebcentral cover ${r.status || r.err}` };
    const len = Number(r.body.headers.get('content-length') || 0);
    if (len > 0 && len < 512) return { ok: false, why: `weebcentral cover suspiciously small (${len}B)` };
    return { ok: true, resolved: `cover ${len || '?'}B` };
}

async function checkApplebooks(e) {
    const r = await head(`https://itunes.apple.com/lookup?id=${encodeURIComponent(e.id)}`);
    if (!r.ok) return { ok: false, why: `itunes ${r.status || r.err}` };
    let j; try { j = await r.body.json(); } catch { return { ok: false, why: 'itunes bad json' }; }
    const hit = j && j.results && j.results[0];
    if (!hit) return { ok: false, why: 'itunes: resultCount 0 — dead store id' };
    const art = hit.artworkUrl100 || hit.artworkUrl60;
    return { ok: true, resolved: hit.trackName || hit.collectionName || '(untitled)', cover: !!art };
}

// tankoban — the app's own reader route (Main.qml routeUniverseSeries): the entry is keyed
// by its MAL id plus the reader's series key. The id is proven through AniList's idMal
// mirror (Jikan/MAL-direct are unreachable from this machine — 21s IPv6-style stall,
// observed 2026-10-05): if the MAL id maps to no manga, the entry is dead. Same
// fetch-or-it-didn't-happen law as every other provider here.
async function checkTankoban(e) {
    if (!e.malId || !e.seriesId)
        return { ok: false, why: 'tankoban: entry must carry malId + seriesId for the reader route' };
    const q = 'query($id:Int){Media(idMal:$id,type:MANGA){id title{romaji english}}}';
    try {
        const r = await fetch('https://graphql.anilist.co', {
            method: 'POST', headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify({ query: q, variables: { id: Number(e.malId) } })
        });
        const j = await r.json();
        const m = j && j.data && j.data.Media;
        if (!m) return { ok: false, why: `anilist idMal: no manga for malId ${e.malId}` };
        return { ok: true, resolved: `mal:${e.malId} ${m.title.english || m.title.romaji} (reader ${e.seriesId})` };
    } catch (err) { return { ok: false, why: 'anilist ' + err }; }
}

async function checkGetcomics(e) {
    const id = (e.posts || [])[0];
    if (!id) return { ok: false, why: 'no post id' };
    const url = `https://getcomics.org/?p=${id}`;
    // getcomics rate-limits rapid bursts (429 after ~8 quick hits, observed 2026-10-05).
    // Back off and retry — a passing id must still fetch a titled post eventually.
    let r = await head(url);
    for (let attempt = 1; !r.ok && (r.status === 429 || r.status === 503) && attempt <= 3; attempt++) {
        await sleep(15000 * attempt);
        r = await head(url);
    }
    if (!r.ok) return { ok: false, why: `getcomics ${r.status || r.err}` };
    const html = await r.body.text();
    const m = html.match(/<title>([^<]*)<\/title>/i);
    const t = m ? m[1].replace(/&#8211;/g, '-').replace(/\s*-\s*GetComics\s*$/, '').trim() : '';
    if (!t) return { ok: false, why: 'getcomics: post has no title' };
    return { ok: true, resolved: t };
}

function providerOf(section, e) {
    if (section.kind === 'video') return 'cinemeta';
    if (section.kind === 'comic') return 'getcomics';
    return e.provider || '(none)';
}

const CHECK = { cinemeta: checkVideo, anilist: checkAnilist, weebcentral: checkWeebcentral,
                applebooks: checkApplebooks, getcomics: checkGetcomics, tankoban: checkTankoban };

let total = 0, failed = 0, noId = 0;
const failures = [];

for (const file of ['one-piece', 'dcau']) {
    const u = load(file);
    console.log(`\n=== ${u.title} (${file}) ===`);
    for (const s of u.sections) {
        console.log(`\n-- ${s.title} [${s.kind}] (${s.entries.length})`);
        for (const e of s.entries) {
            total++;
            const prov = providerOf(s, e);
            const hasIdentity = !!e.id || (e.posts && e.posts.length);
            if (!hasIdentity) {
                noId++; failed++;
                failures.push(`${u.title} / ${s.title} / ${e.title} — NO PROVIDER ID AT ALL`);
                console.log(`  ✗ ${(e.title || '').padEnd(52)} NO PROVIDER ID`);
                continue;
            }
            const fn = CHECK[prov];
            if (!fn) {
                failed++;
                failures.push(`${u.title} / ${s.title} / ${e.title} — unknown provider "${prov}"`);
                console.log(`  ✗ ${(e.title || '').padEnd(52)} unknown provider "${prov}"`);
                continue;
            }
            const r = await fn(e);
            const idStr = e.id || `posts[${e.posts[0]}]`;
            if (r.ok) {
                console.log(`  ok ${(e.title || '').padEnd(52)} ${prov}:${idStr} -> ${r.resolved}`);
            } else {
                failed++;
                failures.push(`${u.title} / ${s.title} / "${e.title}" (${prov}:${idStr}) — ${r.why}`);
                console.log(`  ✗ ${(e.title || '').padEnd(52)} ${prov}:${idStr} -> ${r.why}`);
            }
            await sleep(220);
        }
    }
}

console.log(`\n\n================ SUMMARY ================`);
console.log(`entries checked : ${total}`);
console.log(`with no id      : ${noId}`);
console.log(`FAILED          : ${failed}`);
if (failures.length) {
    console.log(`\n--- every failure ---`);
    failures.forEach(f => console.log('  ' + f));
}
console.log(failed ? '\nNOT ALL IDS RESOLVE' : '\nall ids resolve');
process.exit(failed ? 1 : 0);
