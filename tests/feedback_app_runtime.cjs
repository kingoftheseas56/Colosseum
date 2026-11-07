#!/usr/bin/env node
// Actual Colosseum and its native request form, driven through Lanista.
// Node 24. The default run performs no submissions.
// --live-submit clicks Send once with a labelled feature request and waits for success.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const { spawn, execFileSync } = require('node:child_process');
const { randomUUID, createHash } = require('node:crypto');
const { setTimeout: delay } = require('node:timers/promises');

function argumentsOf(argv) {
  const args = { live: false };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--live-submit') args.live = true;
    else if (argv[i] === '--app' || argv[i] === '--output') {
      const key = argv[i].slice(2);
      if (!argv[i + 1] || argv[i + 1].startsWith('--')) throw Error(`Missing ${key}`);
      args[key] = path.resolve(argv[++i]);
    } else throw Error(`Unknown argument ${argv[i]}`);
  }
  assert(args.app && args.output, 'Usage: node tests/feedback_app_runtime.cjs --app <nativeExe> --output <dir> [--live-submit]');
  return args;
}

async function until(fn, label, ms = 30000) {
  const end = Date.now() + ms;
  let last;
  while (Date.now() < end) {
    try { const value = await fn(); if (value) return value; } catch (e) { if (e.fatal) throw e; last = e; }
    await delay(250);
  }
  throw Error(`Timed out: ${label}${last ? `: ${last.message}` : ''}`);
}

function command(address, cmd, payload = {}) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(address);
    let buffer = '';
    const finish = (err, result) => { socket.destroy(); err ? reject(err) : resolve(result); };
    socket.setTimeout(cmd === 'ui-click' ? 45000 : 5000,
      () => finish(Error(`Lanista ${cmd}${payload.target ? ` ${payload.target}` : ''} timed out`)));
    socket.on('error', reject);
    socket.on('connect', () => socket.write(JSON.stringify({ seq: 1, cmd, payload }) + '\n'));
    socket.on('data', bytes => {
      buffer += bytes;
      if (!buffer.includes('\n')) return;
      try {
        const reply = JSON.parse(buffer.split('\n')[0]);
        assert.equal(reply.type, 'reply', JSON.stringify(reply));
        finish(null, reply);
      } catch (e) { finish(e); }
    });
    socket.on('end', () => { if (!buffer.includes('\n')) reject(Error('Lanista closed without reply')); });
  });
}

