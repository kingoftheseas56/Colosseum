import test from 'node:test';
import assert from 'node:assert/strict';
import {readRoute,routeURL,modeFor,createRouter} from './router.mjs';
test('valid direct routes, explicit fixture mode, malformed input',()=>{
 assert.deepEqual(readRoute('#/theatre/title/tt123'),{name:'detail',world:'theatre',id:'tt123'});
 assert.equal(routeURL(readRoute('#/theatre/shows')),'#/theatre/shows');
 for(const s of ['#/theatre/title/%3C','##/vault','#/theatre//x','#/unknown','#/theatre/title/tt/x'])assert.equal(readRoute(s).name,'not-found');
 assert.equal(modeFor('?mode=fixture'),'fixture');assert.equal(modeFor('?mode=unknown'),'hosted');assert.equal(modeFor(''),'hosted');
});
test('router commits navigations, carries scroll/focus, restores on Back/refresh',()=>{
 const entries=[{url:'#/theatre',state:null}];let i=0;
 const win={location:{get hash(){return entries[i].url}},history:{get state(){return entries[i].state},
 replaceState(state){entries[i].state=state},pushState(state,_,url){entries.splice(++i,Infinity,{url,state})}},
 addEventListener(event,fn){this[event]=fn}};
 const events=[];const factory=()=>createRouter(win,(r,v)=>events.push([r.name,v]),()=>({y:48,focus:'card-1'}));
 const r=factory();r.start();r.go('#/theatre/title/tt123');
 assert.equal(win.location.hash,'#/theatre/title/tt123');i--;win.popstate();
 assert.deepEqual(events.at(-1),['world',{y:48,focus:'card-1'}]);
 factory().start();assert.deepEqual(events.at(-1),['world',{y:48,focus:'card-1'}]);
});
