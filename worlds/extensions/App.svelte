<script>
  // Chain / House / Store picker + live add-on state from the app's ExtensionsStore (over QWebChannel).
  // Installed = the store row exists and is enabled; install/uninstall flip `enabled`, nothing is deleted.
  import { onMount } from 'svelte';
  import Chain from './Chain.svelte';
  import House from './House.svelte';
  import Store from './Store.svelte';

  let { store, host } = $props();

  // Page keys → ExtensionsStore ids (native/engine/ExtensionsStore.cpp seed roster).
  const IDS = {
    torrentio: 'com.stremio.torrentio.addon', nyaa: 'colosseum.well.nyaa', tankoyomi: 'colosseum.well.tankoyomi',
    getcomics: 'colosseum.well.getcomics.issues', tankorent: 'colosseum.well.indexers',
    libgen: 'colosseum.well.libgen', audiobookbay: 'colosseum.well.audiobookbay'
  };

  let tab = $state(typeof location !== 'undefined' && location.hash === '#store' ? 'store' : 'chain');

  function setTab(next) {
    tab = next;
    if (typeof history !== 'undefined') history.replaceState(null, '', next === 'store' ? '#store' : next === 'house' ? '#house' : '#chain');
  }
  let addons = $state({});
  let installedSet = $state(new Set());   // manifest ids + transport URLs of every enabled row (for the Store)
  let loading = $state(true);
  let pending = $state({});

  function refresh() {
    store.installed(rows => {
      const byId = new Map((rows || []).map(r => [r.id, r]));
      installedSet = new Set((rows || []).filter(r => r.enabled).flatMap(r => [r.id, r.transportUrl]).filter(Boolean));
      const next = {};
      for (const [key, id] of Object.entries(IDS)) {
        const row = byId.get(id);
        next[key] = { present: !!row, installed: !!row && !!row.enabled };
      }
      addons = next;
      loading = false;
      for (const key of Object.keys(pending)) if (pending[key] && next[key].installed === pending[key].want) pending = { ...pending, [key]: false };
    });
  }

  onMount(() => {
    refresh();
    store.changed.connect(refresh);
    return () => store.changed.disconnect(refresh);
  });

  function toggle(key) {
    if (pending[key] || !addons[key] || !addons[key].present) return;
    const want = !addons[key].installed;
    pending = { ...pending, [key]: { want } };
    store.setEnabled(IDS[key], want);
    setTimeout(() => { if (pending[key]) { pending = { ...pending, [key]: false }; refresh(); } }, 4000);
  }
</script>

<nav class="extensions-picker" aria-label="Extension sections">
  <button type="button" class:on={tab === 'chain'} data-focus onclick={() => setTab('chain')}>Chain</button>
  <button type="button" class:on={tab === 'house'} data-focus onclick={() => setTab('house')}>House</button>
  <button type="button" class:on={tab === 'store'} data-focus onclick={() => setTab('store')}>Store</button>
</nav>

{#if tab === 'chain'}
  <Chain {addons} {pending} {loading} ontoggle={toggle} />
{:else if tab === 'house'}
  <House {addons} {pending} {loading} ontoggle={toggle} onuniverses={() => host.openUniverseHall()} />
{:else}
  <Store {store} {host} installed={installedSet} />
{/if}

<style>
  .extensions-picker{position:fixed;z-index:30;top:31px;left:50%;transform:translateX(-50%);display:flex;align-items:center;gap:4px;padding:7px;border-radius:30px;background:rgba(105,105,105,.28);border:1px solid rgba(255,255,255,.07);backdrop-filter:blur(24px) saturate(1.05);box-shadow:inset 0 1px 0 rgba(255,255,255,.07),0 8px 24px rgba(0,0,0,.12);font-family:"Segoe UI",system-ui,sans-serif}
  .extensions-picker button{min-width:118px;height:45px;border:0;border-radius:23px;background:transparent;color:rgba(255,255,255,.70);font-size:17px;cursor:pointer;transition:.16s}
  .extensions-picker button:hover{color:#fff;background:rgba(255,255,255,.07)}
  .extensions-picker button.on{background:#f0c44a;color:#15120b;font-weight:600;box-shadow:0 5px 18px rgba(0,0,0,.12)}
  .extensions-picker button:focus-visible{outline:2px solid #f0c44a;outline-offset:3px}
</style>
