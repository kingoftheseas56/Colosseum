import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../qml/feria/FeriaSession.js', import.meta.url), 'utf8');
async function cleanup(serviceWorker) {
    const calls = [];
    const context = {
        window: {}, navigator: { serviceWorker },
        localStorage: { clear() { calls.push('local'); } },
        sessionStorage: { clear() { calls.push('session'); } },
        caches: { async keys() { return ['fixture']; }, async delete(key) { calls.push(key); return true; } },
        indexedDB: { async databases() { return []; } }
    };
    runInNewContext(source, context);
    runInNewContext(context.clearStorage, context);
    await new Promise(resolve => setImmediate(resolve));
    return { result: context.window.__feriaStorageCleared, calls };
}
const unavailable = await cleanup({ async getRegistrations() {
    const error = new Error('blank-origin enumeration unavailable'); error.name = 'InvalidStateError'; throw error;
} });
assert.equal(unavailable.result, 'partial', 'Unavailable service-worker cleanup must not report complete success');
assert.deepEqual(unavailable.calls, ['local', 'session', 'fixture']);
assert.equal((await cleanup({ async getRegistrations() { return [{ async unregister() { return false; } }]; } })).result,
    'partial', 'An unremoved registration must remain explicitly incomplete');
assert.equal((await cleanup({ async getRegistrations() { return [{ async unregister() { return true; } }]; } })).result, 'done');
assert.equal((await cleanup({ async getRegistrations() { throw new Error('storage failure'); } })).result, 'failed');
console.log('PASS Feria scoped cleanup distinguishes complete, partial and failed outcomes');
