const { DEFAULT_MANIFEST, SERVICES, loadManifest, loadProviderCatalog } = window.StreamingCatalog;
const previewData = window.StreamingPreviewData;

const $ = id => document.getElementById(id);
const preview = new URLSearchParams(location.search).has('preview');
const state = {
  mode: preview ? 'preview' : 'live', providerId: 'nfx',
  providers: [...SERVICES], resumes: preview ? previewData.resumes : [],
  catalogs: preview ? { ...previewData.catalogs } : {}, source: null,
  catalogRequests: new Map(), selected: null, searchOpen: false, accountOpen: false,
  accountSelected: 'nfx', connections: {}, hostHandler: null
};

const ARTS = {
  ink: 'radial-gradient(ellipse at 82% 27%,rgba(220,231,235,.62),transparent 18%),linear-gradient(126deg,#111b29 8%,#436275 55%,#101722)',
  ember: 'radial-gradient(circle at 80% 30%,rgba(242,178,98,.7),transparent 18%),linear-gradient(122deg,#1b1418,#813e32 63%,#181a26)',
  corridor: 'linear-gradient(96deg,transparent 19%,rgba(231,244,236,.46) 20%,transparent 22%,transparent 67%,rgba(231,244,236,.36) 68%,transparent 70%),linear-gradient(135deg,#1a292c,#59756e,#13242c)',
  violet: 'radial-gradient(ellipse at 75% 38%,rgba(232,172,227,.55),transparent 27%),linear-gradient(142deg,#172041,#644569,#151527)',
  marble: 'radial-gradient(ellipse at 75% 13%,rgba(249,245,230,.49),transparent 31%),linear-gradient(130deg,#222a32,#72706a,#1a222a)',
  ocean: 'radial-gradient(ellipse at 69% 5%,rgba(153,201,205,.45),transparent 35%),linear-gradient(145deg,#0c1b2f,#315a68,#0d172a)',
  forest: 'radial-gradient(ellipse at 70% 37%,rgba(190,206,155,.31),transparent 25%),linear-gradient(126deg,#15221d,#506052,#131b1e)',
  citrus: 'radial-gradient(ellipse at 71% 39%,rgba(255,221,129,.6),transparent 23%),linear-gradient(135deg,#373d32,#8a7652,#2a3031)',
  shadow: 'radial-gradient(ellipse at 78% 25%,rgba(191,208,230,.36),transparent 21%),linear-gradient(124deg,#10131e,#354255,#0b111a)',
  golden: 'radial-gradient(ellipse at 72% 22%,rgba(247,219,137,.65),transparent 22%),linear-gradient(135deg,#2c231d,#81664b,#17212a)',
  smoke: 'radial-gradient(ellipse at 67% 21%,rgba(224,223,209,.47),transparent 28%),linear-gradient(125deg,#151a21,#525b5a,#131821)'
};

function element(tag, className = '', text = '') {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text) node.textContent = text;
  return node;
}

function safeImage(value) {
  try { const url = new URL(value); return url.protocol === 'https:' ? url.href : ''; }
  catch { return ''; }
}

function setArt(node, item) {
  const image = safeImage(item.image);
  const art = image ? `url(${JSON.stringify(image)})` : ARTS[item.art] || ARTS.ink;
  node.style.setProperty('--art', art);
}

function service(id) { return state.providers.find(item => item.id === id) || SERVICES.find(item => item.id === id) || { id, name: id, mark: id.slice(0, 1).toUpperCase() }; }
function titleKey(item) {
  const title = String(item.title || '').normalize('NFKC').toLocaleLowerCase().replace(/[^\p{L}\p{N}]+/gu, '');
  return `${item.type}:${title || String(item.id || '')}`;
}
function releaseYear(item) { return String(item.year || '').match(/\b(?:18|19|20|21)\d{2}\b/)?.[0] || ''; }

let toastTimer;
function toast(message) {
  const node = $('toast'); node.textContent = message; node.hidden = false;
  clearTimeout(toastTimer); toastTimer = setTimeout(() => { node.hidden = true; }, 4200);
}

