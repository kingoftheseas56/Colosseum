export const worlds = Object.freeze(['tankoban', 'biblio', 'theatre', 'universes', 'vault']);
const theatreTabs = new Set(['discover','movies','shows','anime','library']);
const stableID = /^[A-Za-z0-9][A-Za-z0-9._~-]{0,95}$/;
export function readRoute(hash = '#/') {
  const raw = hash.startsWith('#') ? hash.slice(1) : hash;
  if (!raw || raw === '/') return {name:'home'};
  const bits = raw.startsWith('/') ? raw.slice(1).split('/') : [];
  const [world, tab, id] = bits;
  if (!worlds.includes(world) || bits.some(s => !s || !stableID.test(s))) return {name:'not-found'};
  if (world === 'theatre' && tab === 'title' && bits.length === 3) return {name:'detail',world,id};
  if (bits.length === 1 || (world === 'theatre' && bits.length === 2 && theatreTabs.has(tab))) {
    return {name:'world',world,tab:tab || 'discover'};
  }
  return {name:'not-found'};
}
export function routeURL(r) {
  if (r.name === 'home') return '#/';
  if (r.name === 'world') return '#/'+r.world+(r.tab&&r.tab!=='discover'?'/'+r.tab:'');
  if (r.name === 'detail') return '#/'+r.world+'/title/'+r.id;
  return '#/not-found';
}
export const modeFor = (query) => new URLSearchParams(query).get('mode') === 'fixture' ? 'fixture' : 'hosted';
export function createRouter(win, paint, sample) {
  win.history.scrollRestoration='manual';
  let current, started=false;
  function read() {
    const route=readRoute(win.location.hash);
    const entry=win.history.state?.arc54;
    if(!entry || entry.url!==win.location.hash) {
      win.history.replaceState({...win.history.state,arc54:{url:win.location.hash}},'');
    }
    current=route;
    paint(route,win.history.state?.arc54?.view||null);
  }
  const record = () => {
    const arc54=win.history.state?.arc54;
    if (started && arc54?.url===win.location.hash) win.history.replaceState({...win.history.state,arc54:{...arc54,view:sample()}},'');
  };
  const go = (url) => {
    if (url===win.location.hash) return;
    record();
    win.history.pushState({arc54:{url}},'',url);
    read();
  };
  return {
    start(){if(started)return;started=true;win.addEventListener('popstate',read);read();},
    go, record, current:()=>current
  };
}
