import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash, createPublicKey, generateKeyPairSync, sign, verify } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createWorker } from '../src/worker.mjs';
import productionWorker from '../src/index.js';

const SIGNING_PREFIX = Buffer.from('Colosseum Tankoyomi addon v1\n', 'utf8');
const REPOSITORY_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SIGNER_PATH = join(REPOSITORY_ROOT, 'scripts', 'tankoyomi-sign-addons.mjs');
const PROVIDERS = Object.freeze({
  mangasin: Object.freeze({
    allowedHosts: ['m440.in'],
    source: 'extensions/tankoyomi/languages/es/mangasin.js'
  }),
  lectorxd: Object.freeze({
    allowedHosts: ['lectorxd.com'],
    source: 'extensions/tankoyomi/languages/es/lectorxd.js'
  })
});

function signedEnvelope(providerId, privateKey, sourceBytes, revision = 1) {
  const provider = PROVIDERS[providerId];
  const payload = {
    contractVersion: 1,
    addonId: 'colosseum.well.tankoyomi',
    language: 'es',
    providerId,
    revision,
    allowedHosts: [...provider.allowedHosts],
    scriptSha256: createHash('sha256').update(sourceBytes).digest('hex'),
    scriptBase64: sourceBytes.toString('base64')
  };
  const payloadBytes = Buffer.from(JSON.stringify(payload), 'utf8');
  return {
    formatVersion: 1,
    keyId: 'tankoyomi-addon-2026-10',
    payload: payloadBytes.toString('base64'),
    signature: sign(null, Buffer.concat([SIGNING_PREFIX, payloadBytes]), privateKey).toString('base64')
  };
}

function testWorker(addonEnvelopes, fetchImpl) {
  return createWorker({
    manifest: { languages: [] },
    addonEnvelopes,
    fetchImpl
  });
}

test('fixed Spanish addon routes return client-verifiable signed envelopes without source-site requests', async () => {
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const sourceCalls = [];
  const envelopes = {};
  const scriptSources = {};
  for (const providerId of Object.keys(PROVIDERS)) {
    const sourceBytes = Buffer.from(`// fixture for ${providerId}\nexport default {};\n`, 'utf8');
    scriptSources[providerId] = sourceBytes;
    envelopes[providerId] = signedEnvelope(providerId, privateKey, sourceBytes, 7);
  }
  const worker = testWorker(envelopes, async (...args) => {
    sourceCalls.push(args);
    throw new Error('Addon delivery must not request the source site');
  });

  for (const providerId of Object.keys(PROVIDERS)) {
    const response = await worker.fetch(new Request(
      `https://worker.example/addons/v1/es/${providerId}.json`
    ));
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('content-type'), 'application/json');
    const envelope = await response.json();
    assert.deepEqual(envelope, envelopes[providerId]);
    assert.equal(envelope.formatVersion, 1);
    assert.equal(envelope.keyId, 'tankoyomi-addon-2026-10');

    const payloadBytes = Buffer.from(envelope.payload, 'base64');
    assert.equal(payloadBytes.toString('base64'), envelope.payload, 'payload Base64 is canonical');
    assert.equal(verify(
      null,
      Buffer.concat([SIGNING_PREFIX, payloadBytes]),
      publicKey,
      Buffer.from(envelope.signature, 'base64')
    ), true, `${providerId} signature verifies`);

    const payload = JSON.parse(payloadBytes.toString('utf8'));
    assert.equal(payload.contractVersion, 1);
    assert.equal(payload.addonId, 'colosseum.well.tankoyomi');
    assert.equal(payload.language, 'es');
    assert.equal(payload.providerId, providerId);
    assert.equal(payload.revision, 7);
    assert.deepEqual(payload.allowedHosts, PROVIDERS[providerId].allowedHosts);
    const scriptBytes = Buffer.from(payload.scriptBase64, 'base64');
    assert.equal(payload.scriptSha256, createHash('sha256').update(scriptBytes).digest('hex'));
    assert.deepEqual(scriptBytes, scriptSources[providerId]);
  }
  assert.deepEqual(sourceCalls, []);
});

