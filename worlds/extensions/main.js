// Extensions world: Chain + House + Store, all Svelte, hosted by qml/ExtensionsWorldPage.qml over QWebChannel.
// Channel objects: `extensions` = the app's ExtensionsStore, `host` = the thin native shell bridge
// (back, universe hall, open a URL in the browser).
import { mount } from 'svelte';
import App from './App.svelte';
import { installSpatialFocus } from './focus.js';

// Back closes a Store sub-view (category, search) first, then the world.
const backThen = leave => () => { if (window.__storeBack && window.__storeBack()) return; leave(); };

// ?preview=1 in a plain browser: a stand-in store so the world can be looked at outside the app.
function previewBridge() {
  let rows = [
    { id: 'com.stremio.torrentio.addon', transportUrl: 'https://torrentio.strem.fun/manifest.json', enabled: false },
    { id: 'community.anime.kitsu', enabled: false },
    { id: 'org.stremio.opensubtitlesv3', enabled: false },
    { id: 'colosseum.well.tankoyomi', enabled: true }
  ];
  const listeners = new Set();
  const fire = () => listeners.forEach(fn => fn());
  const store = {
    installed(cb) { cb(rows.map(r => ({ ...r }))); },
    setEnabled(id, on) { rows = rows.map(r => r.id === id ? { ...r, enabled: on } : r); fire(); },
    install(url) { setTimeout(() => { rows = [...rows, { id: url, transportUrl: url, enabled: true }]; fire(); }, 600); },
    changed: { connect: fn => listeners.add(fn), disconnect: fn => listeners.delete(fn) }
  };
  const host = { back() {}, openUniverseHall() {}, openExternal(url) { window.open(url, '_blank'); } };
  return { store, host };
}

function start() {
  const target = document.getElementById('world');
  if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
    if (new URLSearchParams(location.search).get('preview') === '1') {
      const { store, host } = previewBridge();
      mount(App, { target, props: { store, host } });
      installSpatialFocus(backThen(() => {}));
      return;
    }
    target.textContent = 'Extensions can only open inside Colosseum.';
    return;
  }
  new QWebChannel(qt.webChannelTransport, channel => {
    const { extensions, host } = channel.objects;
    mount(App, { target, props: { store: extensions, host } });
    const back = backThen(() => host.back());
    installSpatialFocus(back);
    window.extensionsBack = back;
  });
}
document.readyState === 'loading' ? document.addEventListener('DOMContentLoaded', start) : start();
