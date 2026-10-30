/* Anonymous viewer. No account cookies, persistent storage, external art or HTML rendering. */
'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const screens = ['loading', 'locked', 'failure', 'opening', 'collection'];
  const match = location.pathname.match(/^\/collections\/s\/([A-Za-z0-9_-]{20,128})\/?$/);
  const shareId = match ? match[1] : '';
  let edition = null;
  let request = null;
  let generation = 0;
  let busy = false;
  let protectedLink = false;
  const text = (node, value) => { node.textContent = value || ''; };
  const string = (value, max) => typeof value === 'string' && value.length <= max;
  const presetNames = new Set(['midnight', 'paper', 'sea']);
  const memberLimits = {world:32,kind:32,source:64,source_id:256,edition_id:256,title:300,creator:200,year:32,note:500};

  function show(name, focus) {
    for (const screen of screens) $(screen).hidden = screen !== name;
    if (focus) $(focus).focus({preventScroll:true});
  }
  function failure(message, retry = true) {
    edition = null;
    text($('failure-message'), message);
    $('retry').hidden = !retry;
    show('failure');
  }
  function validate(value) {
    const s = value && value.snapshot;
    if (!s || value.share_id !== shareId || !['collection','gift'].includes(value.kind)
        || !Number.isSafeInteger(value.revision) || value.revision < 1 || value.revoked === true
        || s.schema_version !== 1 || !string(s.name, 100) || !string(s.description, 300)
        || !Array.isArray(s.members) || s.members.length > 500) throw new Error('invalid_edition');
    const p = s.presentation;
    if (!p || !presetNames.has(p.preset) || !string(p.dedication, 2000)
        || !string(p.signature, 100) || !string(p.cover_data, 350000)
        || (p.cover_data && !/^data:image\/(png|jpeg);base64,[A-Za-z0-9+/]+=*$/.test(p.cover_data))) throw new Error('invalid_edition');
    for (const m of s.members) {
      if (!m || !['world','kind','source','source_id','edition_id','title','creator','year','note']
          .every(key => string(m[key], memberLimits[key]))
          || !['canonical_id','canonical_source_id'].every(key => string(m[key], 40))
          || !['album_source_id','album_browse_id'].every(key => m[key] === undefined || string(m[key], 256))
          || (m.comic_post_ids !== undefined && (!Array.isArray(m.comic_post_ids) || m.comic_post_ids.length > 500 || !m.comic_post_ids.every(id => typeof id === 'string' && /^[1-9][0-9]{0,19}$/.test(id))))) throw new Error('invalid_edition');
    }
    // Copy only the public schema even if a future server adds unrelated fields.
    return {share_id:value.share_id, kind:value.kind, revision:value.revision,
      snapshot:{schema_version:1,name:s.name,description:s.description,
        presentation:{preset:p.preset,dedication:p.dedication,signature:p.signature,cover_data:p.cover_data},
        members:s.members.map(m => ({...Object.fromEntries(['world','kind','source','source_id','edition_id','canonical_id','canonical_source_id','title','creator','year','note'].map(k=>[k,m[k]])), album_source_id:m.album_source_id || '', album_browse_id:m.album_browse_id || '', comic_post_ids:(m.comic_post_ids || []).slice()}))}};
  }
  function render(value) {
    edition = validate(value);
    const s = edition.snapshot, p = s.presentation;
    document.body.dataset.preset = p.preset;
    document.title = `${s.name} · Colosseum`;
    text($('opening-title'), s.name);
    text($('dedication'), p.dedication || 'A few things, chosen for you.');
    text($('signature'), p.signature);
    text($('collection-title'), s.name);
    text($('description'), s.description);
    text($('collection-signature'), edition.kind === 'gift' ? p.signature : '');
    text($('edition-label'), edition.kind === 'gift' ? 'YOUR GIFT · A FIXED EDITION' : `SHARED COLLECTION · EDITION ${edition.revision}`);
    text($('member-count'), `${s.members.length} ${s.members.length === 1 ? 'title' : 'titles'} · in the creator’s order`);
    const cover = $('custom-cover');
    cover.hidden = !p.cover_data;
    if (p.cover_data) cover.src = p.cover_data; else cover.removeAttribute('src');
    const fragment = document.createDocumentFragment();
    for (const [i,m] of s.members.entries()) {
      const li = document.createElement('li'); li.className = 'member';
      const number = document.createElement('span'); number.className='ordinal'; number.setAttribute('aria-hidden','true'); text(number,String(i+1).padStart(2,'0'));
      const content = document.createElement('div');
      const title = document.createElement('h2'); text(title,m.title || 'Untitled');
      const meta = document.createElement('p'); meta.className='member-meta'; text(meta,[m.kind,m.creator,m.year].filter(Boolean).join(' · '));
      content.append(title,meta);
      if (m.note) { const note = document.createElement('p'); note.className='member-note'; text(note,m.note); content.append(note); }
      const source = document.createElement('p'); source.className='source-label';
      text(source, `${m.source}${m.edition_id ? ` · Edition: ${m.edition_id}` : ''} · Check availability in Colosseum`);
      content.append(source); li.append(number,content); fragment.append(li);
    }
    $('members').replaceChildren(fragment);
    $('empty').hidden = s.members.length !== 0;
    $('keep-panel').hidden = edition.kind !== 'gift';
    $('read-dedication').hidden = edition.kind !== 'gift';
    text($('download-status'),'');
    show(edition.kind === 'gift' ? 'opening' : 'collection');
  }
  async function readBounded(response) {
    const cap = 2097152;
    if (Number(response.headers.get('content-length')) > cap) throw new Error('invalid_edition');
    const reader = response.body.getReader();
    let length = 0; const chunks=[];
    for (;;) {
      const {done,value} = await reader.read(); if (done) break;
      length += value.byteLength;
      if (length > cap) { await reader.cancel(); throw new Error('invalid_edition'); }
      chunks.push(value);
    }
    const bytes = new Uint8Array(length); let offset=0;
    for (const chunk of chunks) { bytes.set(chunk,offset); offset+=chunk.byteLength; }
    return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
  }
  async function open(passphrase) {
    if (busy || !shareId) return;
    busy = true; $('unlock').disabled = true; $('unlock-form').setAttribute('aria-busy','true');
    text($('unlock-error'),''); $('passphrase').removeAttribute('aria-invalid');
    request = new AbortController(); const thisGeneration = ++generation;
    const timeout = setTimeout(() => request?.abort(), 20000);
    try {
      const supplied = typeof passphrase === 'string';
      const response = await fetch(`/v1/collections/shared/${shareId}${supplied ? '/open' : ''}`, {
        method:supplied?'POST':'GET', cache:'no-store', credentials:'omit', redirect:'error',
        headers:supplied?{'Content-Type':'application/json'}:{},
        body:supplied?JSON.stringify({passphrase}):undefined, signal:request.signal
      });
      // Do not retain the passphrase after handing it to the request.
      passphrase = undefined; $('passphrase').value='';
      const body = await readBounded(response);
      if (thisGeneration !== generation) return;
      if (response.status === 403) {
        protectedLink = true;
        show('locked','passphrase');
        if (supplied) { text($('unlock-error'),'That passphrase didn’t open this collection. Try again.'); $('passphrase').setAttribute('aria-invalid','true'); }
      } else if (response.status === 429) {
        if (protectedLink) { show('locked','passphrase'); text($('unlock-error'),'Too many attempts. Wait a little before trying again.'); }
        else failure('Too many requests. Wait a little before trying again.');
      } else if (response.status === 404 || response.status === 410) {
        failure('This link is no longer available. It may have been revoked, or the address may be incomplete.',false);
      } else if (!response.ok) {
        failure('The collection service couldn’t respond. Try again in a moment.');
      } else { protectedLink = !!body.protected || supplied; render(body); }
    } catch (error) {
      if (thisGeneration !== generation) return;
      failure(error.message === 'invalid_edition' ? 'This edition isn’t supported or is too large. Ask the sender to publish it again.' : 'Couldn’t reach the collection service. Check your connection and try again.');
    } finally {
      clearTimeout(timeout); $('passphrase').value='';
      if (thisGeneration === generation) { busy=false; request=null; $('unlock').disabled=false; $('unlock-form').removeAttribute('aria-busy'); }
    }
  }
  $('unlock-form').addEventListener('submit', event => {
    event.preventDefault();
    if (event.isComposing) return;
    if (!$('passphrase').value) { text($('unlock-error'),'Enter the passphrase the sender gave you.'); $('passphrase').focus(); return; }
    open($('passphrase').value);
  });
  $('show-passphrase').addEventListener('click', () => {
    const visible = $('passphrase').type === 'password';
    $('passphrase').type=visible?'text':'password'; text($('show-passphrase'),visible?'Hide':'Show'); $('show-passphrase').setAttribute('aria-pressed',String(visible));
  });
  $('explore').addEventListener('click', () => { show('collection','collection-title'); window.scrollTo({top:0,behavior:'instant'}); });
  $('read-dedication').addEventListener('click', () => { show('opening','explore'); window.scrollTo({top:0,behavior:'instant'}); });
  $('retry').addEventListener('click', () => { if(protectedLink) show('locked','passphrase'); else { show('loading'); open(); } });
  $('save-edition').addEventListener('click', () => {
    if (!edition || edition.kind !== 'gift') return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(edition)],{type:'application/json'}));
    const link = document.createElement('a'); link.href=url; link.download=`colosseum-gift-${shareId}.json`; link.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
    text($('download-status'),'Edition file prepared. It includes the personal messages and cover, but not the passphrase or media files. Keep it private if the gift is personal.');
  });
  window.addEventListener('pagehide', () => {
    ++generation; request?.abort(); request=null; busy=false; edition=null; $('passphrase').value='';
    $('members').replaceChildren(); $('custom-cover').removeAttribute('src');
    for (const id of ['opening-title','dedication','signature','collection-title','description','collection-signature']) text($(id),'');
    document.title='A collection · Colosseum'; show('loading');
  });
  window.addEventListener('pageshow', event => { if(event.persisted) open(); });
  if (shareId) open(); else failure('The collection address is incomplete. Ask the sender for the full link.',false);
})();
