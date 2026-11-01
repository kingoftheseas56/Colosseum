#!/usr/bin/env node
import { createHash, createPrivateKey, sign } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const KEY_ID = 'tankoyomi-addon-2026-10';
const SIGNING_PREFIX = Buffer.from('Colosseum Tankoyomi addon v1\n', 'utf8');
const REPOSITORY_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ADDONS = Object.freeze({
  mangasin: Object.freeze({
    source: 'extensions/tankoyomi/languages/es/mangasin.js',
    allowedHosts: Object.freeze(['m440.in'])
  }),
  lectorxd: Object.freeze({
    source: 'extensions/tankoyomi/languages/es/lectorxd.js',
    allowedHosts: Object.freeze(['lectorxd.com'])
  })
});

const USAGE = `Usage: node scripts/tankoyomi-sign-addons.mjs --provider <mangasin|lectorxd> --private-key-file <PKCS8 PEM> --revision <positive integer> --out <file>

Signs the checked-in Spanish provider source as an offline Ed25519 addon envelope.
For encrypted PKCS8 keys, set COLOSSEUM_ADDON_SIGNING_PASSPHRASE in the environment.
Use server/tankoyomi-worker/src/addons/<provider>.json as --out to publish in the Worker bundle.
`;

function parseArguments(argv) {
  const values = new Map();
  const allowed = new Set(['--provider', '--private-key-file', '--revision', '--out']);
  for (let index = 0; index < argv.length; index++) {
    const name = argv[index];
    if (name === '--help' || name === '-h') return { help: true };
    if (!allowed.has(name)) throw new Error(`Unknown option: ${name}`);
    if (values.has(name)) throw new Error(`Option may only be supplied once: ${name}`);
    const value = argv[++index];
    if (!value || value.startsWith('--')) throw new Error(`Missing value for ${name}`);
    values.set(name, value);
  }

  for (const name of allowed) {
    if (!values.has(name)) throw new Error(`Required option is missing: ${name}`);
  }
  const providerId = values.get('--provider');
  if (!Object.hasOwn(ADDONS, providerId)) throw new Error('Provider must be mangasin or lectorxd');
  const revisionText = values.get('--revision');
  if (!/^[1-9]\d*$/.test(revisionText)) throw new Error('Revision must be a positive safe integer');
  const revision = Number(revisionText);
  if (!Number.isSafeInteger(revision)) throw new Error('Revision must be a positive safe integer');

  return {
    providerId,
    privateKeyFile: resolve(values.get('--private-key-file')),
    revision,
    outputFile: resolve(values.get('--out'))
  };
}

async function loadSigningKey(privateKeyFile) {
  const pem = await readFile(privateKeyFile);
  if (!/^-----BEGIN (?:ENCRYPTED )?PRIVATE KEY-----/m.test(pem.toString('ascii')))
    throw new Error('Signing key must be a PKCS8 PEM private key');
  const keyOptions = { key: pem, format: 'pem' };
  if (process.env.COLOSSEUM_ADDON_SIGNING_PASSPHRASE !== undefined)
    keyOptions.passphrase = process.env.COLOSSEUM_ADDON_SIGNING_PASSPHRASE;
  let privateKey;
  try {
    privateKey = createPrivateKey(keyOptions);
  } catch (_) {
    throw new Error('Could not load the PKCS8 signing key; check its passphrase');
  }
  if (privateKey.asymmetricKeyType !== 'ed25519')
    throw new Error('Signing key must use Ed25519');
  return privateKey;
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(USAGE);
    return;
  }
  if (options.privateKeyFile.toLowerCase() === options.outputFile.toLowerCase())
    throw new Error('Output file must not overwrite the private signing key');

  const addon = ADDONS[options.providerId];
  const sourceFile = resolve(REPOSITORY_ROOT, addon.source);
  if (sourceFile.toLowerCase() === options.outputFile.toLowerCase())
    throw new Error('Output file must not overwrite the checked-in addon source');
  const sourceBytes = await readFile(sourceFile);
  if (!sourceBytes.length) throw new Error('Checked-in addon source is empty');
  const payload = {
    contractVersion: 1,
    addonId: 'colosseum.well.tankoyomi',
    language: 'es',
    providerId: options.providerId,
    revision: options.revision,
    allowedHosts: [...addon.allowedHosts],
    scriptSha256: createHash('sha256').update(sourceBytes).digest('hex'),
    scriptBase64: sourceBytes.toString('base64')
  };
  const payloadBytes = Buffer.from(JSON.stringify(payload), 'utf8');
  const privateKey = await loadSigningKey(options.privateKeyFile);
  const signature = sign(null, Buffer.concat([SIGNING_PREFIX, payloadBytes]), privateKey);
  const envelope = {
    formatVersion: 1,
    keyId: KEY_ID,
    payload: payloadBytes.toString('base64'),
    signature: signature.toString('base64')
  };

  await mkdir(dirname(options.outputFile), { recursive: true });
  await writeFile(options.outputFile, `${JSON.stringify(envelope, null, 2)}\n`, 'utf8');
  process.stdout.write(`Signed ${options.providerId} revision ${options.revision} to ${options.outputFile}\n`);
}

main().catch(error => {
  process.stderr.write(`${error?.message || 'Could not sign addon'}\n${USAGE}`);
  process.exitCode = 1;
});