function sendAction(type, item = {}) {
  if (state.mode === 'preview') { toast('Design preview — this action connects when Streaming is hosted in Colosseum.'); return; }
  const detail = { type, providerId: item.providerId || state.providerId, titleId: item.id || null, episodeId: item.episodeId || null, title: item.title || null, mediaType: item.type || null };
  if (item.profileId) detail.profileId = item.profileId;
  if (item.conflictId) detail.conflictId = item.conflictId;
  if (Array.isArray(item.providerOrder)) detail.providerOrder = item.providerOrder;
  window.dispatchEvent(new CustomEvent('colosseum:streaming-action', { detail }));
  if (typeof state.hostHandler === 'function') state.hostHandler(detail);
  else if (window.chrome?.webview?.postMessage) window.chrome.webview.postMessage(detail);
  else toast('This action becomes available inside Colosseum.');
}

function renderResumes() {
  const list = $('resume-list'); list.replaceChildren();
  $('resume-aside').textContent = state.mode === 'preview' ? 'Illustrative progress · design preview' : 'Across your connected services';
  if (!state.resumes.length) {
    const box = element('div', 'empty-resume');
    const copy = element('div');
    copy.append(element('p', 'eyebrow', 'Your story continues here'), element('h3', '', 'Nothing to resume yet'), element('p', '', 'Once a service is connected and playback syncs, your next episode and unfinished films will gather here.'));
    const button = element('button', 'outline-action', 'Manage services  ↗'); button.type = 'button'; button.addEventListener('click', openAccount);
    box.append(copy, button); list.append(box); return;
  }
  for (const item of state.resumes.slice(0, 8)) {
    const card = element('article', 'resume-card'); setArt(card, item);
    const copy = element('div', 'resume-copy');
    const label = element('span', 'service-label', service(item.providerId).name);
    const name = element('h3', '', item.title);
    const episode = element('div', 'resume-episode', item.episode || (item.type === 'movie' ? 'Film' : 'Series'));
    const bottom = element('div', 'resume-bottom');
    const progress = Math.max(0, Math.min(100, Math.round(Number(item.progress || 0) * 100)));
    const caption = element('div', 'progress-caption'); caption.append(element('span', '', `${progress}% watched`), element('span', '', item.remaining || ''));
    const track = element('div', 'progress-track'); const fill = element('span'); fill.style.width = `${progress}%`; track.append(fill);
    const actions = element('div', 'resume-actions');
    const resume = element('button', 'primary-action', '▶  Resume'); resume.type = 'button'; resume.addEventListener('click', () => sendAction('resume', item));
    const details = element('button', 'quiet-action', 'Details'); details.type = 'button'; details.addEventListener('click', () => openDetails(item));
    actions.append(resume);
    if (item.type === 'series') { const episodes = element('button', 'quiet-action', 'Episodes'); episodes.type = 'button'; episodes.addEventListener('click', () => openDetails(item)); actions.append(episodes); }
    actions.append(details); bottom.append(caption, track, actions); copy.append(label, name, episode, bottom); card.append(copy); list.append(card);
  }
}

function renderRail() {
  const rail = $('provider-rail'); rail.replaceChildren(element('span', 'rail-label', 'Services'));
  for (const provider of state.providers) {
    const button = element('button', 'provider-button'); button.type = 'button'; button.title = provider.name; button.setAttribute('aria-label', provider.name);
    button.setAttribute('aria-current', String(provider.id === state.providerId));
    button.append(element('small', '', provider.mark));
    button.addEventListener('click', () => selectProvider(provider.id)); rail.append(button);
  }
  const bottom = element('button', 'provider-button rail-bottom', '+'); bottom.type = 'button'; bottom.title = 'Manage services'; bottom.setAttribute('aria-label', 'Manage services'); bottom.addEventListener('click', openAccount); rail.append(bottom);
}

function renderSource() {
  $('catalog-source').textContent = state.mode === 'preview' ? 'Design preview · sample titles' : state.mode === 'host' ? 'Connected catalogue' : 'Live discovery catalogue';
  $('search-scope').textContent = state.mode === 'host' ? 'Search across your services' : state.mode === 'preview' ? 'Search the design preview' : 'Search discovery catalogues';
}

function renderCatalogStatus(message, error = false) {
  const node = $('catalog-status'); node.textContent = message; node.classList.toggle('is-error', error);
}

function makeTitleCard(item) {
  const button = element('button', 'title-card'); button.type = 'button'; button.setAttribute('aria-label', `Details for ${item.title}`); setArt(button, item);
  const corner = element('span', 'type-corner', item.type === 'movie' ? 'Film' : 'Series');
  const body = element('span', 'title-card-body'); body.append(element('strong', '', item.title), element('span', '', [item.year, item.genres?.[0]].filter(Boolean).join(' · ') || service(item.providerId).name));
  button.append(corner, body); button.addEventListener('click', () => openDetails(item)); return button;
}

