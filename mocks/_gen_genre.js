// Generator: builds mocks/genre.html from jikan_cards.json (real Jikan/MAL data).
// Throwaway — documents the Jikan -> card mapping the QML port (GenreApi.js) will mirror.
const fs = require("fs");
const cards = JSON.parse(fs.readFileSync("jikan_cards.json", "utf8"));

const GENRE = "Adventure";
const GENRE_COUNT = "4,642";
const GENRE_DESC =
  "Whether aiming for a specific goal or just struggling to survive, the main character is thrust into " +
  "unfamiliar situations or lands and continuously faces unexpected dangers. The narrative is always on how " +
  "characters react to sudden trials during the journey — indicating personal growth or setback based on " +
  "the actions they take. Simply experiencing foreign worlds is not adventure; the change is.";

// sibling genres to hop to (the GenreMosaic continuity); current one is active
const SIBLINGS = ["Action", "Adventure", "Comedy", "Drama", "Fantasy", "Horror",
                  "Mystery", "Romance", "Sci-Fi", "Slice of Life", "Sports", "Supernatural"];

const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const flipName = n => {                                   // "Miura, Kentarou" -> "Kentarou Miura"
  const p = n.split(",").map(x => x.trim());
  return p.length === 2 ? `${p[1]} ${p[0]}` : n;
};
const members = m => m >= 1000 ? (m / 1000).toFixed(0) + "K" : String(m);
const counts = c => {                                    // "37 vol · 327 ch" / "ongoing"
  const bits = [];
  if (c.volumes) bits.push(c.volumes + " vol");
  if (c.chapters) bits.push(c.chapters + " ch");
  return bits.length ? bits.join(" · ") : (c.status === "Publishing" ? "ongoing" : "—");
};

const cardHtml = (c, i) => {
  const rank = String(i + 1).padStart(2, "0");
  const meta = [c.type, c.year, c.status].filter(Boolean).join(" · ");
  const chips = c.genres.map(g =>
    `<span class="chip${g === GENRE ? " on" : ""}">${esc(g)}</span>`).join("");
  const authors = (c.authors || []).map(flipName).join(", ");
  return `
      <article class="card" tabindex="0">
        <div class="rank">${rank}</div>
        <div class="cover"><img loading="lazy" src="${esc(c.cover)}" alt="${esc(c.title)} cover"></div>
        <div class="body">
          <h3 class="ct">${esc(c.title)}</h3>
          <div class="meta">${esc(meta)} <span class="dot">·</span> ${esc(counts(c))}</div>
          <div class="chips">${chips}</div>
          <p class="syn">${esc(c.synopsis)}…</p>
          <div class="foot">
            <span class="score"><svg class="star" viewBox="0 0 24 24"><path d="M12 2l2.9 6.3 6.9.7-5.1 4.6 1.5 6.8L12 17.3 5.8 20.4l1.5-6.8L2.2 9l6.9-.7z"/></svg>${c.score ?? "—"}</span>
            <span class="mem"><svg class="ic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><circle cx="12" cy="8" r="4"/><path d="M4 21c0-4 3.5-6 8-6s8 2 8 6" stroke-linecap="round"/></svg>${members(c.members || 0)}</span>
            <span class="au">${esc(authors)}</span>
            <button class="add" title="Add to Library">+ Library</button>
          </div>
        </div>
      </article>`;
};

const siblingHtml = SIBLINGS.map(g =>
  `<span class="gpill${g === GENRE ? " on" : ""}">${esc(g)}</span>`).join("");

// montage = the genre's own top covers, washed behind the hero
const montage = cards.slice(0, 7).map(c =>
  `<div class="mtile" style="background-image:url('${esc(c.cover)}')"></div>`).join("");

