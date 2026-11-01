/* Batch 4 local mock support. No network or native operations. */
(function(){
"use strict";
const root=new URL(".",document.currentScript.src);
const paths={profile:"10-profile/profile.html","account-welcome":"11-account-entry/account-welcome.html","account-flyout":"11-account-entry/account-flyout.html","account-centre":"11-account-entry/account-centre.html","account-profile":"12-account-forms/account-profile.html","account-security":"12-account-forms/account-security.html","account-recovery":"12-account-forms/account-recovery.html","account-privacy":"12-account-forms/account-privacy.html","account-devices":"12-account-forms/account-devices.html","account-your-colosseum":"12-account-forms/account-your-colosseum.html","stremio-sync":"13-stremio-sync/stremio-sync.html","extension-setup":"14-extension-setup/extension-setup.html","tankoyomi-configuration":"14-extension-setup/tankoyomi-configuration.html"};
const sizes=[[412,900],[900,412],[354,792],[792,354],[1280,700],[800,1280],[1280,800]];
const labels={entry:["portrait","landscape","narrow","narrow-landscape","desktop","tablet","tablet-landscape"],forms:["phone412","landscape900","phone354","landscape792","desktop","tablet800","tablet1280"],sync:["portrait","landscape","smallPortrait","smallLandscape","desktop","tabletPortrait","tabletLandscape"],setup:["portrait","landscape","small","smallLandscape","desktop","tablet","tabletLandscape"]};
const intents=[];
function emit(action,detail={}){const event={action,detail,localOnly:true,at:new Date().toISOString()};intents.push(event);document.dispatchEvent(new CustomEvent("batch4:intent",{detail:event}));return event;}
function visible(el){return !!el&&el.getClientRects().length>0&&!el.closest("[hidden]")&&getComputedStyle(el).visibility!=="hidden";}
function focusables(scope){return [...scope.querySelectorAll('button:not(:disabled),input:not(:disabled),a[href],select:not(:disabled),[tabindex="0"]')].filter(visible);}
function token(el){if(!el||el===document.body)return null;if(el.id)return {id:el.id};const data={};for(const k of ["act","action","avatar","key","target","page","id","toggle","move","dir","language","default","tab"])if(el.dataset[k]!==undefined)data[k]=el.dataset[k];return Object.keys(data).length?{tag:el.tagName,data}:null;}
function locate(t,scope=document){if(!t)return null;if(t.id)return scope.querySelector("#"+CSS.escape(t.id));let selector=t.tag||"";for(const [k,v]of Object.entries(t.data||{}))selector+="[data-"+k+"="+JSON.stringify(v)+"]";return [...scope.querySelectorAll(selector)].find(e=>visible(e)&&!e.disabled)||null;}
function restore(t,scope=document){const el=locate(t,scope);if(el&&visible(el)&&!el.disabled){el.focus({preventScroll:true});return el;}return null;}
function contain(event,scope){if(event.key!=="Tab"||!scope)return false;const items=focusables(scope);if(!items.length)return false;const index=items.indexOf(document.activeElement),next=(index+(event.shiftKey?-1:1)+items.length)%items.length;event.preventDefault();items[index<0?(event.shiftKey?items.length-1:0):next].focus();return true;}
function route(name,extra={}){
 if(!paths[name])return;
 const current=new URLSearchParams(location.search),frame=document.querySelector("#device,#app-frame,#mock,#viewport")||document.querySelector("#frame"),rect=frame?.getBoundingClientRect(),w=Math.round(parseFloat(frame?.style.width)||rect?.width||412),h=Math.round(parseFloat(frame?.style.height)||rect?.height||900);
 let i=sizes.findIndex(s=>s[0]===w&&s[1]===h);if(i<0)i=w>h?1:0;
 const top=current.get("top")||current.get("insetTop")||(current.get("inset")==="24"?"24":"0"),bottom=current.get("bottom")||current.get("insetBottom")||(current.get("inset")==="24"?"24":"0"),font=current.get("font")||current.get("scale")||"1";
 const dest=new URL(paths[name],root);const q=new URLSearchParams();
 if(current.has("capture"))q.set("capture","1");
 if(current.has("mode"))q.set("mode",current.get("mode"));
 if(name==="profile"){q.set("w",w);q.set("h",h);q.set("scale",font);q.set("top",top);q.set("bottom",bottom);}
 else if(paths[name].startsWith("11-")){q.set("v",labels.entry[i]);q.set("scale",font);q.set("top",top);q.set("bottom",bottom);}
 else if(paths[name].startsWith("12-")){q.set("preset",labels.forms[i]);q.set("font",font);q.set("top",top);q.set("bottom",bottom);}
 else if(name==="stremio-sync"){if(current.has("capture"))q.set("capture",labels.sync[i]);else q.set("preset",labels.sync[i]);q.set("font",font);q.set("top",top);q.set("bottom",bottom);}
 else {q.set("preset",labels.setup[i]);q.set("scale",font);q.set("insetTop",top);q.set("insetBottom",bottom);}
 for(const [k,v]of Object.entries(extra))q.set(k,v);
 dest.search=q;location.href=dest.href;
}
document.addEventListener("click",e=>{
 const original=e.target.closest("button"),key=token(original);if(!original)return;
 queueMicrotask(()=>requestAnimationFrame(()=>{if(!original.isConnected&&(document.activeElement===document.body||!document.activeElement.isConnected))restore(key);}));
},true);
window.Batch4={root,paths,intents,emit,visible,focusables,token,locate,restore,contain,route};
})();