function renderShelves() {
  const provider = service(state.providerId); $('provider-name').textContent = provider.name; $('provider-kicker').textContent = 'In focus · one service at a time';
  const root = $('shelves'); root.replaceChildren();
  const shelves = state.catalogs[state.providerId] || [];
  if (!shelves.length) return;
  for (const shelf of shelves) {
    if (!shelf.titles?.length) continue;
    const section = element('section', 'shelf');
    const heading = element('div', 'shelf-heading'); heading.append(element('h4', '', shelf.name), element('p', '', `${shelf.titles.length} titles in this catalogue`));
    const strip = element('div', 'title-strip'); for (const item of shelf.titles) strip.append(makeTitleCard(item));
    section.append(heading, strip); root.append(section);
  }
  const note = element('p', 'source-caption', state.mode === 'preview' ? 'Sample titles illustrate the design. Availability and progress are not live.' : 'Catalogues help you discover titles. Availability, subscription access and playback are confirmed by the provider.');
  root.append(note);
}

async function ensureCatalog(providerId, force = false) {
  if (state.mode !== 'live' || !state.source) return;
  if (!force && state.catalogs[providerId]) return;
  if (state.catalogRequests.has(providerId)) return state.catalogRequests.get(providerId);
  const promise = loadProviderCatalog(state.source, providerId).then(shelves => {
    if (state.mode !== 'live') return;
    state.catalogs[providerId] = shelves;
    if (providerId === state.providerId) { renderShelves(); renderCatalogStatus('Discovery titles loaded. Playback opens on the service.'); }
    if (state.searchOpen) renderSearch();
  }).catch(error => {
    if (state.mode !== 'live') return;
    if (providerId === state.providerId) renderCatalogStatus(`${error.message} Try Refresh catalog.`, true);
  }).finally(() => state.catalogRequests.delete(providerId));
  state.catalogRequests.set(providerId, promise); return promise;
}

function selectProvider(id) {
  state.providerId = id; renderRail(); renderShelves();
  if (state.mode === 'live') {
    renderCatalogStatus(state.catalogs[id] ? 'Discovery titles loaded. Playback opens on the service.' : 'Loading discovery titles…');
    ensureCatalog(id);
  } else renderCatalogStatus(state.mode === 'preview' ? 'Illustrative catalog · no provider account is connected.' : 'Catalogue supplied by Colosseum.');
}

function detailLine(label, value) {
  const line = element('div', 'detail-meta'); line.append(element('dt', '', label), element('dd', '', value)); return line;
}

let returnFocus = null;
function openDetails(item) {
  returnFocus = document.activeElement; state.selected = item;
  const drawer = $('detail-drawer'); setArt($('drawer-art'), item);
  $('detail-provider').textContent = service(item.providerId).name + (state.mode === 'preview' ? ' · design preview' : '');
  $('detail-title').textContent = item.title;
  $('detail-subtitle').textContent = [item.year, item.type === 'movie' ? 'Film' : 'Series', item.runtime].filter(Boolean).join('  ·  ');
  $('detail-description').textContent = item.description || 'Discover this title in the provider catalogue.';
  $('detail-primary').textContent = state.resumes.some(resume => resume.id === item.id && resume.providerId === item.providerId) ? 'Resume on provider  ↗' : 'Open on provider  ↗';
  $('detail-watchlist').hidden = !item.watchlistAction;
  const more = $('detail-more'); more.replaceChildren();
  if (item.genres?.length) more.append(detailLine('Genres', item.genres.join(' · ')));
  if (item.episode) more.append(detailLine('Up next', item.episode));
  if (item.remaining) more.append(detailLine('Remaining', item.remaining));
  if (Array.isArray(item.episodes) && item.episodes.length) {
    const heading = element('h3', 'episode-heading', 'Episodes'); more.append(heading);
    const seasonNumbers = [...new Set(item.episodes.map(episode => episode.season).filter(Boolean))].sort((a, b) => a - b);
    const chips = element('div', 'season-chips'); const list = element('div', 'episode-list');
    const drawSeason = season => {
      list.replaceChildren();
      for (const chip of chips.children) chip.setAttribute('aria-pressed', String(Number(chip.dataset.season) === season));
      for (const episode of item.episodes.filter(value => !season || value.season === season)) {
        const row = element('button', 'episode-row'); row.type = 'button';
        row.append(element('span', '', episode.label || episode.title || 'Episode'), element('span', '', episode.progress ? `${Math.round(episode.progress * 100)}% watched` : 'View episode'));
        row.addEventListener('click', () => sendAction('episode', { ...item, episodeId: episode.id })); list.append(row);
      }
    };
    for (const season of seasonNumbers) { const chip = element('button', 'season-chip', `Season ${season}`); chip.type = 'button'; chip.dataset.season = String(season); chip.addEventListener('click', () => drawSeason(season)); chips.append(chip); }
    if (chips.childElementCount) more.append(chips);
    more.append(list); drawSeason(seasonNumbers[0]);
  }
  const related = (state.catalogs[item.providerId] || []).flatMap(shelf => shelf.titles || [])
    .filter(other => other.id !== item.id && titleKey(other) !== titleKey(item))
    .sort((a, b) => Number(Boolean(b.genres?.some(genre => item.genres?.includes(genre)))) - Number(Boolean(a.genres?.some(genre => item.genres?.includes(genre)))))
    .slice(0, 4);
  if (related.length) {
    more.append(element('h3', 'episode-heading', 'Related titles'));
    const list = element('div', 'related-titles');
    for (const other of related) list.append(makeTitleCard(other));
    more.append(list);
  }
  more.append(element('p', 'drawer-notice', state.mode === 'preview' ? 'This is a design preview. Your actual account and playback state will come from connected services.' : 'Playback and account access stay on the provider website.'));
  $('screen-shade').hidden = false; drawer.hidden = false; document.body.style.overflow = 'hidden'; $('drawer-close').focus();
}

