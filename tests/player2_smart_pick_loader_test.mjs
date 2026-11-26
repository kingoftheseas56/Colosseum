import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

// Player2Page needs native types and cannot be instantiated headless, so this pins the source
// contract Main.qml relies on (same names as qml/PlayerPage.qml). Android runs Player 2.
const p2 = readFileSync(new URL('../qml/player2host/Player2Page.qml', import.meta.url), 'utf8');

function body(name) {
  const start = p2.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `Missing ${name}`);
  const brace = p2.indexOf('{', start);
  let depth = 0;
  for (let i = brace; i < p2.length; i++) {
    if (p2[i] === '{') depth++;
    if (p2[i] === '}' && --depth === 0) return p2.slice(start, i + 1);
  }
  throw new Error(`Unclosed ${name}`);
}

test('Player 2 exposes the Smart Pick seam Main.qml drives', () => {
  assert.match(p2, /signal manualSourceRequested\(\)/);
  assert.match(p2, /property bool smartPickLoading: false/);
  assert.match(body('beginSmartPick'), /smartPickLoading = true/);
});

test('Player 2 loading screen shows the Smart Pick wait and the Choose source action', () => {
  assert.match(p2, /_starting: !page\.errored\s*&& \(page\.smartPickLoading/);
  assert.match(body('_statusText'), /smartPickLoading\)\s*return qsTr\("Finding the best stream"\)/);
  assert.match(p2, /allowManual: page\.smartPickLoading/);
  assert.match(p2, /onManualRequested: if \(page\.smartPickLoading\) page\.manualSourceRequested\(\)/);
});

test('Back during the Smart Pick wait goes back, and any real playback clears the wait', () => {
  assert.match(body('requestEscape'), /smartPickLoading\) \{ page\.backRequested\(\); return \}/);
  assert.match(body('_reset'), /smartPickLoading = false/);
  assert.match(body('playTorrent'), /_reset\(\)/);
});