test('addon routing accepts only the two fixed Spanish paths and GET', async () => {
  const { privateKey } = generateKeyPairSync('ed25519');
  const envelopes = Object.fromEntries(Object.keys(PROVIDERS).map(providerId => [
    providerId,
    signedEnvelope(providerId, privateKey, Buffer.from('fixture source'))
  ]));
  const worker = testWorker(envelopes, async () => { throw new Error('No upstream calls expected'); });
  const unknownPaths = [
    '/addons/v1/es/other.json',
    '/addons/v1/en/mangasin.json',
    '/addons/v1/es/mangasin.json/',
    '/addons/v1/es/%6dangasin.json',
    '/addons/v1/es/Mangasin.json'
  ];
  for (const path of unknownPaths) {
    const response = await worker.fetch(new Request(`https://worker.example${path}`));
    assert.equal(response.status, 404, path);
  }
  const methodResponse = await worker.fetch(new Request(
    'https://worker.example/addons/v1/es/mangasin.json',
    { method: 'POST' }
  ));
  assert.equal(methodResponse.status, 405);
});

test('missing published addon artifacts fail closed without fetching provider pages', async () => {
  const sourceCalls = [];
  const worker = testWorker({}, async (...args) => {
    sourceCalls.push(args);
    throw new Error('Must not fall back to a source-site request');
  });
  const response = await worker.fetch(new Request(
    'https://worker.example/addons/v1/es/lectorxd.json'
  ));
  assert.equal(response.status, 503);
  assert.deepEqual(await response.json(), {
    contractVersion: 1,
    error: 'Signed addon artifact is unavailable'
  });
  assert.deepEqual(sourceCalls, []);
});

test('production addon artifacts verify against the native pin and contain checked-in provider source', async () => {
  const keyHeader = await readFile(join(REPOSITORY_ROOT, 'native', 'engine', 'TankoyomiAddonPublicKey.h'), 'utf8');
  const keyMatch = keyHeader.match(/kTankoyomiAddonPublicKeyHex\[\]\s*=\s*"([a-f0-9]{64})"/i);
  assert.ok(keyMatch, 'native addon public key pin must be present');
  const publicKeyBytes = Buffer.from(keyMatch[1], 'hex');
  assert.equal(publicKeyBytes.length, 32);
  const pinnedPublicKey = createPublicKey({
    key: { kty: 'OKP', crv: 'Ed25519', x: publicKeyBytes.toString('base64url') },
    format: 'jwk'
  });
  const wrongPublicKey = generateKeyPairSync('ed25519').publicKey;

  for (const providerId of Object.keys(PROVIDERS)) {
    const productionResponse = await productionWorker.fetch(new Request(
      `https://worker.example/addons/v1/es/${providerId}.json`
    ));
    assert.equal(productionResponse.status, 200, `${providerId} production artifact is published`);
    assert.equal(productionResponse.headers.get('content-type'), 'application/json');
    const envelope = await productionResponse.json();
    assert.equal(envelope.formatVersion, 1);
    assert.equal(envelope.keyId, 'tankoyomi-addon-2026-10');

    const payloadBytes = Buffer.from(envelope.payload, 'base64');
    assert.equal(payloadBytes.toString('base64'), envelope.payload, 'production payload Base64 is canonical');
    const signatureBytes = Buffer.from(envelope.signature, 'base64');
    assert.equal(signatureBytes.length, 64);
    const signedBytes = Buffer.concat([SIGNING_PREFIX, payloadBytes]);
    assert.equal(verify(null, signedBytes, pinnedPublicKey, signatureBytes), true,
      `${providerId} verifies with the public key pinned by native code`);
    assert.equal(verify(null, signedBytes, wrongPublicKey, signatureBytes), false,
      `${providerId} does not verify with an unrelated key`);
    const tamperedSignature = Buffer.from(signatureBytes);
    tamperedSignature[0] ^= 1;
    assert.equal(verify(null, signedBytes, pinnedPublicKey, tamperedSignature), false,
      `${providerId} rejects a tampered signature`);

    const payloadText = new TextDecoder('utf-8', { fatal: true }).decode(payloadBytes);
    const payload = JSON.parse(payloadText);
    assert.equal(payload.contractVersion, 1);
    assert.equal(payload.addonId, 'colosseum.well.tankoyomi');
    assert.equal(payload.language, 'es');
    assert.equal(payload.providerId, providerId);
    assert.ok(Number.isSafeInteger(payload.revision) && payload.revision > 0);
    assert.deepEqual(payload.allowedHosts, PROVIDERS[providerId].allowedHosts);

    const scriptBytes = Buffer.from(payload.scriptBase64, 'base64');
    assert.equal(scriptBytes.toString('base64'), payload.scriptBase64, 'embedded script Base64 is canonical');
    assert.equal(payload.scriptSha256, createHash('sha256').update(scriptBytes).digest('hex'),
      `${providerId} hash covers the raw embedded script bytes`);
    const checkedInSource = await readFile(join(REPOSITORY_ROOT, PROVIDERS[providerId].source), 'utf8');
    const embeddedSource = new TextDecoder('utf-8', { fatal: true }).decode(scriptBytes);
    const normalizeLf = text => text.replace(/\r\n?/g, '\n');
    assert.equal(normalizeLf(embeddedSource), normalizeLf(checkedInSource),
      `${providerId} signed source matches checked-in source with line endings normalized`);
  }
});

