import {worlds,routeURL,modeFor,createRouter} from './router.mjs';
const main=document.querySelector('main');
const nav=document.querySelector('#worlds');
export const label=s=>s[0].toUpperCase()+s.slice(1);
const mode=modeFor(location.search);
document.body.dataset.mode=mode;
document.querySelector('#mode').textContent=mode==='fixture'?'Fixture data':'Hosted data';
export function el(tag, attrs={}, ...children) {
  const e=document.createElement(tag);
  for(const [key,val] of Object.entries(attrs)) if (val!=null) e.setAttribute(key,String(val));
  for(const child of children.flat()) if(child!=null) e.append(child instanceof Node ? child : String(child));
  return e;
}
export function link(url,text,attrs={}){return el('a',{href:url,...attrs},text);}
const registry=new Map([
 ['home',()=>[el('p',{class:'eyebrow'},'COLLOSSEUM'),el('h1',{},'Home'),el('p',{},'Choose a world to begin.'),link('#/theatre','Explore Theatre',{class:'button','data-focus-id':'home-theatre'})]],
 ['world',r=>[el('p',{class:'eyebrow'},'WORLD'),el('h1',{},label(r.world)),el('p',{},mode==='fixture'?'Shell preview. No media is being loaded.':'No hosted catalogue is connected to this preview.')]],
 ['detail',r=>[el('h1',{},'Theatre title'),el('p',{},'Reference: '+r.id),link('#/theatre','Back to Theatre')]],
 ['not-found',()=>[el('h1',{},'Page not found'),link('#/','Go home')]]
]);
function render(route,view) {
  nav.replaceChildren(...worlds.map(w=>link(routeURL({name:'world',world:w}),label(w),{'aria-current':route.world===w?'page':null,'data-focus-id':'nav-'+w})));
  main.replaceChildren(...registry.get(route.name)(route));
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
