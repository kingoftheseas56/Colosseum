// extension_panes_and_search_test.mjs — the pane rules after the Sources redesign.
//
// SUPERSEDED, deliberately. This file used to pin "Discover and Browse are Theatre-only"
// and "typing in Discover jumps to Browse". Both were answers to a page that no longer
// exists: Hemanth replaced Discover with a world-agnostic Sources pane (2026-07-26), so
// there is nothing left to hide per world and nothing left to jump away from — Sources
// answers a query in place. What survives is the rule underneath both: a pane is never
// shown a world it cannot honour, and the world tabs belong to Installed alone.
//
// The page's pane rules are plain state transitions, mirrored here and driven headlessly.

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
    pane: 'sources',
    query: '',
    get worldTabsApply() { return this.pane === 'installed'; },
    get paneModel() { return ['sources', 'browse', 'installed']; },
    // what that pane will actually draw — his real roster: 13 total, 4/6/4 per world
    get installedTabCount() {
      return this.worldTabsApply ? ({ theatre: 4, tankoban: 6, biblio: 4 })[this.world] : 13;
    },
    setWorld(w) { this.world = w; },
    setPane(p) { this.pane = p; },
    setQuery(q) { this.query = q; }
  };
}

console.log('every pane is available from every world — Sources is world-agnostic');
{
  const p = makePage();
  eq(p.paneModel, ['sources', 'browse', 'installed'], 'three panes, always');
  p.setWorld('tankoban');
  eq(p.paneModel, ['sources', 'browse', 'installed'], 'still three from Tankoban');
  p.setWorld('biblio');
  eq(p.paneModel, ['sources', 'browse', 'installed'], 'still three from Biblio');
}

console.log('');
console.log('world tabs belong to Installed alone');
{
  const p = makePage();
  eq(p.worldTabsApply, false, 'no world tabs on Sources');
  p.setPane('browse');
  eq(p.worldTabsApply, false, 'no world tabs on Browse');
  p.setPane('installed');
  eq(p.worldTabsApply, true, 'world tabs on Installed');
}

console.log('');
console.log('the Installed count matches what that pane will draw');
{
  const p = makePage();
  eq(p.installedTabCount, 13, 'from Sources, the whole roster');
  p.setPane('installed');
  eq(p.installedTabCount, 4, 'on Installed/Theatre, that world');
  p.setWorld('tankoban');
  eq(p.installedTabCount, 6, 'on Installed/Tankoban, that world');
}

console.log('');
console.log('a search filters in place — no pane moves under you');
{
  for (const start of ['sources', 'browse', 'installed']) {
    const p = makePage();
    p.setPane(start);
    p.setQuery('nyaa');
    eq(p.pane, start, `searching on ${start} stays put`);
    p.setQuery('');
    eq(p.pane, start, `and clearing leaves you on ${start}`);
  }
}

console.log('');
console.log(failed === 0 ? 'all green' : `${failed} FAILED`);
process.exit(failed === 0 ? 0 : 1);
