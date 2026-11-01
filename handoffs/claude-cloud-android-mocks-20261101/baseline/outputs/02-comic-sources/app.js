/* Standalone local fitting study. No backend, torrent or network calls. */
(function () {
  'use strict';
  const presets = [
    ['412x900', 412, 900], ['354x792', 354, 792],
    ['900x412', 900, 412], ['792x354', 792, 354],
    ['1280x700', 1280, 700], ['800x1280', 800, 1280], ['1280x800', 1280, 800]
  ];
  const rows = [
    { sourceName:'Pirate Bay', confidence:'strong', title:'Batman - Year One (1987) Batman #404-407 [CBZ] [Digital]', coverage:true, evidence:['ISSUES'], uploader:'ComicsArchive', trustTier:1, sizeText:'456 MB', seeders:83 },
    { sourceName:'ExtraTorrents', confidence:'weak', title:'Batman - Gotham Knights (2000-2001) #001-012 Complete Digital Remaster [CBR] [HQ Scans] [English]', coverage:false, evidence:[], uploader:'', trustTier:5, sizeText:'1.3 GB', seeders:undefined },
    { sourceName:'Torrents-CSV', confidence:'possible', title:'Batman Year One Deluxe 1987-1988 [CBR] Four Issue Edition', coverage:true, evidence:[], uploader:'', trustTier:3, sizeText:'398 MB', seeders:26 },
    { sourceName:'Pirate Bay', confidence:'strong', title:'Batman - Year One (1987) Complete #404 #405 #406 #407 [CBZ] [Digital Remaster] Very Long Release Name', coverage:true, evidence:['ISSUES'], uploader:'ComicsArchive', trustTier:2, sizeText:'512 MB', seeders:17 }
  ];
  const archives = [
    {index:0, path:'Batman_Year_One_Collected/Batman_404-407_Year_One.cbz', ext:'CBZ', size:'318.4 MB'},
    {index:1, path:'Batman_Year_One_Collected/Batman_Year_One_extra_pages.cbr', ext:'CBR', size:'426.7 MB'},
    {index:2, path:'Batman_Year_One_Collected/Batman_Year_One_Archive_Scans.cb7', ext:'CB7', size:'292.1 MB'},
    {index:3, path:'Batman_Year_One_Collected/Batman_Year_One_Appendix.cbt', ext:'CBT', size:'105.8 MB'}
  ];
  const defaultState = document.body.dataset.defaultState || 'results';
  const qp = new URLSearchParams(location.search);
  const capture = qp.get('capture') === '1';
  const stateOptions = [
    ['results','Results / original rows'], ['weak','Weak-match confirmation open'],
    ['ambiguous','Ambiguous archive picker'], ['incomplete','Incomplete pack'],
    ['combined','Combined archive'], ['inspecting','Inspecting pack'],
    ['loading','Searching'], ['empty','No matching torrents'], ['closed','Picker closed']
  ];
  let state = qp.get('state') || defaultState;
  if (!stateOptions.some(function (p) {return p[0] === state;})) state = defaultState;
  let selected = rows[1];
  let visibleRows = rows.slice();
  let queryValue = '';
  let width = capture ? Number(qp.get('vw')) || innerWidth : 412;
  let height = capture ? Number(qp.get('vh')) || innerHeight : 900;
  let topInset = Math.max(0, Number(qp.get('top')) || 0);
  let bottomInset = Math.max(0, Number(qp.get('bottom')) || 0);
  let fontScale = Math.max(1, Math.min(1.3, Number(qp.get('scale')) || 1));
  const el = function (id) { return document.getElementById(id); };
  const esc = function (value) {return String(value == null ? '' : value).replace(/[&<>"']/g,function (c) {return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});};
  const statusName = function (s) {return (stateOptions.find(function (p) {return p[0] === s;}) || [s,s])[1];};
  if (capture) document.body.classList.add('capture');
  const options = function (entries, current) {
    return entries.map(function (item) {
      const value = Array.isArray(item) ? item[0] : item, label = Array.isArray(item) ? item[1] : item;
      return '<option value="' + esc(value) + '"' + (String(value) === String(current) ? ' selected' : '') + '>' + esc(label) + '</option>';
    }).join('');
  };
  const presetEntries = presets.map(function (p) {return [p[0],p[0].replace('x',' × ') + (p[0]==='1280x700'?' · desktop':'')];});
  el('appRoot').innerHTML =
    '<div class="review-shell"><aside class="review-sidebar" aria-label="Outside-mock review controls">' +
      '<h1>Comic collected-edition sources</h1><p class="description">Approved desktop styling, fitted to phone. These controls and notes are outside Colosseum.</p>' +
      '<div class="links"><a href="comic-sources.html"' + (defaultState==='results'?' aria-current="page"':'') + '>Sources</a><a href="comic-source-confirmation.html"' + (defaultState==='weak'?' aria-current="page"':'') + '>Confirmation & pack states</a><a href="index.html">Index</a></div>' +
      '<div class="group"><div class="field"><label for="preset">Viewport preset</label><select id="preset">' + options([['custom','Custom dimensions']].concat(presetEntries),capture?'custom':'412x900') + '</select></div>' +
      '<div class="numeric-row"><div class="field"><label for="widthInput">Width (logical px)</label><input id="widthInput" type="number" min="320" max="1920" value="412"></div>' +
      '<div class="field"><label for="heightInput">Height (logical px)</label><input id="heightInput" type="number" min="240" max="1500" value="900"></div></div>' +
      '<div class="numeric-row"><div class="field"><label for="topInset">Top inset (px)</label><input id="topInset" type="number" min="0" max="70" value="0"></div>' +
      '<div class="field"><label for="bottomInset">Bottom inset (px)</label><input id="bottomInset" type="number" min="0" max="70" value="0"></div></div>' +
      '<div class="field"><label for="fontScale">Font scale: <output id="fontScaleValue">1.00×</output></label><input type="range" min="1" max="1.3" step="0.05" id="fontScale" value="1"></div></div>' +
      '<div class="group"><div class="field"><label for="stateSelect">Existing component state</label><select id="stateSelect">' + options(stateOptions,state) + '</select></div>' +
      '<p>Tap a source row or its round download glyph to demonstrate the original selection path. A weak match opens the original confirmation. Go back cancels it.</p>' +
      '<p><strong>Fixture only.</strong> Source titles, files, seeders and ISBN are illustrative. Gotham portal art is a real bundled project asset; no matching collected-edition cover is in the provided local asset tree.</p>' +
      '<p>Device sizes and safe areas are provisional. No system chrome is drawn. No provider requests, downloads or accounts are used.</p>' +
      '<div id="reviewStatus" role="status"></div></div></aside>' +
    '<main class="review-stage"><div class="stage-label">Local HTML fitting study · app boundary below; not a runtime capture</div>' +
      '<div id="viewport" class="app-viewport"><div class="underlay" aria-hidden="true"></div>' +
        '<section class="source-sheet" id="sourceSheet" aria-label="Comic alternate sources">' +
          '<div class="hero-art" aria-hidden="true"></div>' +
          '<button type="button" class="back-action" data-action="close" aria-label="Back"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5L8 12l6.5 6.5"/></svg><span>Back</span></button>' +
          '<div class="shell-flow">' +
            '<header class="title-block"><div class="eyebrow">ALTERNATE SOURCES · COLLECTED EDITION</div>' +
              '<h2>Batman: Year One — Deluxe Edition</h2><div class="edition-line">Batman: Year One — Deluxe Edition&nbsp;&nbsp; · &nbsp;&nbsp; ISBN 9781401207526&nbsp;&nbsp; · &nbsp;&nbsp; Batman #404–407</div>' +
            '</header>' +
            '<form class="query-bar" id="searchForm"><input id="queryInput" class="query-input" type="text" autocomplete="off" placeholder="Search another title or ISBN" aria-label="Search another title or ISBN"><button class="search-button" type="submit">Search</button></form>' +
            '<section class="glass-panel" aria-label="Comic torrent results and pack inspection">' +
              '<div id="resultsView" class="panel-view"><header class="table-head"><div class="result-count" id="countText"></div><div class="provider-list">Pirate Bay · ExtraTorrents · Torrents-CSV</div></header><div class="rows-scroll" id="rowsScroll"></div></div>' +
              '<div id="stateView" class="panel-view hidden"></div>' +
            '</section>' +
          '</div>' +
          '<div id="weakLayer" class="weak-layer hidden" aria-hidden="true"><div class="weak-dialog" role="dialog" aria-modal="true" aria-labelledby="warningTitle">' +
            '<p id="warningTitle" class="warning">This release does not closely match the collected edition.</p>' +
            '<p id="pendingRelease" class="pending-release"></p>' +
            '<div class="actions"><button type="button" class="option-btn" data-action="cancel-weak">Go back</button>' +
            '<button type="button" class="option-btn gold-btn" data-action="confirm-weak">Choose anyway</button></div></div></div>' +
        '</section>' +
      '</div>' +
    '</main></div>';
  function rowHTML(r, index) {
    const label = r.confidence==='strong'?'STRONG MATCH':r.confidence==='possible'?'POSSIBLE MATCH':'WEAK MATCH';
    let chips = '';
    if (r.coverage) chips += '<span class="evidence-chip gold">FORMAT RANGE</span>';
    if (r.evidence.indexOf('ISSUES')>=0) chips += '<span class="evidence-chip">ISSUES</span>';
    if (r.uploader && Number(r.trustTier)<=2) chips += '<span class="evidence-chip gold">TRUSTED · ' + esc(r.uploader) + '</span>';
    const meta = [r.sizeText].concat(Number.isFinite(r.seeders) && r.seeders>=0 ? ['👤 '+r.seeders] : []).join(' &nbsp; · &nbsp; ');
    return '<article tabindex="0" role="button" class="source-row" data-row="' + index + '" aria-label="Choose ' + esc(r.title) + '">' +
      '<div class="source-tile" aria-hidden="true">' + esc(r.sourceName.charAt(0)) + '</div><div class="source-copy">' +
      '<div class="source-ident"><span class="source-name">' + esc(r.sourceName) + '</span><span class="confidence ' + r.confidence + '">' + label + '</span></div>' +
      '<div class="release-title">' + esc(r.title) + '</div><div class="evidence">' + chips + '</div><div class="release-meta">' + meta + '</div></div>' +
      '<span class="round-pick" aria-hidden="true">↓</span></article>';
  }
  function archiveHTML(r) {
    return '<article tabindex="0" role="button" class="archive-row" data-archive="' + r.index + '" aria-label="Choose ' + esc(r.path) + '">' +
      '<span class="archive-tile" aria-hidden="true">' + esc(r.ext) + '</span><div class="archive-copy"><div class="archive-name">' +
      esc(r.path) + '</div><div class="archive-size">' + esc(r.size) + '</div></div><span class="round-pick" aria-hidden="true">↓</span></article>';
  }
  function renderRows() {
    el('countText').textContent = visibleRows.length + (visibleRows.length===1?' result':' results');
    el('rowsScroll').innerHTML = visibleRows.map(rowHTML).join('');
  }
  function renderStatePanel() {
    let content = '';
    if (state==='ambiguous') {
      content = '<header class="table-head"><div class="archive-count">' + archives.length + ' comic archives</div><div class="archive-heading-right">CHOOSE ONE</div></header>' +
        '<div class="archives-scroll">' + archives.map(archiveHTML).join('') + '</div>';
    } else if (state==='inspecting') {
      content = '<div class="inspection"><h3>Inspecting pack…</h3><p>' + esc(selected.title) + '</p></div>';
    } else if (state==='combined') {
      content = '<div class="decision-content"><p>Only a combined archive covers this edition — it likely includes other editions too.</p>' +
        '<div class="actions"><button class="option-btn" data-action="back-results">Go back</button>' +
        '<button class="option-btn gold-btn" data-action="confirm-pack">Download whole archive anyway</button></div></div>';
    } else if (state==='incomplete') {
      content = '<div class="decision-content"><p>This pack is missing issues this edition needs.</p><p class="missing">Batman #405 &nbsp; · &nbsp; Batman #406</p>' +
        '<div class="actions"><button class="option-btn" data-action="back-results">Try another source</button>' +
        '<button class="option-btn" data-action="manual-search">Search manually</button></div></div>';
    } else if (state==='loading') {
      content = '<p class="empty-copy">Searching comic sources…</p>';
    } else if (state==='empty') {
      content = '<p class="empty-copy">No torrents matched this query. Try another title or ISBN.</p>';
    }
    el('stateView').innerHTML = content;
  }
  function renderState() {
    const resultsOn = state==='results' || state==='weak';
    const overlay = state==='weak';
    el('viewport').classList.toggle('closed',state==='closed');
    el('searchForm').classList.toggle('hidden',!resultsOn && state!=='loading' && state!=='empty');
    el('resultsView').classList.toggle('hidden',!resultsOn);
    el('stateView').classList.toggle('hidden',resultsOn || state==='closed');
    el('weakLayer').classList.toggle('hidden',!overlay);
    el('weakLayer').setAttribute('aria-hidden',overlay?'false':'true');
    el('pendingRelease').textContent = selected.title;
    renderRows(); renderStatePanel();
    el('stateSelect').value=state;
    el('reviewStatus').textContent = statusName(state) + ' · ' + width + '×' + height + ' logical px. Local demonstration only.';
    // QML does not force initial focus into this dialog; both actions remain keyboard reachable.
  }
  function setState(next) {
    state = next;
    if (state==='weak') selected=rows[1];
    if (state==='results') visibleRows=rows.slice();
    renderState();
  }
  function viewport() {
    width = Math.max(320,Math.min(1920,Number(width)||412));
    height = Math.max(240,Math.min(1500,Number(height)||900));
    const form = Math.min(width,height)<600 ? (width>height?'landscape':'portrait') : 'desktop';
    const node=el('viewport');
    node.dataset.form=form; node.style.width=width+'px'; node.style.height=height+'px';
    node.style.setProperty('--safe-top',topInset+'px');node.style.setProperty('--safe-bottom',bottomInset+'px');
    node.style.setProperty('--font-scale',fontScale);
    el('widthInput').value=width;el('heightInput').value=height;el('topInset').value=topInset;el('bottomInset').value=bottomInset;
    el('fontScale').value=fontScale;el('fontScaleValue').textContent=fontScale.toFixed(2)+'×';
    el('reviewStatus').textContent=statusName(state)+' · '+width+'×'+height+' logical px. Local demonstration only.';
  }
  document.addEventListener('click',function (event) {
    const row=event.target.closest('[data-row]');
    if (row) {
      selected=visibleRows[Number(row.dataset.row)];
      if (!selected) return;
      setState(selected.confidence==='weak'?'weak':'inspecting');return;
    }
    const archive=event.target.closest('[data-archive]');
    if (archive) {
      const data=archives.find(function (a) {return a.index===Number(archive.dataset.archive);});
      if (data) {selected={title:data.path};setState('inspecting');}return;
    }
    const button=event.target.closest('[data-action]');
    if (!button)return;
    switch(button.dataset.action){
      case 'close':setState('closed');break;
      case 'cancel-weak':setState('results');break;
      case 'confirm-weak':setState('inspecting');break;
      case 'back-results':setState('results');break;
      case 'manual-search':setState('results');el('queryInput').focus();break;
      case 'confirm-pack':setState('inspecting');break;
    }
  });
  document.addEventListener('keydown',function (event) {
    if ((event.key==='Enter'||event.key===' ') && event.target.matches('[data-row],[data-archive]')) {event.preventDefault();event.target.click();}
    else if (event.key==='Escape' && state==='weak') {event.preventDefault();setState('results');}
  });
  el('searchForm').addEventListener('submit',function (event) {
    event.preventDefault();queryValue=el('queryInput').value.trim();
    if (!queryValue)return;
    const q=queryValue.toLowerCase();
    visibleRows=rows.filter(function (r) {return r.title.toLowerCase().includes(q)||r.sourceName.toLowerCase().includes(q)||q.includes('9781401207526');});
    state=visibleRows.length?'results':'empty';
    renderState();
  });
  el('stateSelect').addEventListener('change',function (event) {setState(event.target.value);});
  el('preset').addEventListener('change',function (event) {
    const p=presets.find(function (x) {return x[0]===event.target.value;});if(!p)return;
    width=p[1];height=p[2];viewport();
  });
  ['widthInput','heightInput','topInset','bottomInset','fontScale'].forEach(function (id) {
    el(id).addEventListener('input',function () {
      width=Number(el('widthInput').value)||width;height=Number(el('heightInput').value)||height;
      topInset=Math.max(0,Number(el('topInset').value)||0);bottomInset=Math.max(0,Number(el('bottomInset').value)||0);
      fontScale=Number(el('fontScale').value)||1;el('preset').value='custom';viewport();
    });
  });
  viewport();
  renderState();
})();

