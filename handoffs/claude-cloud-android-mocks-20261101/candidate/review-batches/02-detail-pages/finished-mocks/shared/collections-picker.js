/* Local mock of approved collections/AddToCollectionsPopup.qml and its row/search/action components. */
(() => {
  const paths={collections:'<rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/>',close:'<path d="m6 6 12 12M6 18 18 6"/>',plus:'<path d="M12 5v14M5 12h14"/>',check:'<path d="m5 12 4 4L19 6"/>',search:'<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 4.5 4.5"/>',book:'<path d="M6 4h14v17H6a3 3 0 0 1-3-3V7a3 3 0 0 1 3-3Z"/><path d="M6 4v12h14M6 18h13"/>',film:'<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M7 3v18m10-18v18M3 8h4m-4 8h4M17 8h4m-4 8h4"/>',series:'<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 22h8M9 2l3 3 3-3"/>',comic:'<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 10h18M11 10v11M3 15h8M16 3v7"/>'};
  const icon=name=>'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+(paths[name]||paths.collections)+'</svg>';
  const esc=v=>String(v??'').replace(/[&<>"']/g,x=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[x]));
  const groups=[{id:'favorites',name:'Favorites',count:3},{id:'later',name:'For later',count:0},{id:'stories',name:'Stories to revisit',count:5}];
  const saved=new Map();let active=null;
  function close(){
    if(!active)return;const a=active;active=null;document.removeEventListener('keydown',a.keys,true);
    a.overlay.remove();a.inert.forEach(([e,old])=>e.inert=old);
    if(a.trigger?.isConnected)a.trigger.focus({preventScroll:true});
  }
  function open(options){
    close();const host=typeof options.host==='string'?document.querySelector(options.host):options.host;
    if(!host)throw Error('Collections picker requires its owned app frame');
    const key=options.key||options.kind+':'+options.title;
    const trigger=options.trigger||document.activeElement;
    const overlay=document.createElement('div');overlay.className='cp-overlay';
    const s=getComputedStyle(host),top=host.id==='safe-zone'?'0px':s.getPropertyValue('--inset-top')||s.getPropertyValue('--safe-top')||'0px',bottom=host.id==='safe-zone'?'0px':s.getPropertyValue('--inset-bottom')||s.getPropertyValue('--safe-bottom')||'0px';
    overlay.style.top=top;overlay.style.bottom=bottom;
    overlay.style.setProperty('--cp-scale',s.getPropertyValue('--fs')||s.getPropertyValue('--font-scale')||'1');
    overlay.innerHTML='<section class="cp-dialog" role="dialog" aria-modal="true" aria-labelledby="cp-title"><header class="cp-header"><h2 id="cp-title">Add to Collections</h2><button class="cp-button cp-close" aria-label="Close">'+icon('close')+'</button></header><p class="cp-note">Keep this title in one collection, or a few.</p><div class="cp-media"><img src="'+esc(options.cover)+'" alt=""><div><strong>'+esc(options.title)+'</strong><small>'+esc(options.kindLabel||options.kind)+' · '+esc(options.year||'')+'</small></div>'+icon(options.kind)+'</div><div class="cp-search">'+icon('search')+'<input id="cp-search" aria-label="Find a collection" placeholder="Find a collection"><button class="cp-button cp-clear" aria-label="Clear search" hidden>'+icon('close')+'</button></div><div class="cp-memberships"></div><button class="cp-button cp-new">'+icon('plus')+'<span>New collection</span></button><div class="cp-create" hidden><label for="cp-name">Collection name</label><input id="cp-name" maxlength="100" placeholder="Something worth keeping"><p class="cp-error" role="alert"></p></div><footer class="cp-footer"><span class="cp-private">Private, just for you</span><button class="cp-button cp-cancel">Cancel</button><button class="cp-button cp-primary cp-save">Save</button></footer></section>';
    const inert=[...host.children].map(e=>[e,e.inert]);inert.forEach(([e])=>e.inert=true);host.append(overlay);overlay.querySelector('.cp-dialog').style.padding=Math.min(560,host.clientWidth-32)<480?'20px':'26px';
    const a={host,overlay,trigger,inert,options,key,draft:new Set(saved.get(key)||[]),creating:false,keys:null};active=a;
    const q=sel=>overlay.querySelector(sel);
    const rows=()=>{
      const query=q('#cp-search').value.trim().toLowerCase();q('.cp-clear').hidden=!query;
      q('.cp-memberships').innerHTML=groups.filter(g=>g.name.toLowerCase().includes(query)).map(g=>'<button class="cp-membership" role="checkbox" aria-checked="'+a.draft.has(g.id)+'" data-cp-id="'+g.id+'"><span class="cp-cover">'+(g.count?'<img src="'+esc(options.cover)+'" alt="">':icon('collections'))+'</span><span class="cp-row-copy"><strong>'+esc(g.name)+'</strong><small>'+g.count+' titles</small></span><span class="cp-check">'+(a.draft.has(g.id)?icon('check'):'')+'</span></button>').join('')||'<p class="cp-empty">No collections match that name.<br>Create one below, or clear your search.</p>';
    };
    const mode=creating=>{
      a.creating=creating;q('.cp-search').hidden=creating;q('.cp-memberships').hidden=creating;q('.cp-new').hidden=creating;q('.cp-create').hidden=!creating;q('.cp-private').hidden=creating;
      q('.cp-note').textContent=creating?'Give it a name. Make it yours over time.':'Keep this title in one collection, or a few.';
      q('.cp-save').textContent=creating?'Create & add':'Save';q('.cp-error').textContent='';
      if(creating){q('#cp-name').value=q('#cp-search').value.trim();q('#cp-name').focus();}else{q('#cp-name').value='';q('.cp-new').focus();}
    };
    q('.cp-close').onclick=close;
    q('.cp-cancel').onclick=()=>a.creating?mode(false):close();
    q('.cp-new').onclick=()=>mode(true);
    q('#cp-search').oninput=()=>{rows();q('.cp-memberships').scrollTop=0;};
    q('.cp-clear').onclick=()=>{q('#cp-search').value='';rows();q('#cp-search').focus();};
    q('.cp-memberships').onclick=e=>{const b=e.target.closest('[data-cp-id]');if(!b)return;const id=b.dataset.cpId;a.draft.has(id)?a.draft.delete(id):a.draft.add(id);rows();q('[data-cp-id="'+id+'"]')?.focus({preventScroll:true});};
    const save=()=>{
      if(a.creating){const name=q('#cp-name').value.trim();if(!name){q('.cp-error').textContent='Give your collection a name.';q('#cp-name').focus();return;}
        const id='local-'+groups.length;groups.push({id,name,count:1});a.draft.add(id);
      }
      saved.set(key,new Set(a.draft));options.onSaved?.([...a.draft]);close();
    };
    q('.cp-save').onclick=save;q('#cp-name').oninput=()=>q('.cp-error').textContent='';
    q('#cp-name').onkeydown=e=>{if(e.key==='Enter'&&!e.isComposing){e.preventDefault();save();}};
    a.keys=e=>{
      if(e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();close();return;}
      if(e.key!=='Tab')return;
      const controls=[...overlay.querySelectorAll('button,input')].filter(e=>!e.disabled&&e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden');
      const i=controls.indexOf(document.activeElement),next=e.shiftKey?(i<=0?controls.length-1:i-1):(i+1)%controls.length;
      e.preventDefault();e.stopImmediatePropagation();controls[next]?.focus();
    };
    document.addEventListener('keydown',a.keys,true);rows();q('#cp-search').focus();
  }
  window.BatchCollections={open,close,reset(){close();saved.clear();groups.splice(3);},inspect(){return{open:!!active,creating:active?.creating,committed:[...saved.entries()].map(([k,v])=>[k,[...v]])};}};
})();

