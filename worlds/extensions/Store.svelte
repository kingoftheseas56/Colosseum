<script>
  // Store — Hemanth's ledger (Brotherhood/agents/extensions-store-decision-ledger.md):
  // one carousel, three rows (Essentials, Most popular, Get something to watch), six banners; inside a banner the
  // crown jewels by popularity, then an App Store list; search is the power tool. Data: stremio-addons.net snapshot.
  import CATALOG, { SNAPSHOT } from './catalog.js';
  import { onMount } from 'svelte';

  let { store, host, installed } = $props();   // installed: Set of manifest ids and transport URLs

  let view = $state({ name: 'home' });          // home | category {key} | search
  let query = $state('');
  let sort = $state('popular');
  let type = $state('all');
  let chip = $state('all');
  let showAdult = $state(false);
  let slide = $state(0);
  let shown = $state(40);
  let working = $state({});                     // slug → true while an install is in flight

  const visible = $derived(CATALOG.filter(a => !a.trk && !a.r.includes('addon_catalog') && (showAdult || !a.c.includes('nsfw'))));
  const byName = n => visible.find(a => a.n === n);
  const isStream = a => a.r.includes('stream');
  const isVideo = a => a.t.some(t => t === 'movie' || t === 'series' || t === 'anime');

  // The six banners (views over the site's own categories; photos come later).
  const BANNERS = [
    { key: 'play', title: 'Play sources', line: 'Where your movies and shows actually stream from.', hue: [32, 18],
      test: a => isStream(a) && isVideo(a) && !a.c.includes('live tv'), chips: ['torrents', 'debrid support', 'usenet', 'http streams'] },
    { key: 'watch', title: 'Something to watch', line: 'New rows and catalogues for Theatre.', hue: [210, 250],
      test: a => a.r.includes('catalog') && !isStream(a) && !a.c.includes('live tv'), chips: ['movies', 'tv shows', 'anime'] },
    { key: 'anime', title: 'Anime', line: 'Seasons, catalogues and sources for anime.', hue: [340, 280],
      test: a => a.c.includes('anime') || a.t.includes('anime'), chips: ['torrents', 'debrid support', 'metadata'] },
    { key: 'world', title: 'World cinema', line: 'Korean, Chinese, Japanese and Indian film and drama.', hue: [20, 350],
      test: a => a.c.includes('asian drama') || a.c.includes('bollywood'), chips: ['asian drama', 'bollywood'] },
    { key: 'subs', title: 'Subtitles', line: 'Every language, matched to what is playing.', hue: [190, 220],
      test: a => a.r.includes('subtitles') || a.c.includes('subtitles'), chips: [] },
    { key: 'better', title: 'Better titles', line: 'Posters, ratings, episode orders and cleaner metadata.', hue: [45, 260],
      test: a => a.c.includes('metadata') && !isStream(a), chips: ['movies', 'tv shows', 'anime'] }
  ];

  const ESSENTIALS = ['OpenSubtitles v3', 'Streaming Catalogs', 'Comet | ElfHosted', 'Anime Kitsu', 'Torrentio'];
  const essentials = $derived(ESSENTIALS.map(byName).filter(Boolean));
  const popular = $derived(visible.filter(a => !ESSENTIALS.includes(a.n)).sort((x, y) => y.s - x.s).slice(0, 12));
  const toWatch = $derived(visible.filter(BANNERS[1].test).sort((x, y) => y.s - x.s).slice(0, 12));
  const fresh = $derived(visible.filter(a => a.cr >= '2026-09-07').sort((x, y) => y.s - x.s).slice(0, 5));

  const nice = n => n.replace(/\s*\|\s*ElfHosted$/i, '');
  const oneLine = a => { const s = (a.d || '').split(/(?<=[.!?])\s/)[0]; return s.length > 120 ? s.slice(0, 117).trimEnd() + '…' : s; };
  const stars = n => n >= 1000 ? (n / 1000).toFixed(1).replace('.0', '') + 'K' : String(n);
  const isInstalled = a => installed.has(a.id) || installed.has(a.u);
  const verb = a => working[a.slug] ? 'Adding' : isInstalled(a) ? 'Installed' : a.cfg ? 'Set up' : 'Install';

  function act(a, e) {
    e && e.stopPropagation();
    if (isInstalled(a) || working[a.slug]) return;
    if (a.cfg) { host.openExternal(a.cfg); return; }       // configure in the browser, then paste the link back
    working = { ...working, [a.slug]: true };
    store.install(a.u);
    setTimeout(() => { working = { ...working, [a.slug]: false }; }, 8000);
  }
  $effect(() => { for (const a of visible) if (working[a.slug] && isInstalled(a)) working = { ...working, [a.slug]: false }; });

  function open(next) { view = next; chip = 'all'; shown = 40; slide = 0; scrollTo({ top: 0 }); }
  // In-world Back: sub-views first, then the world closes (main.js asks this before host.back()).
  window.__storeBack = () => { if (view.name !== 'home' || query) { query = ''; open({ name: 'home' }); return true; } return false; };

  const banner = $derived(view.name === 'category' ? BANNERS.find(b => b.key === view.key) : null);
  const inBanner = $derived(banner ? visible.filter(banner.test).sort((x, y) => y.s - x.s) : []);
  const jewels = $derived(inBanner.slice(0, 3));
  const listed = $derived(inBanner.slice(3).filter(a => chip === 'all' || a.c.includes(chip)));

  const results = $derived.by(() => {
    const q = query.trim().toLowerCase();
    let r = visible.filter(a => !q || (a.n + ' ' + a.d + ' ' + a.c.join(' ') + ' ' + a.k.join(' ')).toLowerCase().includes(q));
    if (type !== 'all') r = r.filter(a => a.t.includes(type) || (type === 'series' && a.c.includes('tv shows')));
    const key = sort === 'new' ? 'cr' : sort === 'updated' ? 'up' : null;
    return r.sort((x, y) => key ? (y[key] > x[key] ? 1 : y[key] < x[key] ? -1 : y.s - x.s) : y.s - x.s);
  });

  onMount(() => {
    const t = setInterval(() => { if (view.name === 'home' && !query && fresh.length) slide = (slide + 1) % fresh.length; }, 8000);
    const k = e => { if (e.key === '/' && document.activeElement?.tagName !== 'INPUT') { e.preventDefault(); document.getElementById('store-q')?.focus(); } };
    addEventListener('keydown', k);
    return () => { clearInterval(t); removeEventListener('keydown', k); };
  });
