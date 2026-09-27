<script>
  // Chain — Hemanth's mock, 1:1 (colosseum-chain-scroll-colosseum-aesthetic.html). Only the install state is live.
  const A = 'surfaces/extensions-hub/assets/';
  let { addons = {}, pending = {}, loading = false, ontoggle } = $props();

  const OPTIONAL = [
    { key: 'getcomics', x: 1, name: 'GetComics', desc: 'Comic source', connect: ['comics'], logo: 'getcomics.png' },
    { key: 'tankoyomi', x: 17.2, name: 'Tankoyomi', desc: 'Chapter source', connect: ['manga'], logo: 'tankoyomi.png' },
    { key: 'tankorent', x: 34, w: 16, name: 'Tankorent', desc: 'Manga & books', connect: ['manga', 'books'], logo: 'tankorent.png', shared: true },
    { key: 'libgen', x: 50.2, name: 'LibGen', desc: 'Book source', connect: ['books'], logo: 'libgen.ico' },
    { key: 'audiobookbay', x: 66.4, name: 'AudioBookBay', desc: 'Audiobook source', connect: ['audiobook'], logo: 'audiobookbay.png' },
    { key: 'torrentio', x: 82.6, name: 'Torrentio', desc: 'Video source', connect: ['tv', 'movies'], logo: 'torrentio.png' }
  ];
  const WIRES = [
    ['getcomics', 'optional', 'M185 516 C185 564 90 614 90 632'], ['tankoyomi', 'optional', 'M185 516 C185 564 250 564 250 632'],
    ['tankorent', 'shared', 'M185 516 C185 618 420 592 420 632'], ['tankorent', 'shared', 'M500 516 C500 618 420 620 420 632'],
    ['libgen', 'optional', 'M500 516 C500 564 580 614 580 632'], ['audiobookbay', 'optional', 'M500 516 C500 564 740 564 740 632'],
    ['torrentio', 'optional', 'M815 516 C815 564 900 614 900 632'],
    ['getcomics', 'optional', 'M90 756 C90 806 78 812 78 856'], ['tankoyomi', 'optional', 'M250 756 V856'],
    ['tankorent', 'optional', 'M420 756 C420 805 250 810 250 856'], ['tankorent', 'optional', 'M420 756 V856'],
    ['libgen', 'optional', 'M580 756 C580 805 420 810 420 856'], ['audiobookbay', 'optional', 'M740 756 C740 805 590 810 590 856'],
    ['torrentio', 'optional', 'M900 756 C900 805 760 810 760 856'], ['torrentio', 'optional', 'M900 756 C900 805 925 810 925 856']
  ];
  const ENDPOINTS = [
    { key: 'comics', x: 2, label: 'COMICS' }, { key: 'manga', x: 19, label: 'MANGA' }, { key: 'books', x: 36, label: 'BOOKS' },
    { key: 'audiobook', x: 53, label: 'AUDIOBOOK' }, { key: 'tv', x: 70, label: 'TV' }, { key: 'movies', x: 86.5, label: 'MOVIES' }
  ];

  const on = key => !!(addons[key] && addons[key].installed);
  const unlocked = $derived(Object.fromEntries(ENDPOINTS.map(e => [e.key, OPTIONAL.some(o => o.connect.includes(e.key) && on(o.key))])));

  // Energy flows once when an add-on wakes, then rests (the mock's .energizing pass).
  let energizing = $state({});
  let seen = {};
  $effect(() => {
    for (const o of OPTIONAL) {
      const now = on(o.key);
      if (seen[o.key] === false && now) {
        energizing = { ...energizing, [o.key]: true };
        setTimeout(() => { energizing = { ...energizing, [o.key]: false }; }, 900);
      }
      if (!loading) seen[o.key] = now;
    }
  });
</script>

