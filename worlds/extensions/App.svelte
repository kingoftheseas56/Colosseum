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

<button class="manage" type="button" data-focus
  onclick={() => host.openManage && host.openManage()}>Manage</button>

{#if tab === 'chain'}
  <Chain {addons} {pending} {loading} ontoggle={toggle} />
{:else if tab === 'house'}
  <House {addons} {pending} {loading} ontoggle={toggle} onuniverses={() => host.openUniverseHall()} />
{:else}
  <Store {store} {host} installed={installedSet} />
{/if}

<style>
  /* Same size as the Tankoban · Biblio · Theatre pills (qml/TopBar.qml): 46px capsule, 34px pills, 14px text.
     Absolute, not fixed: the top bar scrolls away with the page instead of floating over it. */
  .extensions-picker{position:absolute;z-index:30;top:15px;left:50%;transform:translateX(-50%);display:flex;align-items:center;gap:4px;height:46px;box-sizing:border-box;padding:0 6px;border-radius:999px;background:rgba(105,105,105,.28);border:1px solid rgba(255,255,255,.07);backdrop-filter:blur(24px) saturate(1.05);font-family:"Segoe UI",system-ui,sans-serif}
  .extensions-picker button{height:34px;padding:0 17px;border:0;border-radius:999px;background:transparent;color:rgba(255,255,255,.62);font-size:14px;font-weight:500;font-family:inherit;cursor:pointer;transition:.16s}
  .extensions-picker button:hover{color:#fff;background:rgba(255,255,255,.07)}
  .extensions-picker button.on{background:#f0c44a;color:#1a1408;font-weight:600}
  .manage{position:absolute;z-index:30;top:21px;right:44px;height:34px;padding:0 17px;border-radius:999px;border:1px solid rgba(255,255,255,.12);background:rgba(15,16,22,.8);backdrop-filter:blur(22px);color:rgba(255,255,255,.78);font:500 14px "Segoe UI",system-ui,sans-serif;cursor:pointer}
  .manage:hover{color:#fff;border-color:rgba(255,255,255,.24)}
  .manage:focus-visible{outline:2px solid #f0c44a;outline-offset:3px}
  .extensions-picker button:focus-visible{outline:2px solid #f0c44a;outline-offset:3px}
</style>
