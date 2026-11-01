import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
function shippedFunction(name) {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `Missing ${name}`);
  const brace = source.indexOf('{', start);
  let depth = 0;
  for (let i = brace; i < source.length; i++) {
    if (source[i] === '{') depth++;
    if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`Unclosed ${name}`);
}
function shell() {
  const later = [];
  const pending = { kind: 'session', sessionId: 'episode:1:1' };
  const scope = {
    playerOpen: true, pendingPlayerOpen: pending, playerLayerRetryPending: false,
    playerLayer: { active: true, status: 3, source: 'PlayerPage.qml' },
    Loader: { Error: 3 }, Qt: { callLater: fn => later.push(fn) },
  };
  scope.win = scope;
  vm.createContext(scope);
  vm.runInContext(shippedFunction('ensurePlayerLayerLoading')
                  + '\n' + shippedFunction('retryPlayerLayerLoading'), scope);
  return { scope, later, pending };
}

test('a new video request can retry a failed player instead of reusing the failed Loader', () => {
  const { scope, later, pending } = shell();
  scope.ensurePlayerLayerLoading();
  assert.equal(scope.playerLayer.active, false);
  assert.equal(scope.playerLayer.source, '');
  assert.equal(scope.pendingPlayerOpen, pending);
  assert.equal(later.length, 1);
  later.shift()();
  assert.equal(scope.playerLayer.active, true);
  assert.equal(scope.playerLayer.source, 'PlayerPage.qml');
  assert.equal(scope.pendingPlayerOpen, pending);
});

test('repeated clicks coalesce into one retry', () => {
  const { scope, later } = shell();
  scope.retryPlayerLayerLoading();
  scope.retryPlayerLayerLoading();
  scope.ensurePlayerLayerLoading();
  assert.equal(later.length, 1);
});

test('a cancelled open stays closed when the deferred retry runs', () => {
  const { scope, later } = shell();
  scope.retryPlayerLayerLoading();
  scope.playerOpen = false;
  scope.pendingPlayerOpen = null;
  later.shift()();
  assert.equal(scope.playerLayer.active, false);
  assert.equal(scope.playerLayer.source, 'PlayerPage.qml');
  assert.equal(scope.playerLayerRetryPending, false);
});

test('a newer request wins while the old component is being released', () => {
  const { scope, later } = shell();
  scope.retryPlayerLayerLoading();
  const replacement = { kind: 'direct', infoHash: 'new-video' };
  scope.pendingPlayerOpen = replacement;
  scope.ensurePlayerLayerLoading();
  later.shift()();
  assert.equal(scope.playerLayer.active, true);
  assert.equal(scope.pendingPlayerOpen, replacement);
});

test('retry keeps the selected backend and does not switch players', () => {
  const { scope, later } = shell();
  scope.playerLayer.source = 'player2host/Player2Page.qml';
  scope.retryPlayerLayerLoading();
  later.shift()();
  assert.equal(scope.playerLayer.source, 'player2host/Player2Page.qml');
});

test('a closed player or missing request cannot start a retry loop', () => {
  const { scope, later } = shell();
  scope.pendingPlayerOpen = null;
  scope.retryPlayerLayerLoading();
  assert.equal(later.length, 0);
  scope.playerOpen = false;
  scope.pendingPlayerOpen = {};
  scope.retryPlayerLayerLoading();
  assert.equal(later.length, 0);
});