function closeDetails() {
  $('detail-drawer').hidden = true; $('screen-shade').hidden = true; document.body.style.overflow = ''; state.selected = null; returnFocus?.focus();
}

function canonicalResults(query) {
  const all = Object.values(state.catalogs).flatMap(shelves => shelves.flatMap(shelf => shelf.titles || []));
  const matching = all.filter(item => item.title.toLocaleLowerCase().includes(query.toLocaleLowerCase()));
  const grouped = [];
  for (const item of matching) {
    const key = titleKey(item);
    const year = releaseYear(item);
    const group = grouped.find(group =>
      (item.canonicalId && group.items.some(other => other.canonicalId === item.canonicalId)) ||
      (group.key === key && (!year || !group.year || group.year === year)));
    if (group) { group.items.push(item); if (!group.year) group.year = year; }
    else grouped.push({ key, year, items: [item] });
  }
  return grouped.map(group => {
    const items = group.items;
    const active = items.find(item => state.resumes.some(resume => resume.id === item.id && resume.providerId === item.providerId));
    const primary = active || items.sort((a, b) => state.providers.findIndex(provider => provider.id === a.providerId) - state.providers.findIndex(provider => provider.id === b.providerId))[0];
    return { primary, services: [...new Set(items.map(item => service(item.providerId).name))] };
  }).slice(0, 30);
}

function renderSearch() {
  const query = $('search-input').value.trim(); const root = $('search-results'); root.replaceChildren();
  if (!query) { root.append(element('p', 'search-empty', 'What are you in the mood for?')); return; }
  const results = canonicalResults(query);
  if (!results.length) { root.append(element('p', 'search-empty', 'No matches in loaded catalogues.')); return; }
  for (const result of results) {
    const button = element('button', 'search-result'); button.type = 'button';
    const art = element('span', 'search-result-art'); setArt(art, result.primary);
    const copy = element('span'); copy.append(element('strong', '', result.primary.title), element('small', '', result.services.join(' · ')));
    button.append(art, copy, element('span', 'search-result-arrow', '↗'));
    button.addEventListener('click', () => { closeSearch(); openDetails(result.primary); }); root.append(button);
  }
}

function openSearch() {
  state.searchOpen = true; $('search-layer').hidden = false; document.body.style.overflow = 'hidden'; $('search-input').focus(); renderSearch();
  if (state.mode === 'live') for (const provider of state.providers) ensureCatalog(provider.id);
}
function closeSearch() { state.searchOpen = false; $('search-layer').hidden = true; document.body.style.overflow = ''; $('search-button').focus(); }

