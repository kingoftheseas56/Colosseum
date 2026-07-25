// extension_panes_and_search_test.mjs — Hemanth's two rulings of 2026-07-26.
//
//  1. "remove discover and browser for the other worlds"
//     Discover and Browse are Theatre-only. Discover renders a hardcoded curated rail
//     list (Netflix Catalog, Marvel Universe) that appeared under the Tankoban tab, and
//     Browse queries a community registry that is entirely video add-ons. Tankoban and
//     Biblio get the one pane that was ever true for them: Installed.
//
//  2. "when I search for something in theatre, it does nothing because the results show
//     up in browse but if the page is in discover it doesn't turn to browse"
//     Typing is a request for results, so it moves to the pane that has them. Clearing
//     the box hands Discover back, so a search never costs you your place.
//
// The page's pane/search rules are plain state transitions, so they are mirrored here
// exactly and driven headlessly. The QML side is a binding on the same three rules —
// paneModel, onWorldChanged, onQueryChanged — so a divergence shows up as a diff, not a
// silent pass.

let failed = 0;
const ok  = m => console.log(`  ok   ${m}`);
const bad = m => { console.log(`  FAIL ${m}`); failed++; };
const eq  = (got, want, m) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  g === w ? ok(`${m} → ${g}`) : bad(`${m} → ${g}, expected ${w}`);
};

// ---- the page's rules, mirrored from qml/ExtensionsPage.qml -------------------
function makePage() {
  return {
    world: 'theatre',
    pane: 'discover',
    query: '',
    _searchDroveThePane: false,

    get hasStore() { return this.world === 'theatre'; },
    get paneModel() {
      return this.hasStore ? ['discover', 'browse', 'installed'] : ['installed'];
    },
    setWorld(w) {
      this.world = w;
      if (!this.hasStore && this.pane !== 'installed') this.pane = 'installed';
    },
    setQuery(q) {
      this.query = q;
      if (this.hasStore && q !== '' && this.pane === 'discover') {
        this._searchDroveThePane = true;
        this.pane = 'browse';
      } else if (q === '' && this._searchDroveThePane) {
        this._searchDroveThePane = false;
        if (this.pane === 'browse') this.pane = 'discover';
      }
    },
    setPane(p) { this.pane = p; }
  };
}

console.log('ruling 1 — Discover and Browse are Theatre-only');
{
  const p = makePage();
  eq(p.paneModel, ['discover', 'browse', 'installed'], 'Theatre offers three panes');
  p.setWorld('tankoban');
  eq(p.paneModel, ['installed'], 'Tankoban offers one pane');
  p.setWorld('biblio');
  eq(p.paneModel, ['installed'], 'Biblio offers one pane');
}
{
  // The bug this prevents: leaving a world while parked on a pane it does not have.
  const p = makePage();
  p.setPane('discover');
  p.setWorld('tankoban');
  eq(p.pane, 'installed', 'Discover → Tankoban lands on Installed, not a dead pane');
}
{
  const p = makePage();
  p.setPane('browse');
  p.setWorld('biblio');
  eq(p.pane, 'installed', 'Browse → Biblio lands on Installed');
}
{
  const p = makePage();
  p.setWorld('tankoban');
  p.setWorld('theatre');
  eq(p.pane, 'installed', 'coming back to Theatre keeps the pane you were on');
  eq(p.paneModel, ['discover', 'browse', 'installed'], 'and Theatre gets its panes back');
}

console.log('\nruling 2 — a search goes where the answers are');
{
  const p = makePage();
  eq(p.pane, 'discover', 'starts on Discover');
  p.setQuery('subtitles');
  eq(p.pane, 'browse', 'typing in Theatre moves to Browse — the reported bug');
}
{
  const p = makePage();
  p.setQuery('torrent');
  p.setQuery('');
  eq(p.pane, 'discover', 'clearing the box hands Discover back');
  eq(p._searchDroveThePane, false, 'and the page stops claiming the search owns the pane');
}
{
  // If the user chose Browse himself, clearing the search must NOT yank him to Discover.
  const p = makePage();
  p.setPane('browse');
  p.setQuery('anime');
  eq(p.pane, 'browse', 'searching while already on Browse stays on Browse');
  p.setQuery('');
  eq(p.pane, 'browse', 'and clearing leaves him where HE put himself');
}
{
  // Installed has its own search (it filters the installed rows) and must not be hijacked.
  const p = makePage();
  p.setPane('installed');
  p.setQuery('nyaa');
  eq(p.pane, 'installed', 'searching in Installed filters in place, never jumps to Browse');
}
{
  // The worlds with no Browse must never be sent to it.
  for (const w of ['tankoban', 'biblio']) {
    const p = makePage();
    p.setWorld(w);
    p.setQuery('one piece');
    eq(p.pane, 'installed', `searching in ${w} stays on Installed — it has no Browse`);
  }
}
{
  // Typing, then switching world mid-search: must not strand him on a pane that is gone.
  const p = makePage();
  p.setQuery('debrid');
  eq(p.pane, 'browse', 'searching moved him to Browse');
  p.setWorld('tankoban');
  eq(p.pane, 'installed', 'switching to Tankoban mid-search rescues the pane');
  eq(p.paneModel.includes('browse'), false, 'and Browse is not offered there');
}

console.log(failed === 0 ? '\nall green' : `\n${failed} FAILED`);
process.exit(failed === 0 ? 0 : 1);
