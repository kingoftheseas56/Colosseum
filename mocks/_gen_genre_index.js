// Generator: builds mocks/genre-index.html (the "Explore" genre directory) from LIVE Jikan data.
// Fetches /genres/manga, partitions into MAL's manga.php groups, and pulls a representative cover for
// the prominent genres (Genres + Demographics + top Themes). Explicit + long-tail Themes = gradient tiles.
// Run: node mocks/_gen_genre_index.js   (Node 24 global fetch). Throttled to respect Jikan's 3/sec.
const fs = require("fs");
const JIKAN = "https://api.jikan.moe/v4";
const sleep = ms => new Promise(r => setTimeout(r, ms));

const GENRES = ["Action","Adventure","Avant Garde","Award Winning","Boys Love","Comedy","Drama","Fantasy",
  "Girls Love","Gourmet","Horror","Mystery","Romance","Sci-Fi","Slice of Life","Sports","Supernatural","Suspense"];
const EXPLICIT = ["Ecchi","Erotica","Hentai"];
const DEMOGRAPHICS = ["Shounen","Shoujo","Seinen","Josei","Kids"];
const classOf = n => EXPLICIT.includes(n) ? "explicit" : DEMOGRAPHICS.includes(n) ? "demographics"
                   : GENRES.includes(n) ? "genres" : "themes";

const SWATCH = [["#c9683f","#5a2816"],["#4a6478","#1a2832"],["#c93f8a","#5a1640"],["#7a9a3f","#2e3a16"],
  ["#9a5a4f","#36201c"],["#3fa0b0","#163e46"],["#8a5ac9","#36205a"],["#3f5640","#111b12"],
  ["#5a3a3f","#160d0b"],["#3c4a63","#0e121b"],["#b08a3f","#3a2c12"],["#5b3a64","#170d1b"]];
const swatch = name => { let h=0; for (const c of name) h=(h*31+c.charCodeAt(0))&0xffff; return SWATCH[h%SWATCH.length]; };
const esc = s => String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");
const fmtCount = n => n >= 1000 ? (n/1000).toFixed(n>=10000?0:1)+"k" : String(n);

const GROUP_SUB = {
  "Genres": "the broad strokes",
  "Explicit Genres": "mature content",
  "Themes": "threads that cut across genres",
  "Demographics": "who they're written for"
};

async function getJSON(url){ const r = await fetch(url); if(!r.ok) throw new Error(r.status); return r.json(); }

async function topCovers(id){
  try { const j = await getJSON(`${JIKAN}/manga?genres=${id}&order_by=popularity&sort=asc&limit=10&sfw=true`);
    return (j.data||[]).map(m => (m.images && m.images.jpg && (m.images.jpg.large_image_url||m.images.jpg.image_url)) || "").filter(Boolean); }
  catch(e){ return []; }
}

