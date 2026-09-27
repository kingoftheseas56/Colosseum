<script>
  // House — Hemanth's mock, 1:1 (colosseum-originals-showcase.html). Download = real install; Universes = the Hall.
  import { onMount } from 'svelte';
  const A = 'surfaces/extensions-hub/assets/';
  let { addons = {}, pending = {}, loading = false, ontoggle, onuniverses } = $props();

  const WORLDS = ['starwars-banner.jpg', 'dcau.jpg', 'cosmere.jpg'];
  const APPS = [
    { key: 'nyaa', cls: 'nyaa', name: 'Nyaa', logo: 'nyaa.png' },
    { key: 'tankoyomi', cls: 'tankoyomi', name: 'Tankoyomi', logo: 'tankoyomi.png' },
    { key: 'getcomics', cls: 'getcomics', name: 'GetComics', logo: 'getcomics.png' },
    { key: 'tankorent', cls: 'tankorent', name: 'Tankorent', logo: 'tankorent.png' },
    { key: 'libgen', cls: 'libgen', name: 'LibGen', logo: 'libgen.ico' },
    { key: 'audiobookbay', cls: 'abb', name: 'AudioBookBay', logo: 'audiobookbay.png' }
  ];
  let index = $state(0);
  onMount(() => {
    const t = setInterval(() => { index = (index + 1) % WORLDS.length; }, 7500);
    return () => clearInterval(t);
  });
  const done = key => !!(addons[key] && addons[key].installed);
</script>

