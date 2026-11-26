import {worlds,routeURL,modeFor,createRouter} from './router.mjs';
import {el,link,title,action,sample,status} from './components.mjs';
import {createHosted,errorCopy,CLIENT_VERSION} from './data.mjs';
const host=createHosted();
let routeAbort;
const main=document.querySelector('main');
const nav=document.querySelector('#worlds');
export const label=s=>s[0].toUpperCase()+s.slice(1);
const mode=modeFor(location.search);
document.body.dataset.mode=mode;
document.querySelector('#mode').textContent=mode==='fixture'?'Fixture data':'Hosted data';
const registry=new Map([
 ['home',()=>[el('section',{class:'panel'},...title('COLOSSEUM','Home'),el('p',{},'Choose a world to begin.'),action('Explore Theatre',{href:'#/theatre',primary:true,focusId:'home-theatre'}))]],
 ['components',sample],
 ['data',()=>[...title('Arc 54','Catalogue connection'),el('p',{},mode==='fixture'?'Fixture mode does not contact the Worker. Use hosted mode for a live check.':'Read one real Theatre title through the compatible host.'),el('div',{id:'data-result'}),...(mode==='hosted'?[el('div',{class:'flow'},action('Check catalogue',{id:'check-data'}),action('Check missing title',{id:'check-missing'}))]:[])]],
 ['world',r=>[el('p',{class:'eyebrow'},'WORLD'),el('h1',{},label(r.world)),el('p',{},mode==='fixture'?'Shell preview. No media is being loaded.':'No hosted catalogue is connected to this preview.')]],
 ['detail',r=>[el('h1',{},'Theatre title'),el('p',{},'Reference: '+r.id),link('#/theatre','Back to Theatre')]],
 ['not-found',()=>[el('h1',{},'Page not found'),link('#/','Go home')]]
]);
function render(route,view) {
  routeAbort?.abort();
  routeAbort=new AbortController();
  const signal=routeAbort.signal;
  nav.replaceChildren(...worlds.map(w=>link(routeURL({name:'world',world:w}),label(w),{class:'pill','aria-current':route.world===w?'page':null,'data-focus-id':'nav-'+w})));
  main.replaceChildren(...registry.get(route.name)(route));
  if(route.name==='data'&&mode==='hosted'){
    for(const [id,key] of [['check-data','tt0111161'],['check-missing','tt999999999999']]){
      main.querySelector('#'+id).onclick=async ()=>{
        const target=main.querySelector('#data-result');
        target.replaceChildren(status('loading','Checking catalogue…'));
        try {
          const body=await host.detail(key,signal);
          if(!signal.aborted)target.replaceChildren(status('ready',body.result.title+' · '+body.source));
        } catch(e){if(!signal.aborted)target.replaceChildren(status('error',errorCopy(e)));}
      };
    }
  }
  document.title=(main.querySelector('h1')?.textContent||'Home')+' · Colosseum';
  requestAnimationFrame(()=>{
    if (!view) {window.scrollTo(0,0);main.focus({preventScroll:true});return;}
    const target=[...document.querySelectorAll('[data-focus-id]')].find(e=>e.dataset.focusId===view.focus);
    (target||main).focus({preventScroll:true}); window.scrollTo(0,view.y||0);
  });
}
const router=createRouter(window,render,()=>({y:Math.round(scrollY),focus:document.activeElement?.dataset.focusId??null}));
document.addEventListener('click',e=>{
  if(e.defaultPrevented||e.button!==0||e.metaKey||e.ctrlKey||e.shiftKey||e.altKey)return;
  const a=e.target.closest('a[href^="#/"]'); if(!a)return;
  e.preventDefault(); router.go(a.getAttribute('href'));
});
window.addEventListener('pagehide',router.record);
router.start();
if(mode==='hosted'){
  const refresh=async()=>{
    try {
      const cap=await host.capabilities();
      document.body.dataset.host='compatible';
      window.dispatchEvent(new CustomEvent('arc54:capabilities',{detail:cap}));
      if(cap.version&&cap.version!==CLIENT_VERSION&&!document.querySelector('#refresh-client')){
        const refresh=action('Reload new web version',{id:'refresh-client'});
        refresh.onclick=()=>{router.record();location.reload();};
        document.querySelector('footer').append(refresh);
      }
    } catch {document.body.dataset.host='unavailable';}
  };
  window.addEventListener('focus',refresh);
  refresh();
}