(async () => {
  const j = await getJSON(`${JIKAN}/genres/manga`);
  const buckets = { genres:[], explicit:[], themes:[], demographics:[] }, seen = {};
  for (const g of j.data){ if(seen[g.name])continue; seen[g.name]=true;
    buckets[classOf(g.name)].push({ name:g.name, count:g.count||0, id:g.mal_id }); }
  const byName = (a,b)=> a.name<b.name?-1:1;
  buckets.genres.sort(byName); buckets.themes.sort(byName);

  // EVERY genre gets a representative cover (Hemanth: no bare tiles). 79 throttled calls (~1.05s apart
  // to stay under Jikan's 60/min + 3/sec). Explicit fetched sfw=true → may come back empty (gradient ok).
  const toFetch = [...buckets.genres, ...buckets.explicit, ...buckets.themes, ...buckets.demographics];
  console.log(`fetching ${toFetch.length} covers (throttled ~1.05s)…`);
  const used = new Set();                       // greedy global dedupe → each genre its OWN art
  for (let i=0;i<toFetch.length;i++){
    const covers = await topCovers(toFetch[i].id);
    toFetch[i].cover = covers.find(c=>!used.has(c)) || covers[0] || "";
    if (toFetch[i].cover) used.add(toFetch[i].cover);
    process.stdout.write(`\r  ${i+1}/${toFetch.length}`); await sleep(1050); }
  console.log("");

  const groups = [{ k:"Genres", g:buckets.genres },
                  { k:"Explicit Genres", g:buckets.explicit },
                  { k:"Themes", g:buckets.themes },
                  { k:"Demographics", g:buckets.demographics }];

  const tileHtml = t => {
    const s = swatch(t.name);
    const bg = t.cover
      ? `background-image:linear-gradient(rgba(0,0,0,.42),rgba(0,0,0,.52)),url('${esc(t.cover)}');background-size:cover;background-position:center 18%`
      : `background-image:linear-gradient(135deg,${s[0]},${s[1]})`;
    return `<a class="tile" style="${bg}"><span class="ct">${fmtCount(t.count)}</span><span class="gn">${esc(t.name)}</span></a>`;
  };
  const groupHtml = ({k,g}) => !g.length ? "" : `
      <section class="group">
        <div class="ghead"><h2>${esc(k)}</h2><span class="gcount">${g.length}</span><span class="gsub">${GROUP_SUB[k]||""}</span></div>
        <div class="mosaic">${g.map(tileHtml).join("")}</div>
      </section>`;
  const total = Object.values(buckets).reduce((n,b)=>n+b.length,0);

  const html = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Explore Genres — Tankoban (Colosseum mock)</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Fraunces:ital,opsz,wght@0,9..144,300..600;1,9..144,300..600&display=swap" rel="stylesheet">
<style>
  :root{ --gold:#f0c44a; --ink:#f7f7f5; --inkDim:#c9c8d0; --inkDimmer:#9a99a5;
    --edge:rgba(255,255,255,.18); --glass:rgba(255,255,255,.07); --glassHi:rgba(255,255,255,.12);
    --display:"Fraunces",Georgia,serif; --ui:"Segoe UI",system-ui,sans-serif; --margin:54px; }
  *{box-sizing:border-box;margin:0;padding:0}
  html,body{height:100%}
  body{font-family:var(--ui);color:var(--ink);background:#05060a;overflow:hidden;-webkit-font-smoothing:antialiased}
  @media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
  .wall{position:fixed;inset:0;z-index:0;background:
    radial-gradient(120% 90% at 12% 8%,#2a1f3e 0%,rgba(42,31,62,0) 46%),
    radial-gradient(90% 80% at 88% 14%,#103038 0%,rgba(16,48,56,0) 50%),
    radial-gradient(120% 120% at 70% 100%,#2e2018 0%,rgba(46,32,24,0) 55%),
    linear-gradient(160deg,#090810 0%,#05060a 60%,#040407 100%)}
  .vignette{position:fixed;inset:0;z-index:1;pointer-events:none;
    background:linear-gradient(180deg,rgba(0,0,0,.34),rgba(0,0,0,.06) 40%,rgba(0,0,0,.5))}
  .app{position:relative;z-index:2;height:100%;display:flex;flex-direction:column}
  /* back-bar (fullscreen-only chrome: back · search · minimize · power) */
  .bar{display:flex;align-items:center;height:52px;padding:0 var(--margin);gap:18px;flex:none}
  .back{width:42px;height:34px;border-radius:17px;background:rgba(0,0,0,.4);display:grid;place-items:center;
    color:var(--ink);font-size:22px;cursor:pointer}
  .back:hover{background:rgba(255,255,255,.16)}
  .sp{flex:1}
  .sys{display:flex;gap:18px;color:var(--inkDimmer)}
  .sys span{cursor:pointer}
  .sys span:hover{color:var(--ink)}
  .screen{flex:1;overflow:auto;padding:14px var(--margin) 50px}
  .screen::-webkit-scrollbar{width:10px}
  .screen::-webkit-scrollbar-thumb{background:rgba(255,255,255,.12);border-radius:8px}
  /* header */
  .eyebrow{font-family:var(--ui);font-size:12px;font-weight:600;letter-spacing:.2em;text-transform:uppercase;color:var(--inkDimmer)}
  .h1{font-family:var(--display);font-weight:480;font-size:clamp(40px,5vw,60px);line-height:1;letter-spacing:-.015em;margin:8px 0 0}
  .lede{font-family:var(--display);font-style:italic;font-weight:340;font-size:18px;color:var(--inkDim);margin:14px 0 6px}
  .lede b{font-style:normal;font-weight:600;color:var(--ink)}
  .rule{height:1px;background:linear-gradient(90deg,var(--edge),transparent 60%);margin:18px 0 0;position:relative}
  .rule::before{content:"";position:absolute;left:0;top:-1px;width:34px;height:3px;border-radius:2px;background:var(--gold)}
  /* groups */
  .group{margin-top:40px}
  .ghead{display:flex;align-items:baseline;gap:12px;margin-bottom:16px}
  .ghead h2{font-family:var(--display);font-weight:460;font-size:25px;letter-spacing:-.01em;color:var(--ink)}
  .gcount{font-family:var(--ui);font-size:12px;font-weight:700;color:var(--gold);background:rgba(240,196,74,.14);
    border:1px solid rgba(240,196,74,.4);border-radius:999px;padding:2px 9px}
  .gsub{font-family:var(--ui);font-size:13px;color:var(--inkDimmer);font-style:italic}
  .mosaic{display:grid;grid-template-columns:repeat(6,1fr);gap:14px}
  @media (max-width:1400px){.mosaic{grid-template-columns:repeat(5,1fr)}}
  @media (max-width:1080px){.mosaic{grid-template-columns:repeat(4,1fr)}}
  /* tile = the genre as its own art */
  .tile{position:relative;height:104px;border-radius:13px;overflow:hidden;border:1px solid var(--edge);
    display:block;text-decoration:none;cursor:pointer;transition:transform .14s,border-color .14s;
    box-shadow:0 10px 24px -14px rgba(0,0,0,.7)}
  .tile:hover{transform:translateY(-3px);border-color:var(--gold)}
  .tile .gn{position:absolute;left:13px;bottom:11px;right:36px;font-family:var(--display);font-weight:560;
    font-size:16px;line-height:1.05;color:#fff;text-shadow:0 1px 6px rgba(0,0,0,.7)}
  .tile .ct{position:absolute;right:11px;top:9px;font-family:var(--ui);font-size:11px;font-weight:600;
    color:rgba(255,255,255,.86);text-shadow:0 1px 4px rgba(0,0,0,.8)}
</style></head>
<body>
  <div class="wall"></div><div class="vignette"></div>
  <div class="app">
    <div class="bar">
      <div class="back">‹</div><div class="sp"></div>
      <div class="sys"><span>⌕</span><span>—</span><span>⏻</span></div>
    </div>
    <div class="screen">
      <div class="eyebrow">Tankoban · Manga</div>
      <h1 class="h1">Explore Genres</h1>
      <p class="lede"><b>${total}</b> genres, four ways in — by genre, theme, and who they're for.</p>
      <div class="rule"></div>
      ${groups.map(groupHtml).join("")}
    </div>
  </div>
</body></html>`;

  fs.writeFileSync("mocks/genre-index.html", html);
  console.log(`wrote mocks/genre-index.html — ${total} genres, ${toFetch.filter(t=>t.cover).length} covers`);
})();