async function main() {
  const args = argumentsOf(process.argv.slice(2));
  assert(Number(process.versions.node.split('.')[0]) >= 24, 'Node 24 required');
  assert(['linux', 'darwin', 'win32'].includes(process.platform));
  assert(fs.statSync(args.app).isFile(), 'Native executable missing');
  fs.mkdirSync(args.output, { recursive: true });
  const root = path.resolve(__dirname, '..');
  const id = `${Date.now()}-${randomUUID().slice(0, 8)}`;
  const tag = `feedback-qualification-${process.platform}-${id}`;
  // Short AF_UNIX path: macOS sockaddr_un has a 104-byte path limit.
  const session = fs.mkdtempSync(path.join(os.tmpdir(), 'cf-'));
  const pipe = process.platform === 'win32' ? `ColosseumFeedback-${id}` : path.join(session, 'l.sock');
  const address = process.platform === 'win32' ? `\\\\.\\pipe\\${pipe}` : pipe;
  const evidence = { status: 'FAIL', id, platform: process.platform, liveRequested: args.live,
    sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
    executable: args.app, executableSha256: createHash('sha256').update(fs.readFileSync(args.app)).digest('hex'),
    qmlEntry: path.join(root, 'qml/Main.qml'), tag, pipe, session, stages: {}, liveSendClicks: 0 };
  const save = () => fs.writeFileSync(path.join(args.output, 'receipt.json'), JSON.stringify(evidence, null, 2) + '\n');
  const env = { ...process.env, COLOSSEUM_APPDATA_TAG: tag, COLOSSEUM_LANISTA_PIPE: pipe,
    COLOSSEUM_LANISTA_DRIVE: '1', COLOSSEUM_LANISTA_WRITE: '0',
    QT_FORCE_STDERR_LOGGING: '1' };
  delete env.COLOSSEUM_DEV;
  delete env.QTWEBENGINE_REMOTE_DEBUGGING;
  // Linux's XDG config needs isolation too: onboarding may use QSettings.
  if (process.platform === 'linux') {
    env.XDG_CONFIG_HOME = path.join(session, 'config');
    env.XDG_DATA_HOME = path.join(session, 'data');
    env.XDG_CACHE_HOME = path.join(session, 'cache');
  }
  const log = fs.openSync(path.join(args.output, 'native-process.log'), 'w');
  const child = spawn(args.app, [evidence.qmlEntry], { cwd: root, env, stdio: ['ignore', log, log], detached: process.platform !== 'win32' });
  let spawnError;
  child.on('error', error => { spawnError = error; });
  let ended = false;
  const exited = new Promise(resolve => child.on('exit', (code, signal) => { ended = true; evidence.exit = { code, signal }; resolve(code); }));
  const alive = () => {
    if (spawnError || ended) {
      const error = spawnError || Error(`Native process exited early: ${JSON.stringify(evidence.exit)}`);
      error.fatal = true; throw error;
    }
  };
  const get = async (object, props) => (await command(address, 'qml-get', { object, props })).props;
  const appWait = (fn, label, ms) => until(async () => { alive(); return fn(); }, label, ms);
  const click = target => {
    evidence.lastAction = { command: 'ui-click', target, startedAt: new Date().toISOString() }; save();
    return command(address, 'ui-click', { target });
  };
  async function screenshot(name) {
    const native = await command(address, 'get-state', { grab: { target: 'window' } });
    assert(native.grabPath && fs.existsSync(native.grabPath), 'Native screenshot unavailable');
    fs.copyFileSync(native.grabPath, path.join(args.output, `${name}-app.png`));
  }
  async function openForm(button, kind) {
    const target = await command(address, 'ui-query', { object: button });
    assert(target.visible && target.enabled && target.rect.width > 0 && target.rect.height > 0,
      `The Home ${kind} button is unavailable`);
    await click(button);
    await appWait(async () => {
      if (!(await get('feedbackLayer', ['active'])).active) return false;
      const form = await get('requestFormOverlay', ['visible', 'kind']);
      return form.visible && form.kind === kind && (await get('requestFormPanel', ['visible'])).visible;
    }, `native ${kind} form`);
    return target;
  }
  async function closeForm() {
    await click('requestFormClose');
    await appWait(async () => !(await get('feedbackLayer', ['active'])).active, 'request form closed');
  }
  async function fieldText(key) {
    return (await get(`requestField_${key}`, ['text'])).text;
  }
  async function typeField(key, text) {
    const reply = await command(address, 'ui-text-input', { target: `requestField_${key}`, text });
    assert.equal(reply.typed, text);
    assert.equal(await fieldText(key), text, `Native editor did not receive the complete ${key} field`);
  }

  try {
    evidence.pid = child.pid; save();
    await until(async () => {
      alive();
      const ping = await command(address, 'ping');
      assert.equal(ping.pid, child.pid, 'Lanista belongs to another process');
      assert.equal(ping.gates.drive, true); assert.equal(ping.gates.write, false);
      const state = await command(address, 'get-state');
      assert(state.appDataRoot.includes(tag) && state.cacheRoot.includes(tag), 'Profile escaped tagged roots');
      evidence.storage = { appDataRoot: state.appDataRoot, cacheRoot: state.cacheRoot };
      const splash = await get('bootSplash', ['visible']);
      return state.windows.some(w => w.visible && w.width > 0 && w.height > 0) && splash.visible === false;
    }, 'native boot', 120000);
    if ((await get('accountHost', ['visible'])).visible) {
      await click('accountWelcomeContinueLocal');
      await until(async () => !(await get('accountHost', ['visible'])).visible, 'local-only onboarding');
    }
    evidence.stages.boot = true;
    evidence.homeFeedback = {};
    evidence.homeFeedback.bug = await openForm('homeReportBug', 'bug');
    assert.equal((await get('requestFormSend', ['enabled'])).enabled, false, 'An empty bug report cannot be sent');
    await closeForm();

    evidence.homeFeedback.feature = await openForm('homeRequestFeature', 'feature');
    assert.equal((await get('requestFormSend', ['enabled'])).enabled, false, 'An empty feature request cannot be sent');
    evidence.stages.nativeForms = true;
    evidence.stages.emptyValidation = true;
    await screenshot('empty');

    const title = `TEST ${process.platform} feedback qualification ${id}`;
    const message = `TEST ONLY: automated ${process.platform} actual-app feedback qualification ${id}. Please disregard this labelled test feature idea.`;
    await typeField('title', title);
    assert.equal((await get('requestFormSend', ['enabled'])).enabled, false, 'A request with only a title cannot be sent');
    await typeField('message', message);
    await appWait(async () => (await get('requestFormSend', ['enabled'])).enabled, 'completed feature request');
    await screenshot('draft');
    await closeForm();

    await openForm('homeReportBug', 'bug');
    assert.equal(await fieldText('title'), '', 'Feature drafts must stay separate from bug reports');
    await closeForm();
    await openForm('homeRequestFeature', 'feature');
    assert.equal(await fieldText('title'), title, 'Draft title was lost on close/reopen');
    assert.equal(await fieldText('message'), message, 'Draft message was lost on close/reopen');
    assert.equal((await get('requestFormSend', ['enabled'])).enabled, true);
    evidence.stages.draftReopen = true;

    if (args.live) {
      // The native transport has separate offline coverage for the wire payload.
      // Verify the exact labelled editor contents before the only live click.
      assert.equal(await fieldText('email'), '');
      assert.equal(await fieldText('benefit'), '');
      evidence.liveRequest = { kind: 'feature', title, message };
      evidence.liveSendClicks++;
      await click('requestFormSend');
      const result = await appWait(async () => {
        const state = await get('requestFormOverlay', ['sent', 'sending', 'statusText']);
        if (!state.sending && !state.sent && state.statusText) {
          const error = Error(`Native request failed: ${state.statusText}`);
          error.fatal = true;
          throw error;
        }
        return state.sent && state;
      }, 'native submission success');
      evidence.liveRequest.success = result.statusText;
      evidence.stages.success = true;
      await screenshot('success');
      await closeForm();

      await openForm('homeRequestFeature', 'feature');
      assert.equal(await fieldText('title'), '', 'Submitted draft title was restored');
      assert.equal(await fieldText('message'), '', 'Submitted draft message was restored');
      assert.equal((await get('requestFormSend', ['enabled'])).enabled, false);
      evidence.stages.submittedDraftCleared = true;
    }
    assert.equal(evidence.liveSendClicks, args.live ? 1 : 0);
    await closeForm();
    alive();
    await command(address, 'ui-keypress', { key: 'Ctrl+Q' });
    assert.equal(await Promise.race([exited, delay(30000).then(() => { throw Error('Normal quit timed out'); })]), 0);
    evidence.stages.cleanExit = true;
    evidence.status = 'PASS';
  } catch (error) {
    evidence.error = error.stack || String(error);
    process.exitCode = 1;
  } finally {
    if (!ended && !spawnError) {
      // First request the app's own graceful quit, then kill only our child tree.
      await command(address, 'ui-keypress', { key: 'Ctrl+Q' }).catch(() => {});
      await Promise.race([exited, delay(5000)]);
      if (!ended) {
        evidence.forcedCleanup = true;
        if (process.platform === 'win32') {
          try { execFileSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore' }); } catch {}
        } else { try { process.kill(-child.pid, 'SIGKILL'); } catch {} }
        await Promise.race([exited, delay(3000)]);
      }
    }
    fs.closeSync(log); save();
    console.log(`${evidence.status}: ${path.join(args.output, 'receipt.json')}`);
    if (evidence.error) console.error(evidence.error);
  }
}

if (require.main === module) main().catch(error => { console.error(error.stack); process.exitCode = 1; });
module.exports = { argumentsOf, command };