const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${GENRE} — genre browse (Colosseum mock)</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Fraunces:ital,opsz,wght@0,9..144,300..600;1,9..144,300..600&display=swap" rel="stylesheet">
<style>
  /* ── Colosseum tokens (Theme.qml) ── */
  :root{
    --gold:#f0c44a; --goldDim:rgba(240,196,74,.16);
    --ink:#f7f7f5; --inkDim:#c9c8d0; --inkDimmer:#9a99a5;
    --edge:rgba(255,255,255,.18);
    --glass:rgba(255,255,255,.07); --glassHi:rgba(255,255,255,.11);
    --display:"Fraunces", Georgia, serif;
    --ui:"Segoe UI", system-ui, sans-serif;
    --margin:54px;
  }
  *{box-sizing:border-box; margin:0; padding:0}
  html,body{height:100%}
  body{font-family:var(--ui); color:var(--ink); background:#05060a; overflow:hidden; -webkit-font-smoothing:antialiased}
  @media (prefers-reduced-motion: reduce){ *{animation:none !important; transition:none !important} }

  .wall{position:fixed; inset:0; z-index:0;
    background:
      radial-gradient(120% 90% at 12% 8%, #2a1f3e 0%, rgba(42,31,62,0) 46%),
      radial-gradient(90% 80% at 88% 14%, #103038 0%, rgba(16,48,56,0) 50%),
      radial-gradient(120% 120% at 70% 100%, #2e2018 0%, rgba(46,32,24,0) 55%),
      linear-gradient(160deg, #090810 0%, #05060a 60%, #040407 100%);}
  .vignette{position:fixed; inset:0; z-index:1; pointer-events:none;
    background:linear-gradient(180deg, rgba(0,0,0,.34), rgba(0,0,0,.08) 42%, rgba(0,0,0,.55));}
  .app{position:relative; z-index:2; height:100%; display:flex; flex-direction:column}

  /* ── glass top bar ── */
  .topbar{display:flex; align-items:center; gap:18px; height:64px; margin:22px var(--margin) 0; padding:0 16px;
    background:var(--glass); border:1px solid var(--edge); border-radius:16px;
    backdrop-filter:blur(22px) saturate(1.1); -webkit-backdrop-filter:blur(22px) saturate(1.1);
    box-shadow:0 1px 0 rgba(255,255,255,.05) inset}
  .back{font-family:var(--ui); font-size:14px; color:var(--inkDim); cursor:pointer; display:flex; align-items:center; gap:7px;
    padding:8px 12px; border-radius:10px; transition:color .15s, background .15s}
  .back:hover{color:var(--ink); background:rgba(255,255,255,.06)}
  .wordmark{font-family:var(--display); font-weight:500; font-size:20px; letter-spacing:.2px}
  .pill{font-family:var(--ui); font-size:13px; color:var(--inkDim); padding:7px 14px; border-radius:999px; cursor:pointer}
  .pill.on{background:var(--glassHi); color:var(--ink); border:1px solid var(--edge)}
  .spacer{flex:1}
  .sys{display:flex; gap:6px; color:var(--inkDimmer)}
  .sys span{width:30px; height:30px; display:grid; place-items:center; border-radius:8px; cursor:pointer}
  .sys span:hover{background:rgba(255,255,255,.07); color:var(--ink)}

  .screen{flex:1; overflow:auto}
  .screen::-webkit-scrollbar{width:10px}
  .screen::-webkit-scrollbar-thumb{background:rgba(255,255,255,.12); border-radius:8px}

  /* ══ HERO — the genre as its own art ══ */
  .hero{position:relative; margin:26px var(--margin) 0; border-radius:22px; overflow:hidden;
    border:1px solid var(--edge); min-height:268px; display:flex; align-items:flex-end}
  .montage{position:absolute; inset:0; display:flex; z-index:0}
  .montage .mtile{flex:1; background-size:cover; background-position:center 22%; filter:saturate(.92)}
  .hero::before{content:""; position:absolute; inset:0; z-index:1;
    background:linear-gradient(105deg, rgba(8,7,14,.97) 28%, rgba(8,7,14,.62) 58%, rgba(8,7,14,.42) 100%),
               linear-gradient(0deg, rgba(8,7,14,.9), rgba(8,7,14,0) 60%)}
  .hero-in{position:relative; z-index:2; padding:34px 40px 30px; max-width:760px}
  .eyebrow{font-family:var(--ui); font-size:12px; font-weight:600; letter-spacing:.18em; text-transform:uppercase;
    color:var(--inkDimmer)}
  .eyebrow b{color:var(--gold); font-weight:700}
  .gname{font-family:var(--display); font-weight:480; font-size:clamp(46px,6vw,76px); line-height:.98;
    letter-spacing:-.018em; margin:10px 0 0; font-optical-sizing:auto}
  .standfirst{font-family:var(--display); font-style:italic; font-weight:360; font-size:clamp(16px,1.6vw,19.5px);
    line-height:1.5; color:var(--inkDim); margin:16px 0 0; max-width:60ch}
  .heroline{display:flex; align-items:center; gap:16px; margin-top:22px}
  .heroline .gold-rule{width:34px; height:3px; border-radius:2px; background:var(--gold)}
  .heroline .ct{font-family:var(--ui); font-size:13.5px; color:var(--inkDim); letter-spacing:.02em}
  .heroline .ct b{color:var(--ink); font-weight:600}

  /* ── sibling-genre hop row ── */
  .genres{display:flex; gap:9px; flex-wrap:wrap; margin:20px var(--margin) 0}
  .gpill{font-family:var(--ui); font-size:13px; color:var(--inkDim); padding:7px 14px; border-radius:999px;
    background:var(--glass); border:1px solid var(--edge); cursor:pointer; transition:background .14s, color .14s}
  .gpill:hover{background:var(--glassHi); color:var(--ink)}
  .gpill.on{background:var(--goldDim); border-color:rgba(240,196,74,.5); color:var(--gold); font-weight:600}

  /* ── listing controls ── */
  .controls{display:flex; align-items:center; margin:26px var(--margin) 16px}
  .controls .lbl{font-family:var(--ui); font-size:12px; font-weight:600; letter-spacing:.14em; text-transform:uppercase; color:var(--inkDimmer)}
  .controls .spacer{flex:1}
  .sort{font-family:var(--ui); font-size:13px; color:var(--inkDim); display:flex; align-items:center; gap:8px;
    padding:8px 14px; border-radius:10px; background:var(--glass); border:1px solid var(--edge); cursor:pointer}
  .sort b{color:var(--ink); font-weight:600}
  .view{display:flex; margin-left:12px; border:1px solid var(--edge); border-radius:10px; overflow:hidden}
  .view span{width:36px; height:36px; display:grid; place-items:center; color:var(--inkDimmer); cursor:pointer; background:var(--glass)}
  .view span.on{background:var(--glassHi); color:var(--ink)}
  .view span+span{border-left:1px solid var(--edge)}

  /* ══ the rich card grid ══ */
  .grid{display:grid; grid-template-columns:repeat(3,1fr); gap:18px; padding:0 var(--margin) 44px}
  @media (max-width:1180px){ .grid{grid-template-columns:repeat(2,1fr)} }
  @media (max-width:760px){ .grid{grid-template-columns:1fr} }

  .card{position:relative; display:grid; grid-template-columns:96px 1fr; gap:16px; padding:16px;
    background:var(--glass); border:1px solid var(--edge); border-radius:16px;
    backdrop-filter:blur(18px) saturate(1.06); -webkit-backdrop-filter:blur(18px) saturate(1.06);
    transition:transform .16s, background .16s, border-color .16s; cursor:pointer; outline:none}
  .card:hover,.card:focus-visible{transform:translateY(-3px); background:var(--glassHi); border-color:rgba(255,255,255,.28)}
  .rank{position:absolute; top:12px; right:16px; font-family:var(--display); font-weight:430; font-size:15px;
    color:var(--inkDimmer); letter-spacing:.04em; opacity:.7}
  .cover{width:96px; height:140px; border-radius:8px; overflow:hidden; background:#14131a;
    box-shadow:0 12px 26px -14px rgba(0,0,0,.8); align-self:start}
  .cover img{width:100%; height:100%; object-fit:cover; display:block}
  .body{min-width:0; display:flex; flex-direction:column}
  .ct{font-family:var(--display); font-weight:500; font-size:18.5px; line-height:1.12; letter-spacing:-.01em;
    color:var(--ink); padding-right:26px; overflow:hidden; display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical}
  .meta{font-family:var(--ui); font-size:12px; color:var(--inkDimmer); margin-top:6px}
  .meta .dot{opacity:.5; margin:0 3px}
  .chips{display:flex; flex-wrap:wrap; gap:5px; margin-top:9px}
  .chip{font-family:var(--ui); font-size:10.5px; letter-spacing:.02em; color:var(--inkDim);
    padding:3px 8px; border-radius:6px; background:rgba(255,255,255,.05); border:1px solid rgba(255,255,255,.10)}
  .chip.on{background:var(--goldDim); border-color:rgba(240,196,74,.45); color:var(--gold)}
  .syn{font-family:var(--display); font-weight:340; font-size:13px; line-height:1.5; color:var(--inkDim);
    margin-top:10px; overflow:hidden; display:-webkit-box; -webkit-line-clamp:3; -webkit-box-orient:vertical}
  .foot{display:flex; align-items:center; gap:13px; margin-top:auto; padding-top:12px; flex-wrap:wrap}
  .score{display:flex; align-items:center; gap:5px; font-family:var(--ui); font-size:13.5px; font-weight:600; color:var(--ink)}
  .star{width:14px; height:14px; fill:var(--gold)}
  .mem{display:flex; align-items:center; gap:5px; font-family:var(--ui); font-size:12.5px; color:var(--inkDimmer)}
  .mem .ic{width:14px; height:14px}
  .au{font-family:var(--ui); font-size:12px; color:var(--inkDimmer); flex:1; min-width:0;
    overflow:hidden; text-overflow:ellipsis; white-space:nowrap}
  .add{font-family:var(--ui); font-size:12.5px; font-weight:600; color:var(--inkDim); cursor:pointer;
    padding:7px 12px; border-radius:9px; background:rgba(255,255,255,.05); border:1px solid var(--edge);
    transition:background .14s, color .14s}
  .add:hover{background:var(--goldDim); border-color:rgba(240,196,74,.5); color:var(--gold)}
</style>
</head>
<body>
  <div class="wall"></div>
  <div class="vignette"></div>

  <div class="app">
    <header class="topbar">
      <div class="back">‹ Home</div>
      <div class="wordmark">Tankoban</div>
      <div class="pill on">Manga</div>
      <div class="pill">Comics</div>
      <div class="spacer"></div>
      <div class="sys"><span>—</span><span>⏻</span></div>
    </header>

    <main class="screen">
      <!-- HERO: the genre as its own art -->
      <section class="hero">
        <div class="montage">${montage}</div>
        <div class="hero-in">
          <div class="eyebrow">Tankoban · Manga · <b>Genre</b></div>
          <h1 class="gname">${GENRE}</h1>
          <p class="standfirst">${GENRE_DESC}</p>
          <div class="heroline">
            <span class="gold-rule"></span>
            <span class="ct"><b>${GENRE_COUNT}</b> titles · sorted by readers</span>
          </div>
        </div>
      </section>

      <!-- sibling-genre hop -->
      <nav class="genres">${siblingHtml}</nav>

      <!-- listing controls -->
      <div class="controls">
        <span class="lbl">Most read</span>
        <span class="spacer"></span>
        <div class="sort">Sorted by <b>Readers</b> ▾</div>
        <div class="view">
          <span class="on" title="Detailed">☷</span>
          <span title="Covers">▦</span>
        </div>
      </div>

      <!-- the cards -->
      <section class="grid">${cards.map(cardHtml).join("")}
      </section>
    </main>
  </div>
</body>
</html>`;

fs.writeFileSync("mocks/genre.html", html);
console.log("wrote mocks/genre.html (" + (html.length / 1024).toFixed(1) + " KB, " + cards.length + " cards)");
