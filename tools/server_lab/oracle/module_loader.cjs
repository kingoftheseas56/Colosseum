'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');
const vm = require('node:vm');
const { createRequire } = require('node:module');
const { createClock } = require('./trace_clock.cjs');
const { createRandom } = require('./trace_random.cjs');

function splitTopLevel(source, open) {
  const spans = [];
  let start = open + 1;
  let depth = 0;
  let quote = null;
  let escaped = false;
  let prevSig = '';
  for (let i = open + 1; i < source.length; i += 1) {
    const c = source[i];
    const n = source[i + 1];
    if (quote) {
      if (escaped) escaped = false;
      else if (c === '\\') escaped = true;
      else if (quote === '`' && c === '$' && n === '{') {
        let braces = 1;
        i += 2;
        while (i < source.length && braces) {
          if (source[i] === '{') braces += 1;
          else if (source[i] === '}') braces -= 1;
          else if (source[i] === '"' || source[i] === "'") {
            const nested = source[i];
            i += 1;
            while (i < source.length && source[i] !== nested) i += source[i] === '\\' ? 2 : 1;
          }
          i += 1;
        }
        continue;
      }
      else if (c === quote) quote = null;
      continue;
    }
    if (c === '/' && n === '/') { const end = source.indexOf('\n', i + 2); i = end < 0 ? source.length : end; continue; }
    if (c === '/' && n === '*') { const end = source.indexOf('*/', i + 2); i = end < 0 ? source.length : end + 1; continue; }
    if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
    if (c === '/') {
      let word = '';
      for (let k = i - 1; k >= 0 && /[A-Za-z]/.test(source[k]); k -= 1) word = source[k] + word;
      const regexStart = '(,=:[!&|?{};+-*%~^<>'.includes(prevSig) || prevSig === '' || ['return', 'typeof', 'instanceof', 'in', 'of', 'new', 'delete', 'void', 'throw', 'case', 'do', 'else', 'yield', 'await'].includes(word);
      if (regexStart) {
        let inClass = false;
        for (i += 1; i < source.length; i += 1) {
          if (source[i] === '\\') i += 1;
          else if (source[i] === '[') inClass = true;
          else if (source[i] === ']') inClass = false;
          else if (source[i] === '/' && !inClass) break;
          else if (source[i] === '\n') break;
        }
        prevSig = '/';
        continue;
      }
    }
    if (c === '(' || c === '[' || c === '{') depth += 1;
    else if (c === ')' || c === ']' || c === '}') {
      if (c === ']' && depth === 0) { spans.push([start, i]); return { spans, close: i }; }
      depth -= 1;
    } else if (c === ',' && depth === 0) { spans.push([start, i]); start = i + 1; }
    if (!/\s/.test(c)) prevSig = c;
  }
  throw new Error('webpack module array never closed');
}

function locateArray(source) {
  const matches = [...source.matchAll(/}\s*\)?\s*\(\s*\[/g)];
  if (!matches.length) throw new Error('could not locate webpack module array');
  return source.indexOf('[', matches[0].index);
}

function extractModules(bundlePath) {
  const source = fs.readFileSync(bundlePath);
  const text = source.toString('latin1');
  const open = locateArray(text);
  let spans;
  let close;
  try {
    ({ spans, close } = splitTopLevel(text, open));
  } catch (error) {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'p05-module-split-'));
    try {
      const splitter = path.resolve(__dirname, '../../../docs/research/tankorent2-phase0/labscripts/split_bundle.py');
      const result = spawnSync(process.env.PYTHON || 'python', [splitter, bundlePath, temp], { encoding: 'utf8' });
      if (result.status !== 0) throw error;
      const index = JSON.parse(fs.readFileSync(path.join(temp, 'index.json'), 'utf8'));
      spans = index.map((item) => [item.start, item.end]);
      close = source.length - fs.readFileSync(path.join(temp, '_epilogue.txt')).length;
    } finally {
      fs.rmSync(temp, { recursive: true, force: true });
    }
  }
  const modules = spans.map(([start, end], id) => {
    const bytes = source.subarray(start, end);
    return { id, start, end, length: end - start, sha256: crypto.createHash('sha256').update(bytes).digest('hex'), source: bytes.toString('utf8') };
  });
  return { bundlePath: path.resolve(bundlePath), source, open, close, sha256: crypto.createHash('sha256').update(source).digest('hex'), modules };
}

function verifyModuleIndex(bundlePath, indexPath) {
  const extracted = extractModules(bundlePath);
  const expected = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
  if (expected.oracleSha256 && expected.oracleSha256 !== extracted.sha256) throw new Error('oracle hash mismatch');
  const byId = new Map((expected.modules || []).map((item) => [item.id, item]));
  if (byId.size !== extracted.modules.length) throw new Error('module index mismatch: module count');
  for (const actual of extracted.modules) {
    const item = byId.get(actual.id);
    if (!item || item.start !== actual.start || item.end !== actual.end || item.sha256 !== actual.sha256) throw new Error(`module index mismatch: ${actual.id}`);
  }
  return { ok: true, sha256: extracted.sha256, modules: extracted.modules.length };
}

function loadBundle(bundlePath, options = {}) {
  const extracted = extractModules(bundlePath);
  if (options.indexPath) verifyModuleIndex(bundlePath, options.indexPath);
  const clock = createClock(options.clock ?? (options.now ? options.now : {}));
  const random = createRandom(options.random ?? 1);
  const realDate = Date;
  class TraceDate extends realDate {
    constructor(...args) { super(...(args.length ? args : [clock.now()])); }
    static now() { return clock.now(); }
  }
  const traceMath = Object.create(Math);
  traceMath.random = random;
  const context = vm.createContext({
    Buffer, console, process, setTimeout, clearTimeout, setInterval, clearInterval,
    Date: TraceDate, Math: traceMath,
  });
  const cache = new Map();
  const nodeRequire = createRequire(path.resolve(bundlePath));
  function webpackRequire(id) {
    if (!Number.isInteger(id) || id < 0 || id >= extracted.modules.length || !extracted.modules[id].source.trim()) throw new Error(`unknown webpack module ${id}`);
    if (cache.has(id)) return cache.get(id).exports;
    const module = { exports: {} };
    cache.set(id, module);
    const localRequire = (request) => Number.isInteger(request) ? webpackRequire(request) : nodeRequire(request);
    const script = new vm.Script(`(${extracted.modules[id].source})`, { filename: `${bundlePath}#${id}` });
    script.runInContext(context)(module, module.exports, localRequire);
    return module.exports;
  }
  return { require: webpackRequire, modules: extracted.modules, sha256: extracted.sha256, clock, random };
}

module.exports = { extractModules, loadBundle, verifyModuleIndex };