test('offline signer covers the exact checked-in source for both supported providers', async t => {
  const passphrase = 'fixture only signing passphrase';
  const { privateKey, publicKey } = generateKeyPairSync('ed25519', {
    privateKeyEncoding: {
      type: 'pkcs8',
      format: 'pem',
      cipher: 'aes-256-cbc',
      passphrase
    }
  });
  const temporaryDirectory = await mkdtemp(join(tmpdir(), 'tankoyomi-sign-addons-'));
  t.after(() => rm(temporaryDirectory, { recursive: true, force: true }));
  const keyPath = join(temporaryDirectory, 'fixture-private-key.pem');
  await writeFile(keyPath, privateKey, 'utf8');

  for (const [providerId, provider] of Object.entries(PROVIDERS)) {
    const outputPath = join(temporaryDirectory, `${providerId}.json`);
    const result = spawnSync(process.execPath, [
      SIGNER_PATH,
      '--provider', providerId,
      '--private-key-file', keyPath,
      '--revision', '42',
      '--out', outputPath
    ], {
      cwd: REPOSITORY_ROOT,
      encoding: 'utf8',
      env: { ...process.env, COLOSSEUM_ADDON_SIGNING_PASSPHRASE: passphrase }
    });
    assert.equal(result.status, 0, result.stderr);
    assert.doesNotMatch(result.stdout + result.stderr, new RegExp(passphrase));

    const envelope = JSON.parse(await readFile(outputPath, 'utf8'));
    const payloadBytes = Buffer.from(envelope.payload, 'base64');
    assert.equal(payloadBytes.toString('base64'), envelope.payload);
    assert.equal(envelope.formatVersion, 1);
    assert.equal(envelope.keyId, 'tankoyomi-addon-2026-10');
    assert.equal(verify(
      null,
      Buffer.concat([SIGNING_PREFIX, payloadBytes]),
      publicKey,
      Buffer.from(envelope.signature, 'base64')
    ), true, `${providerId} CLI signature verifies`);

    const payloadText = payloadBytes.toString('utf8');
    const payload = JSON.parse(payloadText);
    assert.equal(JSON.stringify(payload), payloadText, 'payload JSON is compact and deterministic');
    assert.deepEqual(Object.keys(payload), [
      'contractVersion', 'addonId', 'language', 'providerId', 'revision',
      'allowedHosts', 'scriptSha256', 'scriptBase64'
    ]);
    assert.equal(payload.contractVersion, 1);
    assert.equal(payload.addonId, 'colosseum.well.tankoyomi');
    assert.equal(payload.language, 'es');
    assert.equal(payload.providerId, providerId);
    assert.equal(payload.revision, 42);
    assert.deepEqual(payload.allowedHosts, provider.allowedHosts);

    const checkedInSource = await readFile(join(REPOSITORY_ROOT, provider.source));
    const signedSource = Buffer.from(payload.scriptBase64, 'base64');
    assert.deepEqual(signedSource, checkedInSource, `${providerId} bytes match checked-in source`);
    assert.equal(payload.scriptSha256, createHash('sha256').update(checkedInSource).digest('hex'));
  }
});