<main class="page">
  <section class="wallpaper" aria-label="Universe slideshow">
    {#each WORLDS as w, n}<div class="world" class:on={n === index}><img src={A + w} alt=""></div>{/each}
  </section>

  <section class="tray" aria-label="Colosseum native extensions">
    <div class="app-item">
      <button class="app universes" type="button" aria-label="Universes" data-focus data-key="house:universes" onclick={onuniverses}><svg viewBox="0 0 64 64" fill="none" stroke="#111" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="32" cy="32" r="5.5" fill="#111" stroke="none"/><ellipse cx="32" cy="32" rx="23" ry="10"/><ellipse cx="32" cy="32" rx="10" ry="23" transform="rotate(25 32 32)"/><ellipse cx="32" cy="32" rx="10" ry="23" transform="rotate(-25 32 32)"/></svg></button>
      <div class="app-actions"><span class="name-pill">Universes</span></div>
    </div>
    <div class="app-item">
      <span class="app database" aria-label="Grand Database"><img src={A + 'colosseum-grand-database.png'} alt=""></span>
      <div class="app-actions"><span class="name-pill">Grand Database</span></div>
    </div>
    {#each APPS as a}
      <div class="app-item">
        <span class="app {a.cls}"><img src={A + a.logo} alt=""></span>
        <div class="app-actions">
          <span class="name-pill">{a.name}</span>
          <button class="dlglass" class:done={done(a.key)} type="button" data-focus data-key={'house:' + a.key}
            disabled={loading || !addons[a.key] || !addons[a.key].present} aria-busy={!!pending[a.key]}
            aria-label={done(a.key) ? a.name + ' installed. Remove' : 'Download ' + a.name} onclick={() => ontoggle(a.key)}>
            <svg class="arrow" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4v11"/><path d="m8 11 4 4 4-4"/><path d="M5 20h14"/></svg>
            <svg class="check" viewBox="0 0 24 24" aria-hidden="true"><path d="m5 12 4 4L19 7"/></svg>
          </button>
        </div>
      </div>
    {/each}
  </section>
</main>

<style>
  /* Carried over from the mock; the page fills the window behind the app's TopBar as the mock fills the browser. */
  .page{--ink:#f6f5f1;--edge:rgba(255,255,255,.34);color:var(--ink);font-family:"Segoe UI",system-ui,sans-serif}
  .wallpaper{position:fixed;inset:0;z-index:0;background:#06070a}
  .world{position:absolute;inset:0;opacity:0;transition:opacity 1.05s ease;pointer-events:none}
  .world.on{opacity:1}
  .world img{width:100%;height:100%;object-fit:cover;object-position:center;display:block}
  .world:after{content:"";position:absolute;inset:0;background:linear-gradient(180deg,rgba(0,0,0,.24) 0%,rgba(0,0,0,.04) 45%,rgba(0,0,0,.10) 62%,rgba(0,0,0,.45) 100%),linear-gradient(90deg,rgba(0,0,0,.14),transparent 24%,transparent 76%,rgba(0,0,0,.14))}
  .tray{position:fixed;z-index:10;left:50%;bottom:42px;transform:translateX(-50%);width:min(1490px,94vw);height:214px;border-radius:34px;background:linear-gradient(135deg,rgba(236,229,216,.37),rgba(220,212,198,.19));border:1px solid var(--edge);backdrop-filter:blur(34px) saturate(1.08);box-shadow:0 28px 75px rgba(0,0,0,.34),inset 0 1px 0 rgba(255,255,255,.28);display:flex;align-items:center;justify-content:center;gap:20px;padding:21px 24px}
  .app-item{width:142px;display:flex;flex-direction:column;align-items:center;gap:10px;min-width:0}
  .app{width:122px;height:122px;border:0;border-radius:21px;background:rgba(250,250,248,.97);display:grid;place-items:center;overflow:hidden;box-shadow:0 14px 28px rgba(0,0,0,.24);transition:transform .18s ease,box-shadow .18s ease}
  button.app{cursor:pointer}
  button.app:hover,:global(body.keys) button.app:focus{outline:none;transform:translateY(-5px) scale(1.03);box-shadow:0 23px 38px rgba(0,0,0,.34)}
  :global(body.keys) button.app:focus{box-shadow:0 0 0 3px #f0c44a,0 23px 38px rgba(0,0,0,.34)}
  .app img{width:58%;height:58%;object-fit:contain;display:block;filter:none}
  .app.tankoyomi img{width:60%;height:60%;object-fit:cover;border-radius:10px}
  .app.getcomics img{width:62%;height:62%}
  .app.tankorent img{width:58%;height:58%}
  .app.database img{width:60%;height:60%}
  .app.libgen img,.app.abb img,.app.nyaa img{width:55%;height:55%}
  .app.universes svg{width:60%;height:60%}
  .app-actions{height:34px;display:flex;align-items:center;justify-content:center;gap:7px;width:100%}
  .name-pill{max-width:104px;height:30px;padding:0 10px;border-radius:11px;border:1px solid rgba(255,255,255,.16);background:rgba(25,26,29,.26);backdrop-filter:blur(18px) saturate(1.12);box-shadow:inset 0 1px 0 rgba(255,255,255,.12);color:rgba(255,255,255,.84);font-size:11px;line-height:30px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;text-align:center}
  .dlglass{width:30px;height:30px;border-radius:10px;border:1px solid rgba(255,255,255,.25);padding:0;display:grid;place-items:center;cursor:pointer;background:linear-gradient(145deg,rgba(255,255,255,.30),rgba(255,255,255,.11));backdrop-filter:blur(18px) saturate(1.2);box-shadow:inset 0 1px 0 rgba(255,255,255,.34),0 5px 13px rgba(0,0,0,.16);transition:.16s ease}
  .dlglass:hover,:global(body.keys) .dlglass:focus{outline:none;transform:translateY(-2px);background:linear-gradient(145deg,rgba(255,255,255,.42),rgba(255,255,255,.16))}
  :global(body.keys) .dlglass:focus{box-shadow:0 0 0 2px #f0c44a}
  .dlglass:disabled{cursor:default;opacity:.6}
  .dlglass svg{width:15px;height:15px;fill:none;stroke:#fff;stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}
  .dlglass.done{background:linear-gradient(145deg,rgba(87,207,145,.44),rgba(69,161,115,.22));border-color:rgba(159,255,210,.38)}
  .dlglass.done .arrow{display:none}
  .dlglass .check{display:none}
  .dlglass.done .check{display:block}
  @media(max-width:1250px){
    .tray{gap:11px;height:190px;bottom:28px;padding:18px 15px}
    .app-item{width:122px}.app{width:104px;height:104px;border-radius:18px}
    .name-pill{max-width:88px;font-size:10px}.dlglass{width:28px;height:28px}
  }
</style>
