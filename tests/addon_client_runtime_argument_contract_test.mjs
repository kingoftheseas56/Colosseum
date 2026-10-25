import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const callers = [
    '../qml/Main.qml',
    '../qml/PlayerPage.qml',
    '../qml/SourcesSheet.qml',
    '../qml/TheatreWorld.qml',
    '../qml/player2host/ColosseumHostServices.qml'
];
const loadPattern = /AddonClient\.loadStreams\s*\(/g;
const runtimeArgumentPattern = /\(typeof PlatformRuntime !== "undefined" \? PlatformRuntime : null\)/g;

for (const relativePath of callers) {
    const source = fs.readFileSync(path.resolve(testDirectory, relativePath), 'utf8');
    const loadCalls = (source.match(loadPattern) || []).length;
    const forwardedRuntimeArguments = (source.match(runtimeArgumentPattern) || []).length;
    assert.ok(loadCalls > 0, `${relativePath} should contain its expected addon stream call`);
    assert.equal(forwardedRuntimeArguments, loadCalls,
        `${relativePath} must forward the QML PlatformRuntime to every AddonClient loader`);
}

console.log('ADDON_CLIENT_RUNTIME_ARGUMENT_CONTRACT PASS all QML loader call sites forward PlatformRuntime');