function renderAccount() {
  const roster = $('account-roster'); roster.replaceChildren(element('p', 'roster-caption', 'Service order'));
  for (const provider of state.providers) {
    const connection = state.connections[provider.id] || {};
    const row = element('button', 'roster-button'); row.type = 'button'; row.setAttribute('aria-current', String(provider.id === state.accountSelected));
    const label = element('span', 'roster-text'); label.append(element('strong', '', provider.name), element('small', '', connection.status === 'connected' ? connection.profile || 'Connected' : connection.status === 'attention' ? 'Needs attention' : 'Not connected'));
    row.append(element('span', 'roster-mark', provider.mark), label);
    row.addEventListener('click', () => { state.accountSelected = provider.id; renderAccount(); }); roster.append(row);
  }
  const provider = service(state.accountSelected), connection = state.connections[provider.id] || {};
  const detail = $('account-detail'); detail.replaceChildren();
  detail.append(element('p', 'eyebrow', 'Service details'), element('h3', '', provider.name));
  const connected = connection.status === 'connected' || connection.status === 'attention';
  const statusLabel = connection.status === 'attention' ? 'Needs attention' : connected ? 'Connected' : 'Not connected';
  const status = element('span', `connection-state${connected ? ' connected' : ''}`, statusLabel); detail.append(status);
  detail.append(element('p', '', connected ? 'Your provider account is connected. Playback and account access stay on the provider website.' : 'Connect through the provider’s own sign-in page. Colosseum never asks for your provider password here.'));
  const fields = element('div', 'account-fields');
  fields.append(detailLine('Profile', connection.profile || 'None selected'), detailLine('Progress', connection.sync || 'Not synced'), detailLine('Catalogue', state.catalogs[provider.id]?.length ? 'Discovery titles available' : 'Not loaded'));
  detail.append(fields);
  if (Array.isArray(connection.profiles) && connection.profiles.length > 1) {
    detail.append(element('h4', 'account-subhead', 'Provider profile'));
    const profiles = element('div', 'account-profiles');
    for (const profile of connection.profiles) {
      const name = typeof profile === 'string' ? profile : profile.name;
      const id = typeof profile === 'string' ? profile : profile.id;
      const button = element('button', 'outline-action', name); button.type = 'button';
      button.setAttribute('aria-pressed', String(connection.profile === name));
      button.addEventListener('click', () => sendAction('select-profile', { providerId: provider.id, profileId: id })); profiles.append(button);
    }
    detail.append(profiles);
  }
  if (Array.isArray(connection.conflicts) && connection.conflicts.length) {
    detail.append(element('h4', 'account-subhead', 'Needs review'));
    const conflicts = element('div', 'account-conflicts');
    for (const conflict of connection.conflicts) {
      const row = element('button', 'episode-row', conflict.label || 'Review progress conflict'); row.type = 'button';
      row.addEventListener('click', () => sendAction('review-conflict', { providerId: provider.id, conflictId: conflict.id })); conflicts.append(row);
    }
    detail.append(conflicts);
  }
  const actions = element('div', 'account-actions');
  const connect = element('button', 'primary-action', connected ? 'Manage connection  ↗' : 'Connect service  ↗'); connect.type = 'button'; connect.addEventListener('click', () => sendAction(connected ? 'manage-service' : 'connect-service', { providerId: provider.id }));
  const open = element('button', 'outline-action', 'Open provider  ↗'); open.type = 'button'; open.addEventListener('click', () => sendAction('open-provider', { providerId: provider.id }));
  actions.append(connect, open); detail.append(actions);
  const order = element('div', 'account-order');
  const up = element('button', 'text-action', '↑ Move up'); up.type = 'button'; up.disabled = state.providers[0]?.id === provider.id;
  const down = element('button', 'text-action', '↓ Move down'); down.type = 'button'; down.disabled = state.providers.at(-1)?.id === provider.id;
  const move = delta => { const from = state.providers.findIndex(item => item.id === provider.id), to = from + delta; [state.providers[from], state.providers[to]] = [state.providers[to], state.providers[from]]; renderAccount(); renderRail(); sendAction('reorder-services', { providerId: provider.id, providerOrder: state.providers.map(item => item.id) }); };
  up.addEventListener('click', () => move(-1)); down.addEventListener('click', () => move(1)); order.append(up, down); detail.append(order);
  detail.append(element('p', 'account-helper', state.mode === 'preview' ? 'Design preview · connection states and profiles are illustrative.' : 'A catalogue listing does not confirm that a title is playable on your account.'));
}

function openAccount() { state.accountOpen = true; $('account-layer').hidden = false; document.body.style.overflow = 'hidden'; renderAccount(); $('account-close').focus(); }
function closeAccount() { state.accountOpen = false; $('account-layer').hidden = true; document.body.style.overflow = ''; $('account-button').focus(); }

