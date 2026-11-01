"use strict";
(function(){
const PAGE_FILES={profile:"account-profile.html",security:"account-security.html",recovery:"account-recovery.html",privacy:"account-privacy.html",devices:"account-devices.html",your:"account-your-colosseum.html"};
const PAGE_TITLES={profile:"Profile",security:"Security",recovery:"Recovery",privacy:"Data & privacy",devices:"Devices",your:"Your Colosseum"};
const NAV=[["profile","◌","Profile"],["security","◇","Security"],["devices","▣","Devices"],["recovery","↶","Recovery"],["privacy","◫","Data & privacy"]];
const PRESETS=[["phone412","Phone portrait · 412 × 900",412,900,"phone"],["phone354","Small portrait · 354 × 792",354,792,"phone"],["landscape900","Phone landscape · 900 × 412",900,412,"phone"],["landscape792","Small landscape · 792 × 354",792,354,"phone"],["desktop","Desktop · 1280 × 700",1280,700,"desktop"],["tablet800","Tablet portrait · 800 × 1280",800,1280,"tablet"],["tablet1280","Tablet landscape · 1280 × 800",1280,800,"tablet"]];
const STATE_OPTIONS={profile:[["default","Profile + avatars"],["edit","Username edited"],["long","Long username"]],security:[["default","Protection / password / sign-out"],["approval","Pending sign-in approval"],["password","Password fields expanded"],["logout","Sign-out confirmation"],["long","Long labels + password fields"]],recovery:[["default","Recovery overview"],["replace","Replace key expanded"],["long","Long labels + replacement"]],privacy:[["default","All sections"],["search","Clear-search confirmation"],["activity","Clear-activity confirmation"],["delete","Delete-account confirmation"],["long","Long copy + deletion"]],devices:[["default","Trusted devices"],["revoke","Revoke confirmation"],["long","Long device names"],["empty","Empty service response"],["error","Illustrative service error"]],your:[["default","Monthly portrait"],["long","Long titles / activity"]]};
const ICON_PATHS={
shield:["M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z","m9.2 12.3 1.8 1.8 3.9-4.1"],
lock:["M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z","M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10"],
logout:["M9 5H5v14h4","m14 8 4 4-4 4M18 12H9"],
device:["M5 5h14a1.5 1.5 0 0 1 1.5 1.5V15a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 15V6.5A1.5 1.5 0 0 1 5 5Z","M8.5 20h7M12 16.5V20"],
key:["M12.75 11a4.25 4.25 0 1 1-8.5 0 4.25 4.25 0 0 1 8.5 0Z","M12.6 11H21M17.2 11v3M14.6 11v2"],
replace:["M8 8a5 5 0 1 1-1 7","M4 16v-5h5"],
recovery:["M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z","M9 12h6"],
search:["M16 10.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0Z","M15 15l4 4"],
activity:["M5 18V9M10 18V5M15 18v-7M20 18V7"],
sync:["M7 17.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.2 8.8 4.4 4.4 0 0 0 7 17.5Z","M9 12h6"],
portable:["M7 17.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.2 8.8 4.4 4.4 0 0 0 7 17.5Z"],
export:["M12 4v11M8 11l4 4 4-4M5 19h14"],
secret:["M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z","M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10"]};
const AVATAR_PATHS={
laurel:["M15 25C9.7 22.3 7.5 17.7 7.5 12.2M17 25c5.3-2.7 7.5-7.3 7.5-12.8","M7.8 14c-2.4-.7-3.5-2.1-3.5-4.3 2.6.1 4.1 1.2 4.6 3.5M8.8 18.3c-2.6-.1-4-1.2-4.5-3.3 2.5-.5 4.3.3 5.2 2.5M11.1 22c-2.4.5-4.1-.2-5.1-2.1 2.2-1.1 4-.8 5.5.9","M24.2 14c2.4-.7 3.5-2.1 3.5-4.3-2.6.1-4.1 1.2-4.6 3.5M23.2 18.3c2.6-.1 4-1.2 4.5-3.3-2.5-.5-4.3.3-5.2 2.5M20.9 22c2.4.5 4.1-.2 5.1-2.1-2.2-1.1-4-.8-5.5.9"],
column:["M8 8h16M10 5.5h12M10 26.5h12M8 24h16M11.5 8v16M20.5 8v16M14.5 8v16M17.5 8v16"],
book:["M5.5 8.5c4.1-.5 7.6.4 10.5 2.7v14c-2.9-2.3-6.4-3.2-10.5-2.7v-14ZM26.5 8.5c-4.1-.5-7.6.4-10.5 2.7v14c2.9-2.3 6.4-3.2 10.5-2.7v-14Z"],
screen:["M7.5 7h17a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2h-17a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2Z","m14 11.7 6 2.8-6 2.8v-5.6ZM11.5 26h9"],
panels:["M7 6h6a1 1 0 0 1 1 1v7a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 6h7a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 14h7a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V15a1 1 0 0 1 1-1ZM7 18h6a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1Z"],
custom:["M16 8v16M8 16h16"]};
const qs=new URLSearchParams(location.search);
const centreMode=document.body.dataset.page==="centre";
const entryPresets={portrait:"phone412",landscape:"landscape900",narrow:"phone354","narrow-landscape":"landscape792",desktop:"desktop",tablet:"tablet800","tablet-landscape":"tablet1280"};
if(centreMode){if(qs.has("v"))qs.set("preset",entryPresets[qs.get("v")]||"phone412");if(qs.has("scale"))qs.set("font",qs.get("scale"));if(qs.get("stress")==="1")qs.set("state","long");if(qs.has("expanded"))qs.set("state",qs.get("expanded")==="recovery"?"replace":qs.get("expanded"));if(qs.get("scenario")==="approval")qs.set("state","approval");}
let page=centreMode?(qs.get("section")||"profile"):(document.body.dataset.page||"profile");
if(!PAGE_FILES[page])page="profile";
const accountMode=qs.get("mode")||"signed",signedIn=accountMode==="signed";
const formValues=Object.create(null),revealed=Object.create(null),panels=Object.create(null);
const OPENERS={password:"security-password-open",logout:"security-logout-open",replace:"recovery-open",search:"privacy-search-open",activity:"privacy-activity-open",delete:"privacy-delete-open",revoke:"device-revoke"};
function rememberFields(){document.querySelectorAll("#app-frame input[id]").forEach(el=>{formValues[el.id]=el.value;if(el.dataset.secret)revealed[el.id]=el.type==="text";});}
function clearSecrets(){for(const id of Object.keys(formValues))if(id!=="profile-name"){delete formValues[id];delete revealed[id];}document.querySelectorAll("#app-frame input[data-secret],#delete-current").forEach(el=>{el.value="";el.type="password";});}
function seedPanels(){for(const k of Object.keys(panels))delete panels[k];if(OPENERS[activeState])panels[activeState]=true;if(activeState==="long"){if(page==="security")panels.password=true;if(page==="recovery")panels.replace=true;if(page==="privacy")panels.delete=true;}}
function panelOpen(name){return !!panels[name];}
function focusAction(act,id){const all=[...document.querySelectorAll("[data-act="+JSON.stringify(act)+"]")];const el=all.find(e=>Batch4.visible(e)&&(!id||e.dataset.id===id));if(el&&!el.disabled){el.focus();el.scrollIntoView({block:"nearest",inline:"nearest"});}}
function setPanel(name,open,focusId=""){const previousId=data.revokeId;panels[name]=open;if(!open&&["password","replace","delete"].includes(name))clearSecrets();render();if(open){if(focusId){const el=document.getElementById(focusId);if(el){el.focus();el.scrollIntoView({block:"nearest"});}}else focusAction({logout:"security-logout-close",search:"privacy-search-close",activity:"privacy-activity-close",delete:"privacy-delete-close",revoke:"device-cancel"}[name]);}else focusAction(OPENERS[name],previousId);}
function localIntent(action,detail={}){Batch4.emit(action,detail);reviewNote("Local fixture intent: "+action+". No account or provider operation.");}
function routeSection(section){if(!PAGE_FILES[section])return;clearSecrets();if(!centreMode){const u=new URL(PAGE_FILES[section],location.href);const context=new URLSearchParams(location.search);context.delete("state");context.delete("expanded");u.search=context;location.href=u.href;return;}page=section;activeState="default";seedPanels();data.draft=data.username;delete formValues["profile-name"];const u=new URL(location.href);u.searchParams.set("section",section);u.searchParams.delete("expanded");u.searchParams.delete("state");history.replaceState(null,"",u);const selector=document.getElementById("r-state");if(selector){selector.innerHTML=selectOptions(STATE_OPTIONS[page]);selector.value="default";}const label=document.getElementById("review-page-title");if(label)label.textContent=PAGE_TITLES[page];render();const current=[...document.querySelectorAll('[data-act="page"][data-page="'+section+'"]')].find(Batch4.visible);if(current)current.focus();}

const esc=value=>String(value===undefined||value===null?"":value).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const svg=(name,color="#c9c8d0",size=20)=>"<svg viewBox='0 0 24 24' width='"+size+"' height='"+size+"' aria-hidden='true' fill='none' stroke='"+color+"' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>"+(ICON_PATHS[name]||[]).map(d=>"<path d='"+d+"'></path>").join("")+"</svg>";
const avatar=(name,color="#f0c44a",size=40)=>name==="initial"?"<span class='initial-letter' style='color:"+color+"'>"+esc(data.username[0]||"?").toUpperCase()+"</span>":"<svg viewBox='0 0 32 32' width='"+size+"' height='"+size+"' aria-hidden='true' fill='none' stroke='"+color+"' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'>"+(AVATAR_PATHS[name]||[]).map(d=>"<path d='"+d+"'></path>").join("")+"</svg>";
const button=(label,act,variant="",extra="")=>"<button class='btn "+variant+"' type='button' data-act='"+act+"' "+extra+"><span class='btn-label'>"+esc(label)+"</span></button>";
const iconBox=(kind,gold=false)=>"<span class='icon-box"+(gold?" gold":"")+"'>"+svg(kind,gold?"#f0c44a":"#c9c8d0")+"</span>";
const divider=(soft=false)=>"<div class='divider"+(soft?" soft-divider":"")+"'></div>";
const field=(id,label,placeholder="",password=false,help="")=>"<div class='input-field' data-field='"+id+"'><label class='field-label' for='"+id+"'>"+esc(label)+"</label>"+(help?"<div class='field-help'>"+esc(help)+"</div>":"")+"<input id='"+id+"' name='"+id+"' type='"+(password?"password":"text")+"' placeholder='"+esc(placeholder)+"' autocomplete='off' value='"+(id==="profile-name"?esc(data.draft):"")+"' "+(password?"data-secret='1'":"")+">"+(password?"<button class='reveal' type='button' data-act='reveal' data-target='"+id+"' aria-label='Show password'>◉</button>":"")+"</div>";
const setting=(kind,title,copy,actions,second="",gold=false)=>"<div class='setting-row'><div class='setting-main'>"+iconBox(kind,gold)+"<div class='setting-copy'><h3 class='setting-heading'>"+esc(title)+"</h3><p class='setting-description'>"+esc(copy)+"</p>"+(second?"<p class='setting-secondary'>"+esc(second)+"</p>":"")+"</div></div><div class='setting-actions'>"+actions+"</div></div>";
const confirm=(copy,cancel,commit,action)=>"<div class='confirm-inline'><p>"+esc(copy)+"</p><div class='button-row'>"+button("Cancel",cancel,"", "style='width:82px'")+button(commit,action,"primary")+"</div></div>";
const sw=(key,label)=>"<button type='button' class='switch' role='switch' aria-label='"+esc(label)+"' aria-checked='"+String(!!data[key])+"' data-act='switch' data-key='"+key+"'><span class='knob'></span></button>";
const fixtureName=accountMode==="local"?"":centreMode?"Luffy":"Ariadne";
const data={username:fixtureName,draft:fixtureName,avatar:centreMode?"initial":"laurel",saved:false,protection:true,searches:true,activity:true,sync:true,profileStatus:"",recoveryError:"",monthIndex:0,deviceRecords:null,deviceBusy:false};
let activeState=qs.get("state")||"default";
const initially=(STATE_OPTIONS[page]||[]).some(([value])=>value===activeState);
if(!initially)activeState="default";
const modes={phone412:PRESETS[0],phone354:PRESETS[1],landscape900:PRESETS[2],landscape792:PRESETS[3],desktop:PRESETS[4],tablet800:PRESETS[5],tablet1280:PRESETS[6]};
let selectedPreset=qs.get("preset")||"phone412";
if(!modes[selectedPreset])selectedPreset="phone412";
const capture=qs.has("capture")&&qs.get("capture")!=="0";
let viewport={w:Number(qs.get("w"))||modes[selectedPreset][2],h:Number(qs.get("h"))||modes[selectedPreset][3],kind:modes[selectedPreset][4]};
let insetTop=Number(qs.get("top"))||0,insetBottom=Number(qs.get("bottom"))||0,fontScale=Number(qs.get("font"))||1;
let hostPending=qs.get("response")==="pending";
let keyboard=qs.get("keyboard")==="1",requestedIme=Number(qs.get("ime"))||0,imeLayout=qs.get("imeLayout")==="scroll"?"scroll":"fixed";
seedPanels();

function renderProfile(){
 const shown=activeState==="long"?"Ariadne_of_the_Northern_Archives_and_the_Seven_Seas":data.username;
 const options=[["initial","Initial"],["laurel","Laurel"],["column","Column"],["book","Book"],["screen","Screen"],["panels","Panels"],["custom","Custom  Later"]];
 const avatarGrid=options.map(([id,label])=>"<button type='button' class='avatar-choice"+(data.avatar===id?" selected":"")+"' data-act='avatar' data-avatar='"+id+"' aria-label='"+(id==="custom"?"Custom avatar coming later":"Use "+label+" avatar")+"' "+(id==="custom"?"disabled":"")+"><span class='avatar-ring'><span class='avatar-circle'>"+avatar(id,data.avatar===id?"#f0c44a":"#c9c8d0",30)+"</span></span><span>"+esc(label)+"</span></button>").join("");
 return "<h1 class='page-title'>Profile</h1>"+
 "<section class='profile-stage'><div class='hero-medallion'>"+avatar(data.avatar,"#f0c44a",60)+"</div><div class='hero-persona'><h2 class='hero-name'>"+esc(shown)+"</h2><div class='hero-sub'>Colosseum account</div><div class='hero-rule'></div><div class='medallion-note'><span class='medallion-check'>✓</span>"+esc(data.avatar[0].toUpperCase()+data.avatar.slice(1))+" medallion</div></div></section>"+
 "<section class='profile-section'><div class='profile-section-header'><h2>Avatar</h2><span class='status-pill'>BUILT-IN</span></div><div class='avatar-grid'>"+avatarGrid+"</div></section>"+
 "<section class='profile-section'><div class='profile-section-header'><h2>Username</h2></div><div class='username-edit'>"+
 field("profile-name","Username","")+
 button("Save changes","profile-save","primary","id='profile-save'"+(data.draft.trim()===data.username?" disabled":""))+
 "</div>"+(data.saved?"<p class='status-message'>Profile updated</p>":"")+"</section>";
}
function renderSecurity(){
 const pending=activeState==="approval";
 const showPassword=panelOpen("password");
 const showLogout=panelOpen("logout");
 const protection=setting("shield","New-device protection","New sign-ins need approval from a trusted Colosseum device, or the recovery key.",sw("protection","Toggle new-device protection"),data.protection?"Protection is on":"Protection is off",true);
 const approvals="<section class='pending-alert'><div class='gold-overline'>NEW DEVICE</div><div class='pending-title-actions'><h2>New sign-in waiting for approval</h2><div class='button-row'>"+
 button("Deny","approval-deny","","style='width:88px'")+button("Approve","approval-approve","primary","style='width:96px'")+"</div></div><div class='pending-meta'>"+
 "<div><h3>Colosseum sign-in</h3><p>A Colosseum sign-in is waiting for your decision.</p></div><span>Pending</span></div></section>";
 const passwordRow=setting("lock","Password","Change the password used to sign in to this Colosseum account.",showPassword?"":button("Change","security-password-open","","style='width:88px'"),"",true);
 const passwordEditor=showPassword?"<div class='expanded-content'><div class='password-grid'>"+
 field("security-current","Current password","Current password",true)+field("security-new","New password","New password",true)+field("security-confirm","Confirm new password","Confirm new password",true)+
 "</div><p class='status-message' id='security-validity'>Use 8–128 characters.</p><div class='button-row' style='justify-content:flex-end;margin-top:14px'>"+
 button("Cancel","security-password-close","","style='width:88px'")+button("Save new password","security-password-save","primary","style='min-width:158px' disabled")+"</div></div>":"";
 const logoutRow=setting("logout","Sign out everywhere","End every Colosseum account session, including this one.",button("Sign out everywhere","security-logout-open","","style='min-width:154px'"),"",true);
 const logoutConfirm=showLogout?confirm("Every signed-in device will need to sign in again.","security-logout-close","Confirm sign out","security-logout-confirm"):"";
 return "<h1 class='page-title'>Security</h1><div style='height:34px'></div>"+(pending?approvals:divider()+protection)+divider()+passwordRow+(showPassword?passwordEditor:"")+divider()+logoutRow+logoutConfirm+divider();
}
function renderRecovery(){
 const opened=panelOpen("replace");
 const warning="<div class='icon-note' style='margin-bottom:18px'>"+
 "<span class='icon-box gold' style='width:25px;height:25px;min-width:25px;font-size:12px;color:var(--gold);font-weight:600'>!</span>"+
 "<div><div style='font-size:calc(12px * var(--scale));font-weight:600;color:var(--dim)'>The old key will stop working.</div>"+
 "<div class='tiny muted' style='margin-top:4px'>After replacement, save the new key before leaving the one-time screen.</div></div></div>";
 const replaceForm=opened?"<div class='expanded-content'>"+warning+
 "<div style='max-width:460px'>"+field("recovery-current","Current password","Enter your current password",true)+"</div>"+
 "<p id='recovery-error' class='error-message' aria-live='polite' style='display:none'></p>"+
 "<div class='button-row' style='margin-top:14px'>"+button("Cancel","recovery-close","","style='width:92px'")+button("Generate new key","recovery-generate","primary","style='min-width:160px'")+"</div></div>":"";
 return "<h1 class='page-title'>Recovery</h1><section class='recovery-hero'><div class='key-disc'>"+svg("key","#f0c44a",48)+"</div>"+
 "<div class='recovery-copy'><h2>Your way back in.</h2><p>Your recovery key can reset your password if you lose access. Colosseum showed the current key once when it was created and does not keep it available for reveal here.</p>"+
 "<div class='tiny dim'><span class='gold-dot'></span>Only the newest recovery key works.</div></div></section>"+
 "<section class='recovery-row'>"+setting("replace","Replace recovery key","Generate a new recovery key if the old one is lost, exposed, or no longer somewhere you trust.",opened?"":button("Replace key","recovery-open","","style='width:112px'"))+
 replaceForm+"</section>"+
 "<section class='recovery-row'>"+setting("recovery","If you lose your password","Sign out and choose account recovery. Your username, recovery key, and a new password are enough to recover the account.","")+"</section>";
}
function renderDevices(){
 let records=data.deviceRecords||[
 {id:"demo-1",name:"Ariadne's laptop",meta:"Windows · last seen now",current:true},
 {id:"demo-2",name:"OnePlus phone",meta:"Android · last seen 2 hours ago",current:false},
 {id:"demo-3",name:"Reading room tablet",meta:"Android · last seen 1 day ago",current:false}
 ];
 if(activeState==="empty")records=[];
 if(activeState==="long"&&records[1])records[1].name="OnePlus CPH2487 · Archive and reading sessions · demonstration label that should wrap cleanly";
 const revoking=panelOpen("revoke")?(data.revokeId||records[1]?.id):"";
 const rows=records.map((rec)=>{
 const actions=rec.current?"":button("Revoke","device-revoke","","data-id='"+rec.id+"' style='width:82px'");
 const body="<div class='device-row'><div class='device-main'>"+iconBox("device",rec.current)+"<div class='device-copy'><div class='device-name-line'>"+
 "<span class='device-name'>"+esc(rec.name)+"</span>"+(rec.current?"<span class='this-device'>THIS DEVICE</span>":"")+
 "</div><div class='device-meta'>"+esc(rec.meta)+"</div></div></div>"+actions+"</div>";
 const confirmOpen=revoking===rec.id;
 const confirmation=confirmOpen?confirm("Revoke "+rec.name+"? It will lose account access and need to sign in again.","device-cancel","Confirm revoke","device-confirm"):"";
 return body+confirmation;
 }).join("");
 return "<div class='device-title-row'><h1 class='page-title'>Devices</h1>"+button("Refresh","device-refresh","","style='width:82px;height:32px'")+"</div>"+
 "<div class='device-summary'><div><div class='device-count'>"+records.length+"</div><div class='device-count-label'>trusted devices</div></div><p class='device-note'>Revoking a device ends its account access. It can sign in again later if you approve it.</p></div>"+
 "<div class='device-section-label'><strong>Trusted devices</strong><span>"+records.length+" "+(records.length===1?"DEVICE":"DEVICES")+"</span></div>"+divider()+(activeState==="error"?"<p class='error-message'>Could not refresh trusted devices.</p>":"")+(records.length?rows:"<p class='devices-empty'>No trusted devices were returned by the account service.</p>");
}

function renderPrivacy(){
 const clearSearch=setting("search","Search history","Remember searches on this device. Search history never needs to leave this machine.",button("Clear history","privacy-search-open","","style='width:100px;height:33px'")+sw("searches","Remember searches on this device"));
 const clearActivity=setting("activity","Your Colosseum activity","Keep watch and reading activity for Monthly Portrait, recent activity, completion history and personal media insights.",button("Clear activity","privacy-activity-open","","style='width:104px;height:33px'")+sw("activity","Keep watch and reading activity"));
 const syncHistory=setting("sync","Cross-device history","Sync watch and reading history between your signed-in devices. Turning this off can still leave activity available locally.",sw("sync","Sync watch and reading history"));
 const searchConfirm=panelOpen("search")?confirm("Clear remembered searches from this device?","privacy-search-close","Clear search history","privacy-search-commit"):"";
 const activityConfirm=panelOpen("activity")?confirm("Clear your stored watch and reading activity? Continue progress and Collection are not included.","privacy-activity-close","Clear activity history","privacy-activity-commit"):"";
 const exportRow=setting("export","Export my data","Create a portable copy of the account data Colosseum holds for you.",button(data.exportBusy?"Cancel export":"Request export","privacy-export","","style='width:116px;height:33px'"));
 const portable=[
 ["Continue progress","Portable resume points and progress."],["Collection","Titles and shelves you save."],["History","When cross-device history is enabled."],["Portable preferences","Account-level choices with approved owners."]
 ];
 const local=[
 ["Search history","Kept locally when search history is enabled."],["Window & session state","Open sessions, geometry, PiP and machine state."],["Paths & media","Local files and filesystem locations."],["Downloads & caches","Machine-local acquisition and rebuildable data."]
 ];
 function lane(kind,title,description,rows,localOnly){
   return "<div class='map-lane'><div class='map-head'>"+iconBox(kind,!localOnly)+"<div><h3>"+esc(title)+"</h3><p>"+esc(description)+"</p></div></div>"+
   rows.map(([n,desc])=>"<div class='map-fact"+(localOnly?" local":"")+"'><span class='bullet'></span><div><h4>"+esc(n)+"</h4><p>"+esc(desc)+"</p></div></div>").join("")+"</div>";
 }
 const map="<section class='privacy-map'><h2>What Colosseum keeps where</h2>"+
 "<p class='privacy-map-intro'>These controls sit on top of a simple boundary: portable account state can follow you, while machine-specific and sensitive data stays outside ordinary sync.</p>"+
 "<div class='map-grid'>"+lane("portable","Can follow your account","Portable state with a safe sync contract.",portable,false)+
 lane("device","Stays on this device","Machine-owned data that ordinary sync does not carry.",local,true)+"</div>"+
 "<div class='secret-note'>"+iconBox("secret",true)+"<div><h3>Secrets are a separate boundary.</h3>"+
 "<p>Passwords, recovery keys, session tokens and API credentials do not enter ordinary account sync. Filesystem paths and secret-bearing fields are blocked from those payloads too.</p></div></div></section>";
 const deleteRow="<div class='setting-row'><div class='setting-copy'><h3 class='setting-heading'>Delete Colosseum account</h3>"+
 "<p class='setting-description'>Permanently delete the account and its server-side account data. Local media files on your devices are not part of account deletion.</p></div>"+
 "<div class='setting-actions'>"+button("Delete account","privacy-delete-open","danger","style='width:112px;height:33px'")+"</div></div>";
 const deleteConfirm=panelOpen("delete")?
 "<div class='delete-confirm'><h3>This is permanent.</h3><p>Deleting the account removes its server-side identity and portable cloud data. Local media files stay on your devices. Provider backups follow the provider's retention policy; Colosseum keeps only a capability-protected retry receipt for up to 30 days.</p>"+
 "<div class='input-field'><input type='password' id='delete-current' data-secret='1' maxlength='512' autocomplete='off' aria-label='Current password for account deletion' placeholder='Current password' value=''></div>"+
 "<div class='button-row'>"+button("Cancel","privacy-delete-close","","style='width:82px;height:33px'")+
 button("Continue to delete","privacy-delete-continue","danger","id='privacy-delete-continue' style='min-width:140px;height:33px' disabled")+"</div></div>":"";
 return "<h1 class='page-title'>Data &amp; privacy</h1>"+
 "<section class='privacy-group'><span class='section-label'>History</span>"+clearSearch+searchConfirm+clearActivity+activityConfirm+syncHistory+"</section>"+
 "<section class='privacy-group'><span class='section-label'>Your account data</span>"+exportRow+"</section>"+
 map+"<section class='danger-zone'><span class='section-label'>Account</span>"+deleteRow+deleteConfirm+"</section>";
}
const MONTHS=[
 {month:"October",year:"2026",metrics:["28h 14m","1,286","7","23"],highlights:[
 ["One Piece","MOST WATCHED","16 episodes"],["Naruto","MOST RETURNED","8 episodes"],["The Stormlight Archive","MOST READ","402 pages"],["Star Wars","COMPLETED","3 titles"]
 ],activities:[["Oct 30","One Piece","Watched Episode 312","Theatre"],["Oct 28","The Way of Kings","Read 42 pages","Biblio"],["Oct 22","Naruto","Watched 2 episodes","Theatre"],["Oct 17","Star Wars","Completed a title","Theatre"]]},
 {month:"September",year:"2026",metrics:["18h 02m","928","4","18"],highlights:[
 ["Naruto","MOST WATCHED","10 episodes"],["One Piece","MOST RETURNED","5 episodes"],["Words of Radiance","MOST READ","270 pages"],["Star Wars","COMPLETED","2 titles"]
 ],activities:[["Sep 28","Naruto","Watched 2 episodes","Theatre"],["Sep 24","One Piece","Watched Episode 300","Theatre"],["Sep 20","Words of Radiance","Read 21 pages","Biblio"]]}
];
function renderYour(){
 const m=MONTHS[data.monthIndex];
 const long=activeState==="long";
 const highlights=m.highlights.map(([title,label,value],i)=>"<div class='feature'><div class='feature-art'><b>"+esc(long&&i===2?"The Stormlight Archive: The Way of Kings and Words of Radiance":title)+"</b></div>"+
 "<div class='feature-tag'>"+esc(label)+"</div><div class='feature-value'>"+esc(value)+"</div></div>").join("");
 const metrics=m.metrics.map((value,i)=>"<div class='metric'><strong>"+esc(value)+"</strong><span>"+["Watch time","Pages read","Completed","Active days"][i]+"</span></div>").join("");
 const activities=m.activities.map(([date,title,meta,world],i)=>"<div class='activity-record'><div class='activity-date'>"+esc(date)+"</div>"+
 "<div class='activity-text'><strong>"+esc(long&&i===0?"One Piece — Water Seven and Enies Lobby (director's alternate descriptive title)":title)+"</strong><span>"+esc(meta)+"</span></div>"+
 "<div class='activity-world'>"+esc(world)+"</div></div>").join("");
 return "<div class='month-header'><h1 class='page-title'>Your Colosseum</h1><div class='month-nav'><button type='button' data-act='month-previous' aria-label='Previous month' "+(data.monthIndex===1?"disabled":"")+">‹</button>"+
 "<span>"+esc(m.month+" "+m.year)+"</span><button type='button' data-act='month-next' aria-label='Next month' "+(data.monthIndex===0?"disabled":"")+">›</button></div></div>"+
 "<section class='portrait-grid'><div class='month-portrait'><div class='month-heading'><span class='section-label'>Monthly portrait</span>"+
 "<div class='big-month'>"+esc(m.month)+"</div><div class='big-year'>"+esc(m.year)+"</div></div><div class='metrics-grid'>"+metrics+"</div></div>"+
 "<div class='month-shelf'><div class='shelf-header'><strong>"+esc(m.month)+" highlights</strong><span>4 TITLES</span></div><div class='feature-grid'>"+highlights+"</div></div></section>"+
 "<section class='activity-section'><h2>Recent activity</h2>"+activities+"</section>";
}

function buildSidebar(){
 const nav=NAV.map(([id,glyph,label])=>"<button type='button' class='rail-item"+(id===page?" active":"")+"' data-act='page' data-page='"+id+"'><span class='rail-glyph'>"+glyph+"</span>"+esc(label)+"</button>").join("");
 return "<aside class='sidebar'><div class='sidebar-inner'><div class='identity'><div class='identity-initial'>"+esc(data.username[0]||"?")+"</div><div><div class='identity-name'>"+esc(data.username||"Not signed in")+"</div><div class='identity-sub'>Colosseum account</div></div></div>"+
 nav+"<button class='session-action' data-act='signout' type='button'>"+(accountMode==="local"?"Sign in to Colosseum":"Sign out of Colosseum")+"</button></div></aside>";
}
function buildCompact(){
 return "<nav class='compact-nav' aria-label='Account Centre sections'>"+
 NAV.map(([id,glyph,label])=>"<button type='button' class='rail-item"+(id===page?" active":"")+"' data-act='page' data-page='"+id+"'><span class='rail-glyph'>"+glyph+"</span>"+esc(label)+"</button>").join("")+"</nav>";
}
function buildPage(){
 switch(page){case "profile":return renderProfile();case "security":return renderSecurity();case "recovery":return renderRecovery();case "privacy":return renderPrivacy();case "devices":return renderDevices();case "your":return renderYour();default:return renderProfile();}
}
function buildApp(){
 const prefix="../../../../../baseline/reference/desktop/assets/icons/";
 return "<div class='shell' data-focus-scope='account-centre'>"+buildSidebar()+"<main class='main'>"+buildCompact()+
 "<div class='top-actions'><button class='back-action' data-act='back' type='button'>‹ Back</button>"+
 (viewport.kind!=="desktop"?"":"<div class='window-icons'><button aria-label='Minimize' type='button' data-act='window'><img src='"+prefix+"minimize.svg' alt=''></button>"+
 "<button aria-label='Exit fullscreen' type='button' data-act='window'><img src='"+prefix+"fullscreen-exit.svg' alt=''></button>"+
 "<button aria-label='Quit Colosseum' type='button' data-act='window'><img src='"+prefix+"power.svg' alt=''></button></div>")+
 "<button class='compact-session' type='button' data-act='signout'>"+(accountMode==="local"?"Sign in to Colosseum":"Sign out of Colosseum")+"</button></div>"+
 "<div class='page-scroll' tabindex='0' role='region' aria-label='"+esc(PAGE_TITLES[page])+" scroll region'><div class='page-inner'>"+buildPage()+"</div></div></main></div>";
}
const selectOptions=items=>items.map(([value,label])=>"<option value='"+value+"'>"+esc(label)+"</option>").join("");
function createLayout(){
 const ui="<div class='review-ui'><div class='review-top'><div><a href='index.html'>‹ All six studies</a> &nbsp; / &nbsp; <strong id='review-page-title'>"+esc(PAGE_TITLES[page])+"</strong> <span>12-account-forms</span></div><span>Desktop source a40f66be · Alpha phone shell 77844eef</span></div>"+
 "<div class='review-controls'><label>Preset<select id='r-preset'>"+selectOptions(PRESETS.map(x=>[x[0],x[1]]))+"</select></label>"+
 "<label>Width (logical px)<input id='r-width' type='number' min='320' max='1920' step='1'></label>"+
 "<label>Height (logical px)<input id='r-height' type='number' min='240' max='1500' step='1'></label>"+
 "<label>Inset top<input id='r-top' type='number' min='0' max='100'></label>"+
 "<label>Inset bottom<input id='r-bottom' type='number' min='0' max='100'></label>"+
 "<label>Font scale<select id='r-font'><option value='1'>1.0×</option><option value='1.3'>1.3×</option></select></label>"+
 "<label>Page state<select id='r-state'>"+selectOptions(STATE_OPTIONS[page])+"</select></label>"+
 "<label>Host fixture<select id='r-response'><option value='idle'>Idle</option><option value='pending'>Pending response</option></select></label><label class='check'><input id='r-keyboard' type='checkbox'>Reserve keyboard space</label><label>IME px (0 = auto)<input id='r-ime' type='number' min='0' max='500' value='0'></label><label>Keyboard layout<select id='r-ime-layout'><option value='fixed'>Fixed sections · bounded preview</option><option value='scroll'>Scroll original sections · proposal</option></select></label></div>"+
 "<div class='review-note'>Illustrative account, devices, statistics and content values. No real credentials, provider operations, sync, deletion or API calls. Viewport/insets provisional; controls live outside the application mock. <span id='ime-summary'></span> <span id='review-status'></span></div></div>";
 document.body.innerHTML=(capture?"":ui+"<div class='review-stage'>")+
 "<div class='"+(capture?"capture-container":"")+"'><div id='app-frame' class='app-frame' role='application' aria-label='Colosseum account mock'></div></div>"+
 (capture?"":"</div>");
 if(capture)document.body.classList.add("capture");
 else{
  const ids=["preset","width","height","top","bottom","font","state","ime","ime-layout","response"];
  ids.forEach(name=>{let el=document.getElementById("r-"+name);if(!el)return;el.addEventListener("change",()=>handleReview(name,el));});
  document.getElementById("r-keyboard").addEventListener("change",ev=>{keyboard=ev.target.checked;render();focusKeyboardField();});
  document.getElementById("r-preset").value=selectedPreset;
  document.getElementById("r-state").value=activeState;
  document.getElementById("r-font").value=String(fontScale);
  document.getElementById("r-width").value=viewport.w;
  document.getElementById("r-height").value=viewport.h;
  document.getElementById("r-top").value=insetTop;
  document.getElementById("r-bottom").value=insetBottom;
  document.getElementById("r-keyboard").checked=keyboard;
  document.getElementById("r-ime").value=requestedIme;
  document.getElementById("r-ime-layout").value=imeLayout;
  document.getElementById("r-response").value=hostPending?"pending":"idle";
 }
 document.getElementById("app-frame").addEventListener("click",handleAppClick);
 document.getElementById("app-frame").addEventListener("input",handleAppInput);
 document.getElementById("app-frame").addEventListener("keydown",e=>{
  const target=e.target;
  if(e.key==="Tab"){Batch4.contain(e,document.getElementById("app-frame"));return;}
  if(e.key==="Escape"){for(const name of ["delete","activity","search","revoke","replace","password","logout"])if(panelOpen(name)){e.preventDefault();setPanel(name,false);return;}e.preventDefault();Batch4.route("account-flyout",{open:0,mode:accountMode});return;}
  if(target.matches('[data-act="page"]')&&["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End"].includes(e.key)){
   e.preventDefault();const i=NAV.findIndex(n=>n[0]===target.dataset.page),step=["ArrowLeft","ArrowUp"].includes(e.key)?-1:1,next=e.key==="Home"?0:e.key==="End"?NAV.length-1:Math.max(0,Math.min(NAV.length-1,i+step));routeSection(NAV[next][0]);return;
  }
  if(target.matches('[data-act="avatar"]')&&["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End"].includes(e.key)){
   e.preventDefault();const items=[...document.querySelectorAll('[data-act="avatar"]:not(:disabled)')],i=items.indexOf(target),cols=Number(document.getElementById("app-frame").style.getPropertyValue("--avatar-cols")),step=e.key==="ArrowUp"?-cols:e.key==="ArrowDown"?cols:e.key==="ArrowLeft"?-1:1,j=e.key==="Home"?0:e.key==="End"?items.length-1:i+step;if(items[j])items[j].focus();return;
  }
  if(e.key==="Enter"&&target.tagName==="INPUT"){const next={"security-current":"security-new","security-new":"security-confirm"}[target.id];if(next){e.preventDefault();document.getElementById(next)?.focus();}else {const act={"profile-name":"profile-save","security-confirm":"security-password-save","recovery-current":"recovery-generate","delete-current":"privacy-delete-continue"}[target.id];if(act){e.preventDefault();document.querySelector('[data-act="'+act+'"]')?.click();}}}
 });
}
function handleReview(which,control){
 const value=control.value;
 if(which==="preset"){
  selectedPreset=value;
  const p=modes[value];
  viewport={w:p[2],h:p[3],kind:p[4]};
  document.getElementById("r-width").value=viewport.w;
  document.getElementById("r-height").value=viewport.h;
 }else if(which==="width"||which==="height"){
  const n=Math.max(which==="width"?320:240,Math.min(which==="width"?1920:1500,Number(value)||412));
  viewport[which==="width"?"w":"h"]=n;
  viewport.kind=Math.min(viewport.w,viewport.h)<600?"phone":viewport.w===1280&&viewport.h===700?"desktop":"tablet";
 }else if(which==="top")insetTop=Math.max(0,Math.min(100,Number(value)||0));
 else if(which==="bottom")insetBottom=Math.max(0,Math.min(100,Number(value)||0));
 else if(which==="response")hostPending=value==="pending";
 else if(which==="ime")requestedIme=Math.max(0,Math.min(500,Number(value)||0));
 else if(which==="ime-layout")imeLayout=value==="scroll"?"scroll":"fixed";
 else if(which==="font")fontScale=Math.max(1,Math.min(1.3,Number(value)||1));
 else if(which==="state"){activeState=value;if(page==="profile"&&value==="edit")data.draft=data.username+"_edit";else if(page==="profile"&&value==="default")data.draft=data.username;}
 if(which==="state"){clearSecrets();seedPanels();}
 render();
}
function syncStateChoice(){const el=document.getElementById("r-state");if(el)el.value=activeState;}
function reviewNote(message){const el=document.getElementById("review-status");if(el)el.textContent=" — "+message;}
function render(){
 const frame=document.getElementById("app-frame");
 rememberFields();const focusToken=Batch4.token(document.activeElement);const selection=document.activeElement.tagName==="INPUT"?[document.activeElement.selectionStart,document.activeElement.selectionEnd]:null;
 const phone=viewport.kind==="phone",tablet=viewport.kind==="tablet";
 const innerWidth=viewport.w-(phone?32:300);
 frame.style.setProperty("--avatar-cols",String(Math.max(2,Math.min(7,Math.floor(innerWidth/88)))));
 frame.className="app-frame "+(phone?"phone":tablet?"tablet":"desktop")+(innerWidth<620?" narrow":"")+(keyboard?" keyboard":"")+(capture?" capture":"");
 frame.dataset.page=page;
 frame.classList.toggle("keyboard-scroll",phone&&keyboard&&imeLayout==="scroll");
 for(const [cls,on] of Object.entries({"profile-wide":innerWidth>=560,"username-narrow":Math.min(620,innerWidth)<430,"rows-compact":innerWidth<(page==="privacy"?900:page==="recovery"||page==="devices"?820:620),"password-compact":innerWidth<760,"map-compact":innerWidth<900,"recovery-compact":innerWidth<820,"devices-compact":innerWidth<820,"portrait-wide":innerWidth>960,"cards-compact":innerWidth<528,"header-compact":innerWidth<440}))frame.classList.toggle(cls,on);
 frame.style.width=viewport.w+"px";
 frame.style.height=viewport.h+"px";
 frame.style.setProperty("--scale",String(fontScale));
 frame.style.setProperty("--inset-top",insetTop+"px");
 frame.style.setProperty("--inset-bottom",insetBottom+"px");
 const wantedIme=keyboard?(requestedIme||Math.min(255,Math.round(viewport.h*.43))):0;
 const appliedIme=Math.min(wantedIme,Math.max(0,viewport.h-insetTop-insetBottom-(phone&&imeLayout==="scroll"?72:(phone?128:96)+96)));
 frame.style.setProperty("--keyboard",appliedIme+"px");frame.dataset.imeRequested=wantedIme;frame.dataset.imeApplied=appliedIme;frame.dataset.imeLayout=imeLayout;
 const imeSummary=document.getElementById("ime-summary");if(imeSummary)imeSummary.textContent=keyboard?"IME geometry only: requested "+wantedIme+"px; reserved "+appliedIme+"px. "+(imeLayout==="scroll"?"Proposal: original navigation joins scrolling content.":"Fixed navigation; reserve bounded to keep form controls reachable."):"";
 const scroller=frame.querySelector(".page-scroll");
 const lastScroll=scroller?scroller.scrollTop:0;
 frame.innerHTML=buildApp();
 frame.querySelectorAll("input[id]").forEach(el=>{if(el.id in formValues)el.value=formValues[el.id];if(el.dataset.secret){el.maxLength=512;el.type=revealed[el.id]?"text":"password";const btn=frame.querySelector('[data-target="'+el.id+'"]');if(btn){btn.textContent=revealed[el.id]?"○":"◉";btn.setAttribute("aria-label",revealed[el.id]?"Hide password":"Show password");}}});
 if(!signedIn)frame.querySelectorAll('[data-act="avatar"],[data-act="profile-save"],#profile-name,[data-act="security-password-open"],[data-act="security-password-save"],[data-act="security-logout-open"],[data-act="approval-deny"],[data-act="approval-approve"],[data-act="recovery-open"],[data-act="device-revoke"],[data-act="device-refresh"],[data-key="protection"]').forEach(el=>el.disabled=true);
 const restored=Batch4.restore(focusToken,frame);if(restored&&selection&&restored.tagName==="INPUT")try{restored.setSelectionRange(...selection);}catch(_){}
 const next=frame.querySelector(".page-scroll");
 if(next)next.scrollTop=lastScroll;
 const current=frame.querySelector(".compact-nav .rail-item.active");
 if(current&&phone)current.scrollIntoView({block:"nearest",inline:"nearest"});
 updateValidity();
}
function setState(value,focusId=""){activeState=value;seedPanels();syncStateChoice();render();if(focusId){const el=document.getElementById(focusId);if(el){el.focus();el.scrollIntoView({block:"nearest"});}}}
function focusKeyboardField(){
 const id=page==="profile"?"profile-name":page==="security"?"security-current":page==="recovery"?"recovery-current":page==="privacy"?"delete-current":null;
 const el=id?document.getElementById(id):null;
 if(el&&keyboard){el.focus();el.scrollIntoView({block:"nearest"});}
}

function applyHostFixture(){
 if(!hostPending)return;
 const pending=(act,label)=>document.querySelectorAll('[data-act="'+act+'"]').forEach(el=>{if(label){const t=el.querySelector(".btn-label");if(t)t.textContent=label;else el.textContent=label;}el.disabled=true;});
 const disable=(...acts)=>acts.forEach(act=>pending(act));
 if(page==="profile"){pending("profile-save","Saving…");document.getElementById("profile-name").disabled=true;}
 if(page==="security"){
  if(panelOpen("password")){pending("security-password-save","Saving…");disable("security-password-open","security-password-close");}
  if(panelOpen("logout")){pending("security-logout-confirm","Signing out…");disable("security-logout-open","security-logout-close");}
  if(activeState==="approval"){pending("approval-approve","Working…");disable("approval-deny");}
  if(activeState==="default")disable("switch");
 }
 if(page==="recovery"){pending("recovery-generate","Generating…");disable("recovery-open","recovery-close");}
 if(page==="privacy"){
  for(const kind of ["search","activity"])if(panelOpen(kind)){pending("privacy-"+kind+"-commit","Clearing…");disable("privacy-"+kind+"-open","privacy-"+kind+"-close");}
  if(panelOpen("delete")){pending("privacy-delete-continue","Continuing…");disable("privacy-delete-open","privacy-delete-close");const input=document.getElementById("delete-current");if(input)input.disabled=true;}
  if(activeState==="default"){const label=document.querySelector('[data-act="privacy-export"] .btn-label');if(label)label.textContent="Cancel export";}
 }
 if(page==="devices"){
  if(panelOpen("revoke")){pending("device-confirm","Revoking…");disable("device-cancel","device-revoke");}
  else pending("device-refresh","Refreshing…");
 }
}
function updateValidity(){
 const profile=document.getElementById("profile-save");
 if(profile)profile.disabled=!signedIn||!data.draft.trim()||data.draft.trim()===data.username.trim();
 const curr=document.getElementById("security-current");
 const next=document.getElementById("security-new");
 const confirmInput=document.getElementById("security-confirm");
 const securitySave=document.querySelector("[data-act='security-password-save']");
 const validation=document.getElementById("security-validity");
 if(curr&&next&&confirmInput&&securitySave){
  const length=Array.from(next.value).length;
  const valid=curr.value.length>0&&length>=8&&length<=128&&next.value===confirmInput.value;
  securitySave.disabled=!signedIn||!valid;
  if(validation){
   validation.textContent=confirmInput.value.length>0&&next.value!==confirmInput.value?"Those passwords do not match.":"Use 8–128 characters.";
   validation.style.color=confirmInput.value.length>0&&next.value!==confirmInput.value?"#f0a3a3":"#9a99a5";
  }
 }
 const deleteInput=document.getElementById("delete-current");
 const deleteButton=document.getElementById("privacy-delete-continue");
 if(deleteInput&&deleteButton)deleteButton.disabled=!signedIn||deleteInput.value.length===0;
 applyHostFixture();
}
function handleAppInput(ev){
 if(ev.target.id==="profile-name"){data.draft=ev.target.value;data.saved=false;}
 if(ev.target.id)formValues[ev.target.id]=ev.target.value;
 updateValidity();
}
function handleAppClick(ev){
 const control=ev.target.closest("[data-act]");
 if(!control)return;
 const act=control.dataset.act;
 if(act&&!control.disabled&&act!=="reveal")Batch4.emit(act,{page});
 if(control.disabled)return;
 if(act==="page"){routeSection(control.dataset.page);return;}
 if(act==="back"){clearSecrets();Batch4.route("account-flyout",{open:0,mode:accountMode});return;}
 if(act==="window"){reviewNote("Window control shown for desktop fidelity only.");return;}
 if(act==="signout"){reviewNote("Sign-out is not executed by this local mock.");return;}
 if(act==="reveal"){
  const fieldInput=document.getElementById(control.dataset.target);
  if(!fieldInput)return;
  const revealing=fieldInput.type==="password";fieldInput.type=revealing?"text":"password";
  control.textContent=revealing?"○":"◉";
  control.setAttribute("aria-label",revealing?"Hide password":"Show password");revealed[fieldInput.id]=revealing;
  return;
 }
 if(act==="avatar"){
  if(control.dataset.avatar==="custom")return;
  data.avatar=control.dataset.avatar;
  data.saved=true;render();return;
 }
 if(act==="profile-save"){
  if(!data.draft.trim()||data.draft.trim()===data.username.trim())return;
  data.username=data.draft.trim();formValues["profile-name"]=data.username;data.saved=true;localIntent("renameUsername",{fixtureLength:data.username.length});render();document.getElementById("profile-name")?.focus();return;
 }
 if(act==="switch"){data[control.dataset.key]=!data[control.dataset.key];render();reviewNote("Switch host-response illustrated locally.");return;}
 if(act==="security-password-open"){setPanel("password",true,"security-current");return;}
 if(act==="security-password-close"){setPanel("password",false);return;}
 if(act==="security-password-save"){
  updateValidity();if(control.disabled)return;
  reviewNote("Password change request demonstration only; no password changed.");
  localIntent("changePassword",{validated:true});setPanel("password",false);return;
 }
 if(act==="security-logout-open"){setPanel("logout",true);return;}
 if(act==="security-logout-close"){setPanel("logout",false);return;}
 if(act==="security-logout-confirm"){reviewNote("No session was ended.");return;}
 if(act==="approval-deny"||act==="approval-approve"){reviewNote("No device approval was sent. Rows remain server-authoritative.");return;}
 if(act==="recovery-open"){setPanel("replace",true,"recovery-current");return;}
 if(act==="recovery-close"){setPanel("replace",false);return;}
 if(act==="recovery-generate"){
  const input=document.getElementById("recovery-current");
  const error=document.getElementById("recovery-error");
  if(!input||!error)return;
  if(!input.value){error.style.display="block";error.textContent="Enter your current password.";input.focus();}
  else reviewNote("No recovery key generated or disclosed in this local mock.");
  return;
 }
 if(act==="privacy-search-open"){setPanel("search",true);return;}
 if(act==="privacy-search-close"){setPanel("search",false);return;}
 if(act==="privacy-search-commit"){reviewNote("No search history cleared. Request only.");return;}
 if(act==="privacy-activity-open"){setPanel("activity",true);return;}
 if(act==="privacy-activity-close"){setPanel("activity",false);return;}
 if(act==="privacy-activity-commit"){reviewNote("No activity history cleared. Request only.");return;}
 if(act==="privacy-export"){if(hostPending&&activeState==="default"){hostPending=false;data.exportBusy=false;}else data.exportBusy=!data.exportBusy;render();reviewNote("Export request/cancel displayed locally; no export created.");return;}
 if(act==="privacy-delete-open"){setPanel("delete",true);return;}
 if(act==="privacy-delete-close"){setPanel("delete",false);return;}
 if(act==="privacy-delete-continue"){reviewNote("No account deletion or re-authentication performed.");return;}
 if(act==="device-revoke"){data.revokeId=control.dataset.id;setPanel("revoke",true);return;}
 if(act==="device-cancel"){setPanel("revoke",false);data.revokeId="";return;}
 if(act==="device-confirm"){reviewNote("No device revoked. Server-authoritative list remains unchanged.");return;}
 if(act==="device-refresh"){reviewNote("No provider refresh requested. Illustrative device list unchanged.");return;}
 if(act==="month-previous"&&data.monthIndex<MONTHS.length-1){data.monthIndex++;render();return;}
 if(act==="month-next"&&data.monthIndex>0){data.monthIndex--;render();return;}
}
createLayout();
if(activeState==="edit"&&page==="profile")data.draft=data.username+"_edit";
render();
if(keyboard)focusKeyboardField();
else if(centreMode){const initial=[...document.querySelectorAll(".rail-item.active")].find(Batch4.visible);initial?.focus({preventScroll:true});}
})();