<div class="colosseum-reveal" aria-hidden="true"
  style:--medium-comics={unlocked.comics ? 1 : 0} style:--medium-manga={unlocked.manga ? 1 : 0}
  style:--medium-books={unlocked.books ? 1 : 0} style:--medium-audiobook={unlocked.audiobook ? 1 : 0}
  style:--medium-tv={unlocked.tv ? 1 : 0} style:--medium-movies={unlocked.movies ? 1 : 0}>
  <img class="base-photo" src={A + 'colosseum-night-cc0-1920x1080.jpg'} alt="">
  {#each ENDPOINTS as e}<img class="medium-light medium-{e.key}" src={A + 'colosseum-night-cc0-1920x1080.jpg'} alt="">{/each}
</div>

<div class="frame"><main class="scroll">
  <svg class="wires" viewBox="0 0 1000 1414" preserveAspectRatio="none" aria-hidden="true">
    <path class="core wire-link active" d="M500 129 C500 208 185 160 185 196"/><path class="core wire-link active" d="M500 129 V196"/><path class="core wire-link active" d="M500 129 C500 208 815 160 815 196"/><path class="core wire-link active" d="M185 321 V391"/><path class="core wire-link active" d="M500 321 V391"/><path class="core wire-link active" d="M815 321 V391"/>
    {#each WIRES as [key, kind, d]}<path class="{kind} wire-link" class:active={on(key)} class:energizing={energizing[key]} {d}/>{/each}
  </svg>

  <section class="node core root" style="--x:29;--y:1.5;--w:42;--h:7.8"><span class="iconwrap"><svg class="repo-inline" viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-linecap="round"><path d="M10 32.5h28" stroke-width="2.6"/><path d="M14 32.5V20.8c0-5.4 4.5-9.8 10-9.8s10 4.4 10 9.8v11.7" stroke-width="2.6"/><path d="M18 32.5V20.9c0-3.1 2.7-5.7 6-5.7s6 2.6 6 5.7v11.6" stroke-width="2.1"/><path d="M22.2 32.5v-9.9c0-1.1.8-2 1.8-2s1.8.9 1.8 2v9.9" stroke-width="1.8"/><path d="M8.5 37h31" stroke-width="2.6"/></svg></span><span class="copy"><span class="name">Colosseum App</span><span class="desc">The root layer</span></span><span class="status">ROOT</span></section>
  <section class="node core" style="--x:5;--y:13.9;--w:27;--h:8.8"><span class="iconwrap"><svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5h16v11H8l-4 3z"/></svg></span><span class="copy"><span class="name">Tankoban</span><span class="desc">Comics & manga</span></span><span class="status">BUILT-IN</span></section>
  <section class="node core" style="--x:36.5;--y:13.9;--w:27;--h:8.8"><span class="iconwrap"><svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h7v16H4zM13 4h7v16h-7zM11 4v16"/></svg></span><span class="copy"><span class="name">Biblio</span><span class="desc">Books & audiobooks</span></span><span class="status">BUILT-IN</span></section>
  <section class="node core" style="--x:68;--y:13.9;--w:27;--h:8.8"><span class="iconwrap"><svg class="repo-inline" viewBox="0 0 64 64" fill="none" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="20" cy="18" r="8"/><circle cx="38" cy="16" r="6"/><path d="M12 28h35a5 5 0 0 1 5 5v13H12z"/><circle cx="41" cy="37" r="5"/><path d="M52 35l8-5v19l-8-5z"/><path d="M18 46v7M46 46v7"/></svg></span><span class="copy"><span class="name">Theatre</span><span class="desc">Film, shows & anime</span></span><span class="status">BUILT-IN</span></section>
  <section class="node core" style="--x:5;--y:27.7;--w:27;--h:8.8"><span class="iconwrap"><img class="logo" src={A + 'colosseum-grand-database.png'} alt=""></span><span class="copy"><span class="name">Colosseum Database</span><span class="desc">Tankoban foundation</span></span><span class="status">BUILT-IN</span></section>
  <section class="node core" style="--x:36.5;--y:27.7;--w:27;--h:8.8"><span class="iconwrap"><img class="logo" src={A + 'applebooks.ico'} alt=""></span><span class="copy"><span class="name">Apple Books</span><span class="desc">Biblio foundation</span></span><span class="status">BUILT-IN</span></section>
  <section class="node core" style="--x:68;--y:27.7;--w:27;--h:8.8"><span class="iconwrap"><span class="cinemeta">C</span></span><span class="copy"><span class="name">Cinemeta</span><span class="desc">Theatre catalogue</span></span><span class="status">BUILT-IN</span></section>

  {#each OPTIONAL as o}
    <button class="node optional" class:shared={o.shared} class:sim-installed={on(o.key)} style="--x:{o.x};--y:44.7;--w:{o.w || 15.5};--h:8.8"
      type="button" data-focus data-key={'chain:' + o.key} disabled={loading || !addons[o.key] || !addons[o.key].present} aria-busy={!!pending[o.key]}
      aria-label="{o.name}, {on(o.key) ? 'installed' : 'not installed'}" onclick={() => ontoggle(o.key)}>
      <span class="iconwrap"><img class="logo" src={A + o.logo} alt=""></span>
      <span class="copy"><span class="name">{o.name}</span><span class="desc">{o.desc}</span></span>
      <span class="status">{pending[o.key] ? '…' : on(o.key) ? 'INSTALLED' : 'INSTALL'}</span>
    </button>
  {/each}

  {#each ENDPOINTS as e}
    <div class="endpoint" class:unlocked={unlocked[e.key]} style="--x:{e.x};--y:60.7;--w:11.5">
      {#if e.key === 'comics'}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="3" y="3" width="8" height="8" rx="1.5"/><rect x="13" y="3" width="8" height="8" rx="1.5"/><rect x="3" y="13" width="8" height="8" rx="1.5"/><rect x="13" y="13" width="8" height="8" rx="1.5"/></svg>
      {:else if e.key === 'manga'}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M4 5h16v11H8l-4 3z"/></svg>
      {:else if e.key === 'books'}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M4 4h7v16H4zM13 4h7v16h-7zM11 4v16"/></svg>
      {:else if e.key === 'audiobook'}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1v-6h3zM3 19a2 2 0 0 0 2 2h1v-6H3z"/></svg>
      {:else if e.key === 'tv'}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="6" width="18" height="13" rx="2"/><path d="m8 3 4 3 4-3"/></svg>
      {:else}<svg class="repo-inline" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="M3 9h18M8 5v14M16 5v14"/></svg>{/if}
      <strong>{e.label}</strong>
    </div>
  {/each}
</main></div>

<style>
  /* Carried over from the mock; only .frame's top padding changed (the picker sits under the app's TopBar). */
  .colosseum-reveal{--medium-comics:0;--medium-manga:0;--medium-books:0;--medium-audiobook:0;--medium-tv:0;--medium-movies:0;position:fixed;z-index:0;inset:0;pointer-events:none;overflow:hidden;background:#05070a}
  .colosseum-reveal img{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;object-position:center 50%;display:block;transform:scale(1.012)}
  .colosseum-reveal .base-photo{filter:brightness(.29) saturate(.66) contrast(.93)}
  .colosseum-reveal .medium-light{filter:brightness(1.04) saturate(1.04) contrast(1.04);transition:opacity .75s ease}
  .colosseum-reveal .medium-comics{opacity:calc(var(--medium-comics)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 60% at 4% 58%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 60% at 4% 58%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal .medium-manga{opacity:calc(var(--medium-manga)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 60% at 22% 59%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 60% at 22% 59%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal .medium-books{opacity:calc(var(--medium-books)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 58% at 41% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 58% at 41% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal .medium-audiobook{opacity:calc(var(--medium-audiobook)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 58% at 59% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 58% at 59% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal .medium-tv{opacity:calc(var(--medium-tv)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 58% at 78% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 58% at 78% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal .medium-movies{opacity:calc(var(--medium-movies)*.88);-webkit-mask-image:radial-gradient(ellipse 24% 58% at 96% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%);mask-image:radial-gradient(ellipse 24% 58% at 96% 60%,#000 0%,rgba(0,0,0,.96) 34%,rgba(0,0,0,.56) 67%,transparent 100%)}
  .colosseum-reveal:after{content:"";position:absolute;inset:0;background:radial-gradient(ellipse at 50% 42%,rgba(0,0,0,.14) 0%,rgba(0,0,0,.24) 48%,rgba(0,0,0,.52) 100%),linear-gradient(180deg,rgba(4,5,8,.26),rgba(4,5,8,.10) 42%,rgba(4,5,8,.34))}
  .frame{--gold:#f0c44a;--ivory:#f7f7f5;--edge:rgba(255,255,255,.15);--display:Georgia,"Times New Roman",serif;width:100%;min-width:760px;position:relative;z-index:1;margin:0;padding-top:22px;font-family:"Segoe UI",system-ui,sans-serif;color:var(--ivory)}
  .scroll{position:relative;container-type:inline-size;width:100%;height:clamp(820px,110vh,1200px);overflow:hidden}
  .wires{position:absolute;inset:0;width:100%;height:100%;z-index:4;pointer-events:none}
  .wires path{fill:none;stroke-linecap:round;stroke-linejoin:round}
  .wires .core{stroke:rgba(240,196,74,.88);stroke-width:3}
  .wires .optional,.wires .shared{stroke:rgba(213,209,199,.34);stroke-width:2.5}
  .wires .wire-link{transition:stroke .25s ease,filter .25s ease,opacity .25s ease;opacity:.64}
  .wires .wire-link.active{stroke:var(--gold);opacity:1;filter:drop-shadow(0 0 6px rgba(240,196,74,.74))}
  .wires .wire-link.energizing{stroke:var(--gold);opacity:1;filter:drop-shadow(0 0 7px rgba(240,196,74,.86));stroke-dasharray:11 10;animation:current .82s linear 1}
  @keyframes current{to{stroke-dashoffset:-24}}
  .node{--x:0;--y:0;--w:20;--h:8;position:absolute;z-index:7;left:calc(var(--x)*1%);top:calc(var(--y)*1%);width:calc(var(--w)*1%);height:calc(var(--h)*1%);padding:1.12cqw 1.05cqw;border-radius:1.25cqw;display:flex;align-items:center;gap:1cqw;color:var(--ivory);border:1px solid var(--edge);background:linear-gradient(145deg,rgba(17,18,21,.92),rgba(5,6,8,.82));box-shadow:0 .55cqw 1.45cqw #0009,inset 0 1px 0 rgba(255,255,255,.055),inset 0 0 30px rgba(255,255,255,.012);backdrop-filter:blur(20px) saturate(115%)}
  .node.core{border-color:rgba(240,196,74,.44);box-shadow:0 0 1.5cqw rgba(240,196,74,.10),0 .55cqw 1.45cqw #0009,inset 0 1px 0 rgba(255,255,255,.08)}
  .node.core:after{content:"";position:absolute;inset:.32cqw;border-radius:.94cqw;border:1px solid rgba(240,196,74,.13);pointer-events:none}
  .node.root{background:linear-gradient(145deg,rgba(32,27,18,.96),rgba(8,8,9,.89));border-color:rgba(240,196,74,.72);box-shadow:0 0 2.25cqw rgba(240,196,74,.18),0 .7cqw 1.8cqw #000b,inset 0 1px 0 rgba(255,255,255,.09)}
  .node.optional{appearance:none;cursor:pointer;flex-direction:column;justify-content:center;text-align:center;gap:.34cqw;padding:1.25cqw .55cqw .65cqw;border-color:rgba(255,255,255,.105);background:linear-gradient(145deg,rgba(11,12,15,.91),rgba(4,5,7,.88));transition:transform .18s ease,border-color .18s ease,box-shadow .18s ease,background .18s ease}
  .node.optional:hover,:global(body.keys) .node.optional:focus{transform:translateY(-.28cqw);border-color:rgba(240,196,74,.42);box-shadow:0 .8cqw 1.7cqw #000c,0 0 1cqw rgba(240,196,74,.10)}
  :global(body.keys) .node.optional:focus{outline:2px solid rgba(240,196,74,.9);outline-offset:3px}
  .node.optional:disabled{cursor:default}
  .node.optional.sim-installed{background:linear-gradient(145deg,rgba(36,28,14,.93),rgba(8,8,9,.90));border-color:rgba(240,196,74,.72);box-shadow:0 0 1.45cqw rgba(240,196,74,.20),0 .65cqw 1.5cqw #000a,inset 0 1px 0 rgba(255,255,255,.07)}
  .node.shared:before{content:"SHARED";position:absolute;top:-1.02cqw;left:50%;transform:translateX(-50%);padding:.2cqw .65cqw;border-radius:999px;background:#111216;color:var(--gold);font-size:.61cqw;letter-spacing:.14em;border:1px solid rgba(240,196,74,.42);box-shadow:0 0 .8cqw #000}
  .node .iconwrap{flex:0 0 auto;width:4.55cqw;height:4.55cqw;display:grid;place-items:center;border-radius:1cqw;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.07)}
  .node.root .iconwrap{background:rgba(240,196,74,.07);border-color:rgba(240,196,74,.2)}
  .node.optional .iconwrap{width:4.15cqw;height:4.15cqw;background:rgba(255,255,255,.035)}
  .logo{max-width:82%;max-height:82%;object-fit:contain}
  .node.optional .logo{filter:grayscale(.9) saturate(.4) brightness(.72);opacity:.66}
  .node.optional:hover .logo,.node.optional.sim-installed .logo{filter:none;opacity:1}
  .repo-inline{display:block;width:72%;height:72%;color:#ddd7ca;overflow:visible}
  .repo-inline :global(*){vector-effect:non-scaling-stroke}
  .node.core .repo-inline{color:var(--gold)}
  .cinemeta{font:600 2.1cqw var(--display);color:var(--gold)}
  .node .copy{min-width:0;line-height:1.05;display:flex;flex-direction:column;gap:.34cqw;overflow:hidden}
  .node .name{display:block;font:600 1.48cqw/1 var(--display);letter-spacing:.025em;white-space:normal}
  .node .desc{display:block;font-size:.71cqw;color:#aaa59b;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .node.optional .copy{width:100%;align-items:center}
  .node.optional .name{font:600 1.1cqw/1.02 var(--display);letter-spacing:.03em}
  .node.optional .desc{font-size:.68cqw;color:#7f7b73}
  .node.root .name{font-size:2.15cqw}
  .node.root .desc{font-size:.77cqw}
  .status{position:absolute;right:.66cqw;top:.58cqw;font-size:.56cqw;letter-spacing:.13em;text-transform:uppercase;color:#7e7970}
  .node.core .status{color:rgba(240,196,74,.72)}
  .node.optional.sim-installed .status{color:var(--gold)}
  .endpoint{--x:0;--y:0;--w:11.5;position:absolute;z-index:8;left:calc(var(--x)*1%);top:calc(var(--y)*1%);width:calc(var(--w)*1%);height:10.8%;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:.55cqw;color:#a6a199;background:linear-gradient(145deg,rgba(22,23,26,.88),rgba(6,7,9,.84));border:1px solid rgba(255,255,255,.14);box-shadow:inset 0 1px 0 rgba(255,255,255,.04),0 .6cqw 1.35cqw #0009;filter:saturate(.45);opacity:.86;transition:color .25s ease,border-color .25s ease,background .25s ease,box-shadow .25s ease,filter .25s ease,opacity .25s ease,transform .25s ease}
  .endpoint.unlocked{color:#ffe477;border:1.5px solid rgba(255,224,113,.98);filter:none;opacity:1;transform:scale(1.035);background:linear-gradient(145deg,rgba(91,64,18,.98),rgba(20,14,7,.96));box-shadow:0 0 3.5cqw rgba(240,196,74,.62),0 0 1.5cqw rgba(255,226,124,.42),0 .7cqw 1.55cqw #000c,inset 0 1px 0 rgba(255,255,255,.18),inset 0 0 2.8cqw rgba(240,196,74,.18)}
  .endpoint .repo-inline{width:4.35cqw;height:4.35cqw;color:currentColor}
  .endpoint.unlocked .repo-inline{filter:drop-shadow(0 0 .9cqw rgba(255,224,112,.72))}
  .endpoint strong{font:600 1.02cqw/1 var(--display);letter-spacing:.07em;color:currentColor}
  .endpoint.unlocked strong{text-shadow:0 0 .9cqw rgba(255,224,112,.72)}
  @media(prefers-reduced-motion:reduce){.wires .wire-link.energizing{animation:none}}
</style>