function trapFocus(event, root) {
  const controls = [...root.querySelectorAll('button:not([disabled]), input, a[href]')].filter(node => !node.hidden);
  if (!controls.length) return;
  const first = controls[0], last = controls.at(-1);
  if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
}

function mount(data) {
  if (!data || typeof data !== 'object') throw new TypeError('Streaming data must be an object.');
  state.mode = 'host'; state.source = null; state.catalogRequests.clear();
  state.providers = Array.isArray(data.providers) && data.providers.length ? data.providers : [...SERVICES];
  state.providerId = state.providers.some(provider => provider.id === state.providerId) ? state.providerId : state.providers[0].id;
  state.resumes = Array.isArray(data.resumes) ? data.resumes : [];
  state.catalogs = data.catalogs && typeof data.catalogs === 'object' ? data.catalogs : {};
  state.connections = data.connections && typeof data.connections === 'object' ? data.connections : {};
  if (typeof data.accountInitial === 'string') $('account-button').firstElementChild.textContent = data.accountInitial.slice(0, 1).toUpperCase();
  renderAll();
  if (state.searchOpen) renderSearch();
  if (state.accountOpen) renderAccount();
}

function renderAll() { renderResumes(); renderRail(); renderSource(); renderShelves(); renderCatalogStatus(state.mode === 'preview' ? 'Illustrative catalog · no provider account is connected.' : state.mode === 'host' ? 'Catalogue supplied by Colosseum.' : 'Loading discovery titles…'); }

function bind() {
  $('search-button').addEventListener('click', openSearch); $('search-close').addEventListener('click', closeSearch); $('search-input').addEventListener('input', renderSearch);
  $('account-button').addEventListener('click', openAccount); $('account-close').addEventListener('click', closeAccount);
  $('refresh-button').addEventListener('click', async () => {
    if (state.mode !== 'live') { toast(state.mode === 'preview' ? 'Open the live Streaming page to refresh catalogues.' : 'Colosseum updates connected catalogues.'); return; }
    if (!state.source) { await connectLive(); return; }
    delete state.catalogs[state.providerId]; renderShelves(); renderCatalogStatus('Refreshing discovery titles…'); await ensureCatalog(state.providerId, true);
  });
  $('open-provider-button').addEventListener('click', () => sendAction('open-provider', { providerId: state.providerId }));
  $('drawer-close').addEventListener('click', closeDetails); $('screen-shade').addEventListener('click', closeDetails);
  $('detail-primary').addEventListener('click', () => { if (state.selected) sendAction(state.resumes.some(resume => resume.id === state.selected.id && resume.providerId === state.selected.providerId) ? 'resume' : 'open-title', state.selected); });
  $('detail-watchlist').addEventListener('click', () => { if (state.selected) sendAction('watchlist', state.selected); });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') { if (!($('detail-drawer').hidden)) closeDetails(); else if (state.accountOpen) closeAccount(); else if (state.searchOpen) closeSearch(); }
    if (event.key === 'Tab') { const root = !$('detail-drawer').hidden ? $('detail-drawer') : state.accountOpen ? $('account-layer') : state.searchOpen ? $('search-layer') : null; if (root) trapFocus(event, root); }
    if (event.key === '/' && !state.searchOpen && !state.accountOpen && $('detail-drawer').hidden && event.target?.tagName !== 'INPUT') { event.preventDefault(); openSearch(); }
  });
}

async function connectLive() {
  renderCatalogStatus('Connecting to discovery catalogue…');
  try {
    const source = await loadManifest(DEFAULT_MANIFEST);
    if (state.mode !== 'live') return;
    const providers = SERVICES.filter(provider => source.manifest.catalogs.some(catalog => catalog.id === provider.id));
    if (!providers.length) throw new Error('No supported service catalogs were returned.');
    state.source = source; state.providers = providers;
    selectProvider(state.providers.some(provider => provider.id === state.providerId) ? state.providerId : state.providers[0].id);
  } catch (error) { if (state.mode === 'live') renderCatalogStatus(`${error.message} Use Refresh catalog to retry.`, true); }
}

window.ColosseumStreaming = Object.freeze({
  mount,
  onAction(handler) { if (typeof handler !== 'function') throw new TypeError('Action handler must be a function.'); state.hostHandler = handler; },
  getState() { return { mode: state.mode, providerId: state.providerId, loadedProviders: Object.keys(state.catalogs) }; }
});

bind(); renderAll();
if (state.mode === 'live') connectLive();
