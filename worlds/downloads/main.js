import { mount } from 'svelte';
import App from './App.svelte';
import { installSpatialFocus } from './focus.js';

function previewModel() {
  return {
    summary: {
      items: 115,
      bytes: '107.9 GB',
      active: 3,
      attention: 1,
      free: '312 GB',
      capacity: '476 GB',
      usedRatio: 0.344,
      worlds: [
        ['Tankoban', 23],
        ['Biblio', 8],
        ['Theatre', 80],
        ['Audiobooks', 4]
      ]
    },
    arriving: [
      {
        id: 'one-piece-111',
        title: 'One Piece',
        subtitle: 'Volume 111 · 36 of 57 files landed',
        world: 'Tankoban',
        state: 'downloading',
        progress: 0.72,
        received: '146 MB',
        total: '203 MB',
        speed: '8.4 MB/s',
        eta: '7 sec',
        canPlay: false,
        tone: 'crimson'
      },
      {
        id: 'dune-two',
        title: 'Dune: Part Two',
        subtitle: '2160p · HDR · English',
        world: 'Theatre',
        state: 'downloading',
        progress: 0.43,
        received: '7.8 GB',
        total: '18.2 GB',
        speed: '31.7 MB/s',
        eta: '5 min',
        canPlay: true,
        tone: 'sand'
      },
      {
        id: 'stormlight',
        title: 'Words of Radiance',
        subtitle: 'Audiobook · 47 tracks',
        world: 'Biblio',
        state: 'paused',
        progress: 0.61,
        received: '1.9 GB',
        total: '3.1 GB',
        speed: '',
        eta: '',
        canPlay: false,
        tone: 'blue'
      },
      {
        id: 'frieren',
        title: 'Frieren',
        subtitle: 'Episode 28 · source needs attention',
        world: 'Theatre',
        state: 'failed',
        progress: 0.18,
        received: '232 MB',
        total: '1.3 GB',
        speed: '',
        eta: '',
        canPlay: false,
        tone: 'violet'
      }
    ],
    landed: [
      { id:'landed-1', title:'The Batman', subtitle:'2160p · 31.4 GB', world:'Theatre', action:'Play', tone:'red', added:'Today', size:'31.4 GB' },
      { id:'landed-2', title:'One Piece', subtitle:'Volume 110 · 198 MB', world:'Tankoban', action:'Read', tone:'sun', added:'Today', size:'198 MB' },
      { id:'landed-3', title:'The Way of Kings', subtitle:'EPUB · 7.8 MB', world:'Biblio', action:'Read', tone:'storm', added:'Yesterday', size:'7.8 MB' },
      { id:'landed-4', title:'Arcane', subtitle:'Season 2 · 8 episodes', world:'Theatre', action:'Play', tone:'blue', added:'Yesterday', size:'18.7 GB' },
      { id:'landed-5', title:'Berserk', subtitle:'Deluxe Vol. 14 · 612 MB', world:'Tankoban', action:'Read', tone:'iron', added:'Sep 25', size:'612 MB' },
      { id:'landed-6', title:'Project Hail Mary', subtitle:'Audiobook · 16h 10m', world:'Audiobooks', action:'Listen', tone:'mint', added:'Sep 24', size:'743 MB' },
      { id:'landed-7', title:'Andor', subtitle:'Season 1 · 12 episodes', world:'Theatre', action:'Play', tone:'gold', added:'Sep 23', size:'42.1 GB' },
      { id:'landed-8', title:'Vagabond', subtitle:'VizBig Vol. 4 · 388 MB', world:'Tankoban', action:'Read', tone:'paper', added:'Sep 22', size:'388 MB' }
    ],
    elsewhere: [
      { id:'else-1', title:'Blade Runner 2049', subtitle:'Laptop 3 · Theatre', available:true },
      { id:'else-2', title:'Mistborn: The Final Empire', subtitle:'Laptop 2 · Biblio', available:true },
      { id:'else-3', title:'Akira', subtitle:'Laptop 3 · Tankoban', available:false }
    ],
    background: [
      { id:'bg-1', title:'Batman: The Long Halloween', stage:'detecting comic panels', progress:0.68, paused:false },
      { id:'bg-2', title:'Words of Radiance', stage:'aligning audiobook text', progress:0.31, paused:false }
    ]
  };
}

function start() {
  const target = document.getElementById('world');
  const preview = new URLSearchParams(location.search).get('preview') === '1';

  if (preview) {
    const actions = {
      back() {},
      open() {},
      delete() {},
      redownload() {},
      cancel() {},
      pause() {},
      resume() {}
    };
    const app = mount(App, { target, props: { initialModel: previewModel(), actions, preview: true } });
    installSpatialFocus(() => app?.back?.() || false);
    return;
  }

  if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
    target.textContent = 'Downloads can only open inside Colosseum. Add ?preview=1 to inspect the Svelte mock.';
    target.style.cssText = 'color:#c9c8d0;font:15px Segoe UI,sans-serif;padding:54px;background:#06070a;min-height:100vh';
    return;
  }

  target.textContent = 'The Svelte Downloads world is a visual prototype; the native Downloads bridge is not wired yet.';
  target.style.cssText = 'color:#c9c8d0;font:15px Segoe UI,sans-serif;padding:54px;background:#06070a;min-height:100vh';
}

document.readyState === 'loading' ? document.addEventListener('DOMContentLoaded', start) : start();