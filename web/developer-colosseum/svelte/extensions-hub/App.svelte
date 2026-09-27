<script>
  // Picker + data. The feed says which add-ons are installed; the pages are Hemanth's designs.
  import { onMount } from 'svelte';
  import Chain from './Chain.svelte';
  import House from './House.svelte';

  let { route, env } = $props();
  let tab = $state(route?.params?.tab === 'house' ? 'house' : 'chain');
  let addons = $state({});
  let state = $state('loading');
  let error = $state('');
  let pending = $state({});

  export function setRoute(next) { tab = next?.params?.tab === 'house' ? 'house' : 'chain'; }

  onMount(() => {
    const sub = env.port.subscribe('page.extensionsHub', {}, ev => {
      const s = (ev.sections || []).find(x => x.id === 'extensionsHub.addons');
      if (!s) return;
      state = s.state;
      error = s.error || '';
      if (s.state === 'ready' && s.data) addons = s.data.addons || {};
    });
    return () => sub.close();
  });

  function pick(next) {
    if (next === tab) return;
    window.CW.router.go({ name: 'page', page: 'extensionsHub', params: { tab: next } }, { replace: true });
  }

  function toggle(key) {
    if (pending[key]) return;
    const on = !(addons[key] && addons[key].installed);
    pending = { ...pending, [key]: true };
    env.act(on ? 'page.extensionsHub.install' : 'page.extensionsHub.uninstall', { key }).then(r => {
      pending = { ...pending, [key]: false };
      if (!r.ok && env.toast) env.toast(r.error || 'Colosseum could not change that add-on.');
    });
  }

  function openUniverses() {
    env.act('open.universeHall', {}).then(r => { if (!r.ok && env.toast) env.toast(r.error || 'The Universe Hall is unavailable.'); });
  }
</script>

<nav class="extensions-picker" aria-label="Extension sections">
  <button type="button" class:on={tab === 'chain'} data-focus data-key="ext:chain" onclick={() => pick('chain')}>Chain</button>
  <button type="button" class:on={tab === 'house'} data-focus data-key="ext:house" onclick={() => pick('house')}>House</button>
  <button class="placeholder" type="button" aria-disabled="true" disabled>Store</button>
</nav>

{#if state === 'error'}
  <p class="state">{error}</p>
{:else if tab === 'chain'}
  <Chain {addons} {pending} loading={state === 'loading'} ontoggle={toggle} />
{:else}
  <House {addons} {pending} loading={state === 'loading'} ontoggle={toggle} onuniverses={openUniverses} />
{/if}

<style>
  :global(#app[data-surface="page.extensionsHub"] #board > .col) { padding: 0; gap: 0; }
  :global(#app[data-surface="page.extensionsHub"] #wall) { display: none; }
  .extensions-picker{position:relative;z-index:12;margin:6px auto 0;width:max-content;display:flex;align-items:center;gap:4px;padding:7px;border-radius:30px;background:rgba(105,105,105,.28);border:1px solid rgba(255,255,255,.07);backdrop-filter:blur(24px) saturate(1.05);box-shadow:inset 0 1px 0 rgba(255,255,255,.07),0 8px 24px rgba(0,0,0,.12);font-family:"Segoe UI",system-ui,sans-serif}
  .extensions-picker button{min-width:118px;height:45px;border:0;border-radius:23px;background:transparent;color:rgba(255,255,255,.70);font-size:17px;cursor:pointer;transition:.16s;text-align:center}
  .extensions-picker button:hover{color:#fff;background:rgba(255,255,255,.07)}
  .extensions-picker button.on{background:#f0c44a;color:#15120b;font-weight:600;box-shadow:0 5px 18px rgba(0,0,0,.12)}
  .extensions-picker button.placeholder{cursor:default}
  .extensions-picker button.placeholder:hover{color:rgba(255,255,255,.70);background:transparent}
  :global(body.keys) .extensions-picker button:focus{box-shadow:var(--ring)}
  .state{position:relative;z-index:12;text-align:center;color:rgba(255,255,255,.7);margin-top:40px}
</style>