</script>

{#snippet icon(a, size)}
  <span class="icon" style:width="{size}px" style:height="{size}px">
    {#if a.l}<img src={a.l} alt="" onerror={e => e.currentTarget.replaceWith(document.createTextNode(nice(a.n)[0]))}>{:else}{nice(a.n)[0]}{/if}
  </span>
{/snippet}

{#snippet button(a)}
  <button class="get" class:done={isInstalled(a)} type="button" data-focus disabled={isInstalled(a)} onclick={e => act(a, e)}>{verb(a)}</button>
{/snippet}

{#snippet feature(a)}
  <article class="feature">
    <span class="feature-bg" style:background-image={a.l ? `url("${a.l}")` : 'none'}></span>
    <span class="feature-shade"></span>
    <div class="feature-copy">
      {@render icon(a, 88)}
      <h2>{nice(a.n)}</h2>
      <p>{oneLine(a)}</p>
      <div class="feature-foot"><span class="stars">★ {stars(a.s)}</span>{@render button(a)}</div>
    </div>
  </article>
{/snippet}

{#snippet row(a)}
  <div class="row">
    {@render icon(a, 56)}
    <div class="row-copy"><strong>{nice(a.n)}</strong><span>{oneLine(a)}</span></div>
    <span class="stars">★ {stars(a.s)}</span>
    {@render button(a)}
  </div>
{/snippet}

<div class="store">
  <label class="search">
    <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="6.5"/><path d="m16 16 4.5 4.5"/></svg>
    <input id="store-q" bind:value={query} placeholder="Search {visible.length} add-ons" aria-label="Search add-ons" data-focus>
    <kbd>/</kbd>
  </label>

  <main class="page">
    {#if query.trim()}
      <section class="results">
        <h1>Results for “{query.trim()}”</h1>
        <div class="filters">
          <select bind:value={sort} data-focus aria-label="Sort">
            <option value="popular">Most popular</option><option value="new">Newest</option><option value="updated">Recently updated</option>
          </select>
          {#each [['all', 'Everything'], ['movie', 'Movies'], ['series', 'TV shows'], ['anime', 'Anime']] as [k, label]}
            <button type="button" class="chip" class:on={type === k} data-focus onclick={() => (type = k)}>{label}</button>
          {/each}
          <label class="adult"><input type="checkbox" bind:checked={showAdult} data-focus> Show adult add-ons</label>
        </div>
        <p class="count">{results.length} add-ons</p>
        <div class="list">{#each results.slice(0, shown) as a (a.slug)}{@render row(a)}{/each}</div>
        {#if results.length > shown}<button class="more" type="button" data-focus onclick={() => (shown += 40)}>Show more</button>{/if}
        {#if !results.length}<p class="empty">No add-on matches that. Try a site name, a language or a genre.</p>{/if}
      </section>

    {:else if view.name === 'category' && banner}
      <section class="category">
        <button class="back" type="button" data-focus onclick={() => open({ name: 'home' })}>Store</button>
        <h1>{banner.title}</h1>
        <p class="lede">{banner.line} {inBanner.length} add-ons, ranked by the community.</p>
        {#if jewels.length}
          <div class="carousel">
            {@render feature(jewels[slide % jewels.length])}
            {#if jewels.length > 1}
              <div class="dots">{#each jewels as j, i}<button type="button" class:on={i === slide % jewels.length} data-focus aria-label="Show {nice(j.n)}" onclick={() => (slide = i)}></button>{/each}</div>
            {/if}
          </div>
        {/if}
        {#if banner.chips.length}
          <div class="filters">
            <button type="button" class="chip" class:on={chip === 'all'} data-focus onclick={() => (chip = 'all')}>All</button>
            {#each banner.chips as c}<button type="button" class="chip" class:on={chip === c} data-focus onclick={() => (chip = c)}>{c[0].toUpperCase() + c.slice(1)}</button>{/each}
          </div>
        {/if}
        <div class="list">{#each listed.slice(0, shown) as a (a.slug)}{@render row(a)}{/each}</div>
        {#if listed.length > shown}<button class="more" type="button" data-focus onclick={() => (shown += 40)}>Show more</button>{/if}
      </section>

    {:else}
      {#if fresh.length}
        <section class="carousel home-carousel" aria-label="New this week">
          {@render feature(fresh[slide % fresh.length])}
          <div class="dots">{#each fresh as f, i}<button type="button" class:on={i === slide % fresh.length} data-focus aria-label="Show {nice(f.n)}" onclick={() => (slide = i)}></button>{/each}</div>
        </section>
      {/if}

      <section class="shelf">
        <h2>Essentials</h2>
        <p class="lede">The five a fresh Colosseum needs, in case you skipped the Chain.</p>
        <div class="tiles">
          {#each essentials as a (a.slug)}
            <div class="tile">{@render icon(a, 64)}<strong>{nice(a.n)}</strong><span>{oneLine(a)}</span><div class="tile-foot"><span class="stars">★ {stars(a.s)}</span>{@render button(a)}</div></div>
          {/each}
        </div>
      </section>

      <section class="shelf">
        <h2>Most popular</h2>
        <p class="lede">Ranked by stars on stremio-addons.net.</p>
        <div class="list two">{#each popular as a (a.slug)}{@render row(a)}{/each}</div>
      </section>

      <section class="shelf">
        <h2>Get something to watch</h2>
        <p class="lede">Catalogues that add new rows to Theatre.</p>
        <div class="list two">{#each toWatch as a (a.slug)}{@render row(a)}{/each}</div>
      </section>

      <section class="shelf">
        <h2>Browse the Store</h2>
        <div class="banners">
          {#each BANNERS as b}
            {@const top = visible.filter(b.test).sort((x, y) => y.s - x.s)}
            <button class="banner" type="button" data-focus onclick={() => open({ name: 'category', key: b.key })}
              style:--h1={b.hue[0]} style:--h2={b.hue[1]}>
              <span class="banner-logos">{#each top.slice(0, 4) as a}{@render icon(a, 72)}{/each}</span>
              <span class="banner-copy"><strong>{b.title}</strong><span>{b.line}</span><em>{top.length} add-ons</em></span>
            </button>
          {/each}
        </div>
      </section>
      <p class="credit">Community add-ons and stars from stremio-addons.net, {SNAPSHOT}. Their authors run them, not Colosseum.</p>
    {/if}
  </main>
</div>

<style>
  .store{--gold:#efc15a;--ink:#f6f5f1;--dim:rgba(255,255,255,.58);--edge:rgba(255,255,255,.1);min-height:100vh;color:var(--ink);font-family:"Segoe UI",system-ui,sans-serif;
    background:radial-gradient(1100px 620px at 50% -12%,rgba(239,193,90,.07),transparent 60%),linear-gradient(180deg,#0b0c11,#06070a 45%)}
  .page{width:min(1480px,calc(100vw - 120px));margin:0 auto;padding:118px 0 80px}
  .search{position:fixed;z-index:40;right:44px;top:34px;width:min(360px,26vw);height:46px;display:flex;align-items:center;gap:10px;padding:0 14px;border-radius:23px;border:1px solid rgba(255,255,255,.12);background:rgba(15,16,22,.8);backdrop-filter:blur(22px)}
  .search svg{width:16px;height:16px;fill:none;stroke:rgba(255,255,255,.5);stroke-width:1.8;stroke-linecap:round}
  .search input{flex:1;min-width:0;border:0;outline:0;background:none;color:#fff;font:inherit;font-size:14px}
  .search:focus-within{border-color:rgba(239,193,90,.55)}
  .search kbd{font-family:inherit;font-size:11px;line-height:1.4;color:rgba(255,255,255,.4);border:1px solid rgba(255,255,255,.14);border-radius:6px;padding:1px 7px}
  h1{font:500 48px/1.05 Georgia,"Times New Roman",serif;margin:0 0 8px;letter-spacing:-.02em}
  h2{font:500 30px/1.1 Georgia,"Times New Roman",serif;margin:0 0 4px}
  .lede{color:var(--dim);margin:0 0 20px;font-size:15px}
  .shelf{margin-top:56px}
  .icon{display:inline-grid;place-items:center;flex:0 0 auto;border-radius:22%;overflow:hidden;background:#1a1b21;border:1px solid var(--edge);font:600 22px Georgia,serif;color:var(--dim)}
  .icon img{width:100%;height:100%;object-fit:cover}
  .stars{color:var(--gold);font-size:13px;font-weight:600;white-space:nowrap}
  .get{height:34px;min-width:88px;padding:0 16px;border-radius:17px;border:0;background:#f5f3ee;color:#111217;font-family:inherit;font-weight:600;font-size:13px;cursor:pointer;white-space:nowrap}
  .get.done{background:rgba(255,255,255,.08);color:var(--dim);cursor:default}
  .get:focus-visible,.chip:focus-visible,.banner:focus-visible,.more:focus-visible,.back:focus-visible,.dots button:focus-visible,select:focus-visible{outline:2px solid var(--gold);outline-offset:3px}

  .carousel{position:relative}
  .feature{position:relative;height:400px;border-radius:30px;overflow:hidden;border:1px solid var(--edge);background:#101116}
  .feature-bg{position:absolute;inset:-40px;background-size:cover;background-position:center;filter:blur(46px) saturate(1.5) brightness(.55);transform:scale(1.2)}
  .feature-shade{position:absolute;inset:0;background:linear-gradient(90deg,rgba(7,8,11,.92) 0%,rgba(7,8,11,.55) 55%,rgba(7,8,11,.25))}
  .feature-copy{position:absolute;left:52px;bottom:46px;max-width:640px;display:flex;flex-direction:column;gap:14px}
  .feature-copy h2{font-size:52px;margin:6px 0 0}
  .feature-copy p{margin:0;color:#d9d6cd;font-size:17px;line-height:1.5}
  .feature-foot{display:flex;align-items:center;gap:18px}
  .dots{display:flex;gap:8px;justify-content:center;margin-top:14px}
  .dots button{width:26px;height:6px;border-radius:3px;border:0;background:rgba(255,255,255,.2);cursor:pointer;padding:0}
  .dots button.on{background:var(--gold)}

  .tiles{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:16px}
  .tile{display:flex;flex-direction:column;gap:10px;padding:20px;border-radius:22px;border:1px solid var(--edge);background:rgba(255,255,255,.035)}
  .tile strong{font-size:16px}
  .tile>span:not(.icon){color:var(--dim);font-size:13px;line-height:1.45;flex:1}
  .tile-foot{display:flex;align-items:center;justify-content:space-between;gap:10px}

  .list{display:grid;grid-template-columns:1fr;gap:2px}
  .list.two{grid-template-columns:repeat(2,minmax(0,1fr));column-gap:36px}
  .row{display:grid;grid-template-columns:56px minmax(0,1fr) auto auto;align-items:center;gap:16px;padding:12px 8px;border-bottom:1px solid rgba(255,255,255,.06)}
  .row-copy{min-width:0;display:flex;flex-direction:column;gap:3px}
  .row-copy strong{font-size:15px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .row-copy span{color:var(--dim);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

  .banners{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px;margin-top:18px}
  .banner{position:relative;height:230px;border-radius:26px;border:1px solid var(--edge);overflow:hidden;cursor:pointer;text-align:left;padding:0;color:inherit;
    background:radial-gradient(120% 100% at 100% 0%,hsl(var(--h1) 55% 32% / .75),transparent 60%),radial-gradient(90% 90% at 0% 100%,hsl(var(--h2) 45% 26% / .7),transparent 65%),#101116;transition:transform .18s ease,border-color .18s ease}
  .banner:hover{transform:translateY(-3px);border-color:rgba(255,255,255,.22)}
  .banner-logos{position:absolute;right:22px;top:22px;display:flex;gap:10px;opacity:.9}
  .banner-logos :global(.icon){box-shadow:0 12px 30px rgba(0,0,0,.35)}
  .banner-copy{position:absolute;left:28px;right:28px;bottom:24px;display:flex;flex-direction:column;gap:6px}
  .banner-copy strong{font:500 32px/1 Georgia,"Times New Roman",serif}
  .banner-copy span{color:#d4d1c8;font-size:14px}
  .banner-copy em{font-style:normal;color:var(--gold);font-size:13px;font-weight:600}

  .back{border:0;background:none;color:var(--dim);font:inherit;cursor:pointer;padding:0;margin-bottom:18px}
  .back:before{content:"‹  "}
  .category .carousel{margin:22px 0 30px}
  .filters{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:0 0 18px}
  .chip{height:36px;padding:0 16px;border-radius:18px;border:1px solid var(--edge);background:transparent;color:var(--dim);font:inherit;font-size:14px;cursor:pointer}
  .chip.on{background:rgba(255,255,255,.1);color:var(--ink)}
  select{height:36px;border-radius:18px;border:1px solid var(--edge);background:#15161c;color:var(--ink);font:inherit;padding:0 12px}
  .adult{color:var(--dim);font-size:14px;display:flex;align-items:center;gap:8px;margin-left:8px}
  .count{color:var(--dim);margin:0 0 6px}
  .more{margin:20px auto 0;display:block;height:40px;padding:0 22px;border-radius:20px;border:1px solid var(--edge);background:transparent;color:var(--ink);font:inherit;cursor:pointer}
  .empty{color:var(--dim);margin-top:30px}
  .credit{margin-top:60px;color:rgba(255,255,255,.34);font-size:12px}
  @media(max-width:1300px){.tiles{grid-template-columns:repeat(3,minmax(0,1fr))}.banners{grid-template-columns:repeat(2,minmax(0,1fr))}.list.two{grid-template-columns:1fr}.page{width:calc(100vw - 64px)}}
</style>
