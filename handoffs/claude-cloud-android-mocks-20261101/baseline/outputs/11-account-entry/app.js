(function(){
'use strict';
const $=id=>document.getElementById(id);
const page=document.body.dataset.page;
const params=new URLSearchParams(location.search);
const presets={portrait:[412,900],landscape:[900,412],narrow:[354,792],'narrow-landscape':[792,354],desktop:[1280,700],tablet:[800,1280],'tablet-landscape':[1280,800]};
const modes=['local','signed','offline'];
const state={v:params.get('v')||'portrait',mode:params.get('mode')||(page==='account-welcome'?'local':'signed'),open:params.get('open')!=='0',section:params.get('section')||'profile',scenario:params.get('scenario')||'normal',stress:params.get('stress')==='1',avatar:'initial',usernameDraft:'Luffy',passwordExpanded:false,logoutConfirm:false,recoveryExpanded:false,revoke:'',searchConfirm:false,activityConfirm:false,deleteConfirm:false,protection:true,searchHistory:true,activityHistory:true,syncHistory:true,notices:0,fontScale:Number(params.get('scale')||1),inTop:Number(params.get('top')||0),inBottom:Number(params.get('bottom')||0),inLeft:0,inRight:0};
const expanded=params.get('expanded')||'';
state.passwordExpanded=expanded==='password';state.logoutConfirm=expanded==='logout';state.recoveryExpanded=expanded==='recovery';state.searchConfirm=expanded==='search';state.activityConfirm=expanded==='activity';state.deleteConfirm=expanded==='delete';
if(!presets[state.v])state.v='portrait';
if(!modes.includes(state.mode))state.mode='local';
if(!['profile','security','devices','recovery','privacy'].includes(state.section))state.section='profile';
let w=presets[state.v][0],h=presets[state.v][1];
const capture=params.get('capture')==='1';
const asset='../../reference/desktop/assets/';
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const signed=()=>state.mode==='signed'||state.mode==='offline';
const username=()=>state.stress?'Luffy_TheGrandLineAndTheVeryLongCollectionName': 'Luffy';
const initial=()=>signed()?username()[0].toUpperCase():'?';
const icon=(file,alt)=>'<img src="'+asset+'icons/'+file+'" alt="'+esc(alt||'')+'">';
const paths={
laurel:['M15 25C9.7 22.3 7.5 17.7 7.5 12.2M17 25c5.3-2.7 7.5-7.3 7.5-12.8','M7.8 14c-2.4-.7-3.5-2.1-3.5-4.3 2.6.1 4.1 1.2 4.6 3.5M8.8 18.3c-2.6-.1-4-1.2-4.5-3.3 2.5-.5 4.3.3 5.2 2.5M11.1 22c-2.4.5-4.1-.2-5.1-2.1 2.2-1.1 4-.8 5.5.9','M24.2 14c2.4-.7 3.5-2.1 3.5-4.3-2.6.1-4.1 1.2-4.6 3.5M23.2 18.3c2.6-.1 4-1.2 4.5-3.3-2.5-.5-4.3.3-5.2 2.5M20.9 22c2.4.5 4.1-.2 5.1-2.1-2.2-1.1-4-.8-5.5.9'],
column:['M8 8h16M10 5.5h12M10 26.5h12M8 24h16M11.5 8v16M20.5 8v16M14.5 8v16M17.5 8v16'],
book:['M5.5 8.5c4.1-.5 7.6.4 10.5 2.7v14c-2.9-2.3-6.4-3.2-10.5-2.7v-14ZM26.5 8.5c-4.1-.5-7.6.4-10.5 2.7v14c2.9-2.3 6.4-3.2 10.5-2.7v-14Z'],
screen:['M7.5 7h17a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2h-17a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2Z','m14 11.7 6 2.8-6 2.8v-5.6ZM11.5 26h9'],
panels:['M7 6h6a1 1 0 0 1 1 1v7a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 6h7a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 14h7a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V15a1 1 0 0 1 1-1ZM7 18h6a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1Z'],
custom:['M16 8v16M8 16h16']
};
function avatar(id,letter){if(id==='initial')return '<span class="avatar-glyph" style="font-size:36px;font-weight:600">'+esc(letter||initial())+'</span>';
return '<span class="avatar-glyph"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+(paths[id]||[]).map(d=>'<path d="'+d+'"/>').join('')+'</svg></span>';}
const glyphPaths={
 shield:['M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z','m9.2 12.3 1.8 1.8 3.9-4.1'],
 lock:['M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z','M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10'],
 logout:['M9 5H5v14h4','m14 8 4 4-4 4M18 12H9'],
 key:['M12.75 11a4.25 4.25 0 1 1-8.5 0 4.25 4.25 0 0 1 8.5 0Z','M12.6 11H21M17.2 11v3M14.6 11v2'],
 replace:['M8 8a5 5 0 1 1-1 7','M4 16v-5h5'],
 recovery:['M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z','M9 12h6'],
 search:['M16 10.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0Z','M15 15l4 4'],
 activity:['M5 18V9M10 18V5M15 18v-7M20 18V7'],
 sync:['M7 17.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.2 8.8 4.4 4.4 0 0 0 7 17.5Z','M9 12h6'],
 portable:['M7 17.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.2 8.8 4.4 4.4 0 0 0 7 17.5Z'],
 export:['M12 4v11M8 11l4 4 4-4M5 19h14'],
 device:['M5 5h14a1.5 1.5 0 0 1 1.5 1.5V15A1.5 1.5 0 0 1 19 16.5H5A1.5 1.5 0 0 1 3.5 15V6.5A1.5 1.5 0 0 1 5 5Z','M8.5 20h7M12 16.5V20'],
 secret:['M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z','M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10']
};
function glyph(kind,dim){return '<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" style="color:'+(dim?'#c9c8d0':'#f0c44a')+';stroke:currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">'+(glyphPaths[kind]||[]).map(d=>'<path d="'+d+'"/>').join('')+'</svg>';}
function btn(label,act,cls,disabled){return '<button type="button" class="account-btn '+(cls||'')+'" data-action="'+act+'" '+(disabled?'disabled':'')+'>'+label+'</button>';}
function notify(msg){const e=$('notice');if(e)e.textContent=msg;}
function noteAction(msg){notify('Mock only — '+msg+'. No application or account operation occurred.');}
function welcome(){
const choices=[['Continue without account','Keep everything local on this device.','continue-local'],['Sign in','Bring your Colosseum back on this device.','sign-in'],['Create account','Start syncing your personal Colosseum.','create-account']];
return '<div class="wallpaper onboard-wallpaper"></div><div class="welcome-flow"><div class="welcome-content">'+
'<section class="intro"><div class="eyebrow">COLOSSEUM · WELCOME</div><h1 class="serif">Welcome to Colosseum.</h1><div class="rule"></div>'+
'<p class="detail">Use Colosseum completely locally, or sign in to carry your Collection, progress, reader setup, preferences, and other personal choices between devices.</p></section>'+
'<section class="welcome-panel glass"><p class="kicker">START</p><h2>How do you want to begin?</h2><p class="sub">An account is optional. You can change your mind later.</p>'+
choices.map(a=>'<button type="button" class="welcome-choice" data-action="'+a[2]+'"><span><span class="title">'+a[0]+'</span><span class="description" style="display:block">'+a[1]+'</span></span></button>').join('')+
'<p class="welcome-foot">Accounts are optional. Sign in later whenever you want sync.</p></section></div></div>';
}
const profileRows=[
['collections','Your Collections','Titles you keep together'],['downloads','Downloads','Everything saved on this device'],['extensions','Extensions','Add-ons and sources for every world'],
['colosseum','Your Colosseum','History, highlights and stats'],['trackers','Trackers','Connections to other libraries'],['stremio','Stremio','Bring your Stremio library across'],
['account','Account','Sign in, devices and security']];
const worldTabs=[['Theatre','projector-theatre.svg'],['Tankoban','comic-book.svg'],['Biblio','book-library.svg'],['Universes','universes-atom.svg'],['Profile','your-colosseum-laurel.svg']];
function profileBackdrop(){
const list=profileRows.map(a=>'<button type="button" class="profile-link" data-action="'+(a[0]==='account'?'toggle-flyout':'inert-page')+'"><span><strong>'+a[1]+'</strong><small>'+a[2]+'</small></span><span class="drawn-chevron" aria-hidden="true"></span></button>').join('');
return '<div class="profile-backdrop"><div class="profile-scroll"><div class="profile-column"><p class="eyebrow">COLOSSEUM</p><h1>Profile</h1>'+list+'</div></div>'+
'<nav class="world-tabs" aria-label="Worlds">'+worldTabs.map(a=>'<button type="button" data-action="inert-page" class="world-tab '+(a[0]==='Profile'?'active':'')+'">'+icon(a[1],a[0])+'<span>'+a[0]+'</span></button>').join('')+'</nav></div>';
}
function desktopBackdrop(){
const dt=new Date(),hh=dt.getHours()%12||12,ampm=dt.getHours()<12?'AM':'PM',mm=String(dt.getMinutes()).padStart(2,'0');
const date=dt.toLocaleDateString('en-US',{weekday:'long',month:'long',day:'numeric'});
const items=['Tankoban','Biblio','Theatre','Universes','Feria','Vinyl'];
const pills=items.map(n=>'<span class="world-pill '+(['Feria','Vinyl'].includes(n)?'soon':'')+'">'+n+(['Feria','Vinyl'].includes(n)?'<span class="soon-chip">SOON</span>':'')+'</span>').join('');
const sys=(file,label,action)=>'<button type="button" class="desktop-sys" data-action="'+(action||'inert-page')+'" aria-label="'+esc(label)+'">'+icon(file,label)+'</button>';
const medallion='<button class="flyout-trigger '+(state.mode==='local'?'local':'')+'" type="button" data-action="toggle-flyout" aria-label="Account">'+(state.mode==='local'?'Device':initial())+'</button>';
return '<div class="wallpaper"></div><div class="desktop-underlay"><div class="chrome">'+
'<span class="desktop-clock"><span class="desktop-hour">'+hh+':'+mm+'</span><span class="desktop-ampm">'+ampm+'</span><span class="desktop-date">'+date+'</span></span>'+
'<span class="worlds">'+pills+'</span><span class="desktop-icons">'+
sys('update.svg','Updates')+sys('trackers.svg','Trackers')+medallion+sys('access-point.svg','Share on your Wi-Fi')+sys('settings.svg','Wallpaper')+
sys('minimize.svg','Minimize','window-minimize')+sys('fullscreen.svg','Enter fullscreen','window-fullscreen')+sys('power.svg','Quit Colosseum','window-quit')+
'</span></div></div>';
}
function flyout(){
const phone=Math.min(w,h)<600;
const backdrop=phone?profileBackdrop():desktopBackdrop();
let content=backdrop;
if(!state.open)return content;
const dotColor=state.scenario==='retry'?'#e0564b':(state.mode==='offline'?'#f0df9a':'#7ec97e');
const sync=state.scenario==='retry'?'Sync needs attention':state.scenario==='pending'?'Syncing — 2 changes pending':state.mode==='offline'?'Waiting for the account service…':'All changes synced';
const retry=state.scenario==='retry';
const nav=signed()?[['Account ›','profile'],['Devices ›','devices']]:[['Data & privacy','privacy']];
content+='<div class="flyout-layer"><button class="flyout-dismiss" data-action="close-flyout" aria-label="Close flyout"></button>'+
'<section class="flyout-card" role="dialog" aria-label="Account"><div class="sheen"></div>'+
'<div class="identity"><span class="mini-medallion">'+esc(initial())+'</span><span class="identity-text"><div class="identity-name">'+esc(signed()?username():'Not signed in')+'</div><div class="identity-sub">Colosseum account</div></span></div>'+
(signed()?'<div class="sep"></div><div class="sync"><span class="dot" style="background:'+dotColor+'"></span><span>'+sync+'</span></div><div class="sep"></div>':'')+
'<nav class="nav-wrap">'+nav.map(x=>'<button type="button" class="nav-row" data-action="open-section" data-section="'+x[1]+'">'+x[0]+'</button>').join('')+'</nav>'+
(retry&&signed()?'<button type="button" class="retry-btn" data-action="retry">Retry sync</button>':'')+
'<button type="button" class="session-btn" data-action="session">'+(signed()?'Sign out':'Sign in')+'</button></section></div>';
requestAnimationFrame(positionFlyout);
return content;
}
function positionFlyout(){
const card=document.querySelector('.flyout-card');if(!card)return;
const trigger=document.querySelector('.flyout-trigger');
const frame=document.querySelector('#frame');
const rect=frame.getBoundingClientRect(),scale=rect.width/w||1;
const margin=16,anchorR=trigger?(trigger.getBoundingClientRect().right-rect.left)/scale:w-16;
const anchorB=trigger?(trigger.getBoundingClientRect().bottom-rect.top)/scale:76;
const height=card.offsetHeight;
const x=Math.max(margin,Math.min(w-card.offsetWidth-margin,anchorR-card.offsetWidth));
const y=Math.max(margin+state.inTop,Math.min(h-height-margin-state.inBottom,anchorB+8));
card.style.left=Math.round(x)+'px';card.style.top=Math.round(y)+'px';
}
function profile(){
const user=signed()?username():'';
const names=['Initial','Laurel','Column','Book','Screen','Panels','Custom'];const ids=['initial','laurel','column','book','screen','panels','custom'];
return '<h2>Profile</h2><section class="profile-hero"><div class="hero-medallion">'+avatar(state.avatar,initial())+'</div><div style="min-width:0">'+
'<div class="hero-username">'+esc(user)+'</div><div class="hero-account-type">Colosseum account</div><div style="margin-top:20px;width:40px" class="hairline"></div>'+
'<div class="hero-check"><i>✓</i>'+names[ids.indexOf(state.avatar)]+' medallion</div></div></section>'+
'<div class="avatar-header"><span>Avatar</span><span class="builtin">BUILT-IN</span></div><div class="avatar-grid">'+
names.map((name,i)=>'<button type="button" class="avatar-choice '+(state.avatar===ids[i]?'selected':'')+'" data-action="avatar" data-id="'+ids[i]+'" '+(!signed()||state.mode==='offline'||ids[i]==='custom'?'disabled':'')+' aria-label="'+(ids[i]==='custom'?'Custom avatar coming later':'Use '+name+' avatar')+'"><span class="ring" style="color:'+(state.avatar===ids[i]?'#f0c44a':'#c9c8d0')+'">'+avatar(ids[i],initial())+'</span><span>'+ (ids[i]==='custom'?'Custom  Later':name)+'</span></button>').join('')+
'</div><div class="username-edit"><div class="row-head" style="color:#c9c8d0">Username</div><div class="field-block"><label>Username<input id="usernameDraft" name="usernameDraft" value="'+esc(state.usernameDraft)+'" '+(state.mode!=='signed'?'disabled':'')+'></label>'+
btn('Save changes','save-user','primary',state.mode!=='signed'||!state.usernameDraft.trim()||state.usernameDraft===username())+'</div></div>';
}
function sectionRow(glyph,title,detail,actions){
return '<div class="inner-row"><div class="label-row"><div class="row-left"><span class="icon-box" aria-hidden="true">'+glyph+'</span><div class="copy"><div class="row-head">'+title+'</div><div class="row-detail">'+detail+'</div></div></div>'+
(actions?'<div class="row-actions">'+actions+'</div>':'')+'</div></div>';}
function security(){
const local=state.mode!=='signed';
const approval=state.scenario==='approval';
let out='<h2>Security</h2><div class="section-line"></div>';
if(approval){
out+='<div class="inner-row"><div class="section-kicker" style="color:#f0c44a">NEW DEVICE</div><div class="label-row"><div><div style="font-size:24px;font-weight:600">New sign-in waiting for approval</div><div class="row-detail">Colosseum sign-in</div><div class="row-detail">A Colosseum sign-in is waiting for your decision.</div><div class="small" style="margin-top:10px">Pending</div></div>'+
'<div class="row-actions">'+btn('Deny','approve-deny','',local)+btn('Approve','approve-yes','primary',local)+'</div></div></div>';
}else{
out+=sectionRow(glyph('shield'),'New-device protection','New sign-ins need approval from a trusted Colosseum device, or the recovery key. <br><span style="font-size:10px">Protection is '+(state.protection?'on':'off')+'</span>',
'<button type="button" class="switch '+(state.protection?'on':'')+'" data-action="protection" aria-label="Toggle new-device protection" aria-pressed="'+state.protection+'" '+(local?'disabled':'')+'></button>');
}
out+=sectionRow(glyph('lock'),'Password','Change the password used to sign in to this Colosseum account.',state.passwordExpanded?'':btn('Change','password-toggle','',local));
if(state.passwordExpanded)out+='<div class="inline-panel"><div class="fields"><label>Current password<input type="password" autocomplete="off" placeholder=""></label><label>New password<input type="password" autocomplete="off"></label><label>Confirm new password<input type="password" autocomplete="off"></label></div><div class="inline-actions">'+btn('Cancel','password-toggle')+btn('Save changes','password-save','primary')+'</div></div>';
out+=sectionRow(glyph('logout'),'Sign out everywhere','End every Colosseum account session, including this one.',state.logoutConfirm?'':btn('Sign out everywhere','logout-toggle','',local));
if(state.logoutConfirm)out+='<div class="inline-panel"><p>Every signed-in device will need to sign in again.</p><div class="inline-actions">'+btn('Cancel','logout-toggle')+btn('Sign out everywhere','logout-demo','primary')+'</div></div>';
return out;
}
function devices(){
const people=signed()?[['Luffy’s computer','Windows · last seen now',true],['OnePlus','Android · last seen 1 hour ago',false],['Tablet','Android · last seen 1 day ago',false]]:[];
if(state.stress&&people.length)people[1][0]='OnePlus_with_an_extremely_long_device_name_that_must_wrap_correctly';
let out='<h2 style="display:flex;justify-content:space-between;align-items:center;gap:10px">Devices '+btn('Refresh','refresh-devices','',state.mode!=='signed')+'</h2>'+
'<div class="summary-strip"><div><div class="summary-number">'+people.length+'</div><div class="small">'+(people.length===1?'trusted device':'trusted devices')+'</div></div>'+
'<p class="summary-note">Revoking a device ends its account access. It can sign in again later if you approve it.</p></div>'+
'<div class="section-kicker" style="display:flex;justify-content:space-between"><span>Trusted devices</span><span>'+people.length+' DEVICES</span></div><div class="section-line"></div>';
if(!people.length)out+='<p class="small" style="padding:30px 0">No trusted devices were returned by the account service.</p>';
people.forEach((person,i)=>{
out+='<div class="trust-row"><div class="device-main"><span class="icon-box device-icon '+(person[2]?'current-device':'')+'">'+glyph('device',!person[2])+'</span><div style="min-width:0;flex:1"><div class="device-name text-wrap">'+esc(person[0])+' '+(person[2]?'<span class="status-mark">THIS DEVICE</span>':'')+'</div><div class="device-meta">'+person[1]+'</div></div></div>'+
(person[2]?'':btn('Revoke','revoke-start-'+i))+'</div>';
if(state.revoke===String(i))out+='<div class="inline-panel"><p>Revoke '+esc(person[0])+'? It will lose account access and need to sign in again.</p><div class="inline-actions">'+btn('Cancel','revoke-cancel')+btn('Revoke','revoke-confirm','danger')+'</div></div>';
});
return out;
}
function recovery(){
let out='<h2>Recovery</h2><div class="recovery-hero"><div class="key-mark" aria-hidden="true">'+glyph('key')+'</div><div style="min-width:0"><h3>Your way back in.</h3>'+
'<p>Your recovery key can reset your password if you lose access. Colosseum showed the current key once when it was created and does not keep it available for reveal here.</p>'+
'<div class="pill-note">Only the newest recovery key works.</div></div></div>';
out+=sectionRow(glyph('replace',true),'Replace recovery key','Generate a new recovery key if the old one is lost, exposed, or no longer somewhere you trust.',state.recoveryExpanded?'':btn('Replace key','recovery-toggle','',state.mode!=='signed'));
if(state.recoveryExpanded)out+='<div class="inline-panel"><p style="color:#f7f7f5">The old key will stop working.</p><p>After replacement, save the new key before leaving the one-time screen.</p>'+
'<label class="single-field">Current password<input type="password" autocomplete="off"></label><div class="inline-actions">'+btn('Cancel','recovery-toggle')+btn('Generate new key','recovery-request','primary')+'</div></div>';
out+=sectionRow(glyph('recovery',true),'If you lose your password','Sign out and choose account recovery. Your username, recovery key, and a new password are enough to recover the account.','');
return out;
}
function privacySetting(iconText,title,description,actionLabel,action,swKey){
const isSwitch=swKey!==undefined;
return sectionRow(iconText,title,description,
btn(actionLabel,action)+' '+(isSwitch?'<button type="button" class="switch '+(state[swKey]?'on':'')+'" data-action="switch-'+swKey+'" aria-label="'+esc(title)+'" aria-pressed="'+state[swKey]+'"></button>':''));
}
function facts(items){return items.map(x=>'<div class="fact"><strong>'+x[0]+'</strong><small>'+x[1]+'</small></div>').join('');}
function privacy(){
let out='<h2>Data &amp; privacy</h2><div class="section-line"></div><div class="section-kicker">History</div>';
out+=privacySetting(glyph('search',true),'Search history','Remember searches on this device. Search history never needs to leave this machine.','Clear history','search-confirm','searchHistory');
if(state.searchConfirm)out+='<div class="inline-panel"><p>Clear remembered searches from this device?</p><div class="inline-actions">'+btn('Cancel','search-confirm')+btn('Clear history','search-confirm-action','primary')+'</div></div>';
out+=privacySetting(glyph('activity',true),'Your Colosseum activity','Keep watch and reading activity for Monthly Portrait, recent activity, completion history and personal media insights.','Clear activity','activity-confirm','activityHistory');
if(state.activityConfirm)out+='<div class="inline-panel"><p>Clear your stored watch and reading activity? Continue progress and Collection are not included.</p><div class="inline-actions">'+btn('Cancel','activity-confirm')+btn('Clear activity','activity-confirm-action','primary')+'</div></div>';
out+=sectionRow(glyph('sync',true),'Cross-device history','Sync watch and reading history between your signed-in devices. Turning this off can still leave activity available locally.',
'<button type="button" class="switch '+(state.syncHistory?'on':'')+'" data-action="switch-syncHistory" aria-label="Cross-device history" aria-pressed="'+state.syncHistory+'"></button>');
out+='<div class="section-kicker">Your account data</div>';
out+=sectionRow(glyph('export',true),'Export my data','Create a portable copy of the account data Colosseum holds for you.',btn('Request export','export'));
out+='<div class="section-line"></div><h3 class="map-title">What Colosseum keeps where</h3><p class="small" style="margin:4px 0 18px">These controls sit on top of a simple boundary: portable account state can follow you, while machine-specific and sensitive data stays outside ordinary sync.</p>'+
'<div class="privacy-map"><section class="map-lane"><strong>Can follow your account</strong><p>Portable state with a safe sync contract.</p>'+
facts([['Continue progress','Portable resume points and progress.'],['Collection','Titles and shelves you save.'],['History','When cross-device history is enabled.'],['Portable preferences','Account-level choices with approved owners.']])+
'</section><section class="map-lane local"><strong>Stays on this device</strong><p>Machine-owned data that ordinary sync does not carry.</p>'+
facts([['Search history','Kept locally when search history is enabled.'],['Window &amp; session state','Open sessions, geometry, PiP and machine state.'],['Paths &amp; media','Local files and filesystem locations.'],['Downloads &amp; caches','Machine-local acquisition and rebuildable data.']])+'</section></div>'+
'<div class="secret-boundary"><span class="icon-box">'+glyph('secret',true)+'</span><div><div class="row-head">Secrets are a separate boundary.</div><div class="row-detail">Passwords, recovery keys, session tokens and API credentials do not enter ordinary account sync. Filesystem paths and secret-bearing fields are blocked from those payloads too.</div></div></div>'+
'<div class="section-kicker">Account</div><div class="danger-zone"></div>'+
sectionRow(glyph('secret',true),'Delete Colosseum account','Permanently delete the account and its server-side account data. Local media files on your devices are not part of account deletion.',btn('Delete account','delete-confirm','danger'));
if(state.deleteConfirm)out+='<div class="inline-panel"><p style="font-weight:600;color:#f7f7f5">This is permanent.</p><p>Deleting the account removes its server-side identity and portable cloud data. Local media files stay on your devices. Provider backups follow the provider’s retention policy; Colosseum keeps only a capability-protected retry receipt for up to 30 days.</p>'+
'<label class="single-field">Current password<input type="password" autocomplete="off"></label><div class="inline-actions">'+btn('Cancel','delete-confirm')+btn('Continue','delete-demo','danger')+'</div></div>';
return out;
}
const tabs=[['profile','Profile','◌'],['security','Security','◇'],['devices','Devices','▣'],['recovery','Recovery','↶'],['privacy','Data & privacy','◫']];
function centre(){
const accountName=signed()?username():'Not signed in';
const nav=tabs.map(row=>'<button type="button" role="tab" class="rail-tab '+(state.section===row[0]?'active':'')+'" data-action="tab" data-section="'+row[0]+'" aria-selected="'+(state.section===row[0])+'"><span class="rail-glyph">'+row[2]+'</span><span>'+row[1]+'</span></button>').join('');
const content={profile:profile,security:security,devices:devices,recovery:recovery,privacy:privacy}[state.section]();
const windowIcons='<div class="centre-window-controls">'+[['minimize.svg','Minimize','window-minimize'],['fullscreen.svg','Enter fullscreen','window-fullscreen'],['power.svg','Quit Colosseum','window-quit']].map(x=>'<button type="button" class="desktop-sys" aria-label="'+x[1]+'" data-action="'+x[2]+'">'+icon(x[0],x[1])+'</button>').join('')+'</div>';
return '<div class="centre-shell"><nav class="centre-rail" aria-label="Account Centre sections"><div class="rail-person"><span class="mini-medallion">'+esc(initial())+'</span>'+
'<span style="min-width:0"><div class="rail-name">'+esc(accountName)+'</div><div class="rail-sub">Colosseum account</div></span></div>'+
'<div class="rail-list" role="tablist">'+nav+'</div><button type="button" class="rail-sign" data-action="session">'+(signed()?'Sign out of Colosseum':'Sign in to Colosseum')+'</button></nav>'+
'<main class="centre-main"><button type="button" class="centre-back" data-action="back-centre">‹ Back</button><div class="centre-content" role="tabpanel" aria-label="'+state.section+'">'+content+'</div></main>'+windowIcons+'</div>';
}
function resetExpanded(){state.passwordExpanded=false;state.logoutConfirm=false;state.recoveryExpanded=false;state.revoke='';state.searchConfirm=false;state.activityConfirm=false;state.deleteConfirm=false;}
function paint(){
const el=$('mock');
el.className='mock '+(Math.min(w,h)<600?'phone '+(w>h?'landscape':'portrait')+(w<380?' narrow':''):w===1280&&h===700?'desktop':'tablet');
if(w>=600&&w<1040)el.classList.add('compact-tablet');
el.style.setProperty('--fs',String(state.fontScale));
el.style.setProperty('--in-top',state.inTop+'px');
el.style.setProperty('--in-bottom',state.inBottom+'px');
el.style.setProperty('--in-left',state.inLeft+'px');
el.style.setProperty('--in-right',state.inRight+'px');
el.innerHTML=page==='account-welcome'?welcome():page==='account-flyout'?flyout():centre();
if(page==='account-flyout')requestAnimationFrame(positionFlyout);
if($('sectionControl'))$('sectionControl').value=state.section;
if($('flyState'))$('flyState').value=state.open?'open':'closed';
}
function resize(){
w=Math.max(320,Number($('vWidth').value)||412);
h=Math.max(240,Number($('vHeight').value)||900);
state.inTop=Number($('inTop').value)||0;
state.inBottom=Number($('inBottom').value)||0;
state.inLeft=Number($('inLeft').value)||0;
state.inRight=Number($('inRight').value)||0;
state.fontScale=Number($('fontScale').value)||1;
const frame=$('frame'),scaler=$('scaler');
frame.style.width=w+'px';frame.style.height=h+'px';
const bay=$('previewBay'),bw=capture?window.innerWidth:Math.max(350,bay.clientWidth-16),bh=capture?window.innerHeight:Math.max(280,bay.clientHeight-16);
const ratio=capture?1:Math.min(1,bw/w,bh/h);
frame.style.transform='scale('+ratio+')';
scaler.style.width=(w*ratio)+'px';scaler.style.height=(h*ratio)+'px';
paint();
}
function setView(v){
if(!presets[v])return;
state.v=v;const p=presets[v];$('vWidth').value=p[0];$('vHeight').value=p[1];resize();
}
function updateControls(){
$('preset').value=state.v;
$('mode').value=state.mode;
$('scenario').value=state.scenario;
$('stress').checked=state.stress;
$('fontScale').value=String(state.fontScale);
$('inTop').value=state.inTop;$('inBottom').value=state.inBottom;
$('inLeft').value=state.inLeft;$('inRight').value=state.inRight;
$('sectionControl').value=state.section;
$('flyState').value=state.open?'open':'closed';
}
function hrefFor(name,extras){
const u=new URL(name+'.html',location.href);u.search='';
u.searchParams.set('v',state.v);u.searchParams.set('mode',state.mode);
if(extras)Object.entries(extras).forEach(([key,val])=>u.searchParams.set(key,val));
return u.href;
}
function handleAction(a,el){
if(a==='continue-local'){state.mode='local';location.href=hrefFor('account-flyout',{open:'1'});return;}
if(a==='sign-in'||a==='create-account'){noteAction((a==='sign-in'?'Sign in':'Create account')+' would open the existing onboarding route');return;}
if(a==='inert-page'){noteAction('this existing shell or world action is outside the account mock');return;}
if(a.startsWith('window-')){noteAction('window operation '+a.substring(7)+' was not performed');return;}
if(a==='toggle-flyout'||a==='close-flyout'){state.open=a==='close-flyout'?false:!state.open;paint();return;}
if(a==='retry'){noteAction('Retry sync would contact the account service');return;}
if(a==='session'){noteAction(signed()?'Sign out would call logoutCurrent':'Sign in would open the existing account sign-in route');return;}
if(a==='open-section'){location.href=hrefFor('account-centre',{section:el.dataset.section});return;}
if(a==='back-centre'){location.href=hrefFor('account-flyout',{open:'1'});return;}
if(a==='tab'){state.section=el.dataset.section;resetExpanded();paint();return;}
if(a==='avatar'){state.avatar=el.dataset.id;paint();noteAction('avatar choice is preview-only, not setBuiltinAvatar');return;}
if(a==='save-user'){noteAction('Save changes would request renameUsername; no value was saved');return;}
if(a==='password-toggle'){state.passwordExpanded=!state.passwordExpanded;paint();return;}
if(a==='password-save'){noteAction('Password change request was not sent');return;}
if(a==='logout-toggle'){state.logoutConfirm=!state.logoutConfirm;paint();return;}
if(a==='logout-demo'){noteAction('Global sign-out was not sent');return;}
if(a==='protection'){noteAction('New-device protection toggle intent not sent; authoritative switch remains '+(state.protection?'on':'off'));return;}
if(a.startsWith('approve-')){noteAction('Device approval decision was not sent');return;}
if(a==='refresh-devices'){noteAction('Trusted device refresh not sent');return;}
if(a.startsWith('revoke-start-')){state.revoke=a.split('-').at(-1);paint();return;}
if(a==='revoke-cancel'){state.revoke='';paint();return;}
if(a==='revoke-confirm'){noteAction('Device revocation not sent; row intentionally remains visible');return;}
if(a==='recovery-toggle'){state.recoveryExpanded=!state.recoveryExpanded;paint();return;}
if(a==='recovery-request'){noteAction('Recovery key replacement was not requested; no secret generated');return;}
if(a.startsWith('switch-')){noteAction('Privacy switch change intent was not sent; authoritative state remains unchanged');return;}
if(a==='search-confirm'){state.searchConfirm=!state.searchConfirm;paint();return;}
if(a==='activity-confirm'){state.activityConfirm=!state.activityConfirm;paint();return;}
if(a==='search-confirm-action'||a==='activity-confirm-action'){noteAction('History clear not sent; data unchanged');return;}
if(a==='export'){noteAction('Account data export request not sent');return;}
if(a==='delete-confirm'){state.deleteConfirm=!state.deleteConfirm;paint();return;}
if(a==='delete-demo'){noteAction('Account deletion was not requested');return;}
}
function init(){
updateControls();
setView(state.v);
document.addEventListener('click',ev=>{const el=ev.target.closest('[data-action]');if(!el)return;ev.preventDefault();handleAction(el.dataset.action,el);});
$('preset').addEventListener('change',e=>setView(e.target.value));
['vWidth','vHeight','inTop','inBottom','inLeft','inRight','fontScale'].forEach(id=>$(id).addEventListener('change',resize));
$('mode').addEventListener('change',e=>{state.mode=e.target.value;resetExpanded();state.usernameDraft=username();paint();});
$('scenario').addEventListener('change',e=>{state.scenario=e.target.value;paint();});
$('stress').addEventListener('change',e=>{state.stress=e.target.checked;paint();});
$('sectionControl').addEventListener('change',e=>{state.section=e.target.value;resetExpanded();paint();});
$('flyState').addEventListener('change',e=>{state.open=e.target.value==='open';paint();});
document.addEventListener('input',e=>{if(e.target.id==='usernameDraft'){state.usernameDraft=e.target.value;const save=document.querySelector('[data-action="save-user"]');if(save)save.disabled=state.mode!=='signed'||!e.target.value.trim()||e.target.value.trim()===username();}});
window.addEventListener('resize',resize);
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(page==='account-flyout'&&state.open){state.open=false;paint();}else if(page==='account-centre'){location.href=hrefFor('account-flyout',{open:'1'});}}});
if(capture)document.body.classList.add('capture');
document.fonts.ready.then(()=>{if(page==='account-flyout')positionFlyout();});
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();

