#!/usr/bin/env node
// Actual Colosseum + its QtWebEngine page; no external browser or dependencies.
// Node 24. By default ALL submission POSTs are mocked. --live-submit releases
// exactly one validated, labelled test POST and never retries it.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const { spawn, execFileSync } = require('node:child_process');
const { randomUUID, createHash } = require('node:crypto');
const { setTimeout: delay } = require('node:timers/promises');
const FORMSPARK_ENDPOINT = 'https://submit-form.com/VADEhLJlB';

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

class CDP {
  constructor(socket) {
    this.socket = socket; this.sequence = 0; this.pending = new Map(); this.listeners = new Map();
    socket.addEventListener('message', event => {
      const msg = JSON.parse(event.data);
      if (msg.id) {
        const waiter = this.pending.get(msg.id);
        if (!waiter) return;
        this.pending.delete(msg.id); clearTimeout(waiter.timer);
        msg.error ? waiter.reject(Error(JSON.stringify(msg.error))) : waiter.resolve(msg.result);
      } else for (const fn of this.listeners.get(msg.method) || []) fn(msg.params);
    });
    socket.addEventListener('close', () => {
      for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(Error('CDP disconnected')); }
      this.pending.clear();
    });
  }
  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { socket.close(); reject(Error('CDP connection timeout')); }, 10000);
      socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
      socket.addEventListener('error', () => { clearTimeout(timer); reject(Error('CDP connection error')); }, { once: true });
    });
    return new CDP(socket);
  }
  on(event, fn) { this.listeners.set(event, [...(this.listeners.get(event) || []), fn]); }
  send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++this.sequence;
      const timer = setTimeout(() => { this.pending.delete(id); reject(Error(`CDP ${method} timeout`)); }, 15000);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async evaluate(expression) {
    const result = await this.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
}

function validatePayload(body, expected) {
  assert.deepEqual(Object.keys(body).sort(), ['_email', '_gotcha', 'app_version', 'area', 'message', 'platform', 'reference', 'title', 'type'].sort(), 'Only controlled test fields may leave the app');
  assert.equal(body.title, expected.title);
  assert.equal(body.message, expected.message);
  assert.equal(body.type, 'Universe request');
  assert.equal(body.area, 'Universes');
  assert.equal(body.platform, expected.platform);
  assert.equal(body.app_version, expected.version);
  assert.match(body.reference, /^[\w-]{1,64}$/);
  assert.equal(Object.hasOwn(body, 'email'), false, 'Anonymous test must omit email');
  assert.equal(body._gotcha, '');
  assert.deepEqual(body._email, { subject: `[Colosseum] [Universe request] [Universes] ${expected.title}` });
}

function isOwnedTelemetry(request) {
  const url = new URL(request.url);
  return request.method === 'POST' && url.origin === 'https://getcolosseum.world' && url.pathname === '/cdn-cgi/rum';
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
  const reservation = net.createServer();
  await new Promise(resolve => reservation.listen(0, '127.0.0.1', resolve));
  const port = reservation.address().port;
  await new Promise(resolve => reservation.close(resolve));
  const evidence = { status: 'FAIL', id, platform: process.platform, liveRequested: args.live,
    sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
    executable: args.app, executableSha256: createHash('sha256').update(fs.readFileSync(args.app)).digest('hex'),
    qmlEntry: path.join(root, 'qml/Main.qml'), tag, pipe, session, stages: {}, submissions: [], realPosts: 0 };
  const save = () => fs.writeFileSync(path.join(args.output, 'receipt.json'), JSON.stringify(evidence, null, 2) + '\n');
  const env = { ...process.env, COLOSSEUM_APPDATA_TAG: tag, COLOSSEUM_LANISTA_PIPE: pipe,
    COLOSSEUM_LANISTA_DRIVE: '1', COLOSSEUM_LANISTA_WRITE: '0',
    QTWEBENGINE_REMOTE_DEBUGGING: `127.0.0.1:${port}`, QT_FORCE_STDERR_LOGGING: '1' };
  delete env.COLOSSEUM_DEV;
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
  let cdp;
  let interceptionError;
  let phase = 'rate-limit';
  let expected;
  const check = () => { alive(); if (interceptionError) { interceptionError.fatal = true; throw interceptionError; } };
  const get = async (object, props) => (await command(address, 'qml-get', { object, props })).props;
  const suppressTelemetry = async event => {
    if (!isOwnedTelemetry(event.request)) return false;
    evidence.suppressedTelemetryPosts = (evidence.suppressedTelemetryPosts || 0) + 1;
    // The deployed page sends a Cloudflare performance beacon. Keep it offline;
    // it is unrelated to the user feedback form, and its payload is not saved.
    await cdp.send('Fetch.fulfillRequest', { requestId: event.requestId, responseCode: 204 });
    return true;
  };
  const click = target => {
    evidence.lastAction = { command: 'ui-click', target, startedAt: new Date().toISOString() }; save();
    return command(address, 'ui-click', { target });
  };
  async function screenshot(name) {
    const native = await command(address, 'get-state', { grab: { target: 'window' } });
    assert(native.grabPath && fs.existsSync(native.grabPath), 'Native screenshot unavailable');
    fs.copyFileSync(native.grabPath, path.join(args.output, `${name}-app.png`));
    const picture = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(path.join(args.output, `${name}-document.png`), Buffer.from(picture.data, 'base64'));
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
    if (!(await get('colosseumTaskbar', ['open'])).open) await click('colosseumTaskbarHomeButton');
    await until(async () => (await get('colosseumTaskbar', ['open'])).open === true, 'actual taskbar opens');
    await delay(500); // The production dock width transition lasts 240 ms.
    const dock = await command(address, 'ui-query', { object: 'taskbarDock' });
    const feedback = await command(address, 'ui-query', { object: 'feedbackEntry' });
    const cx = feedback.rect.x + feedback.rect.width / 2;
    const cy = feedback.rect.y + feedback.rect.height / 2;
    assert(feedback.visible && feedback.enabled && feedback.rect.width > 0 && feedback.rect.height > 0,
      'Feedback is unavailable in the taskbar');
    assert(cx >= dock.rect.x && cx <= dock.rect.x + dock.rect.width &&
      cy >= dock.rect.y && cy <= dock.rect.y + dock.rect.height,
      'Feedback is outside the taskbar dock');
    evidence.taskbar = { dock, feedback };
    const dockShot = await command(address, 'get-state', { grab: { target: 'window' } });
    assert(dockShot.grabPath && fs.existsSync(dockShot.grabPath), 'Taskbar screenshot unavailable');
    fs.copyFileSync(dockShot.grabPath, path.join(args.output, 'taskbar-feedback-app.png'));
    evidence.taskbar.windowState = dockShot.windows;
    evidence.stages.feedbackTaskbar = true;
    await click('feedbackEntry');
    await until(async () => (await get('feedbackLayer', ['active'])).active, 'real feedback layer');
    assert.equal((await get('colosseumTaskbar', ['open'])).open, false, 'Feedback closes the taskbar');
    const target = await until(async () => {
      check();
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(3000) })).json();
      return list.find(t => {
        if (t.type !== 'page') return false;
        try { const url = new URL(t.url); return url.origin === 'https://getcolosseum.world' && url.pathname === '/feedback/'; }
        catch { return false; }
      });
    }, 'owned QtWebEngine target');
    evidence.documentUrl = target.url;
    const url = new URL(target.url);
    assert.equal(url.searchParams.get('platform'), { linux: 'linux', darwin: 'osx', win32: 'windows' }[process.platform]);
    assert(url.searchParams.get('version'), 'Actual app version missing');
    expected = { title: `TEST ${process.platform} feedback qualification ${id}`,
      message: `TEST ONLY: automated ${process.platform} actual-app feedback qualification ${id}. Please disregard this labelled test universe request.`,
      platform: { linux: 'Linux', darwin: 'macOS', win32: 'Windows' }[process.platform], version: url.searchParams.get('version') };
    cdp = await CDP.connect(target.webSocketDebuggerUrl);
    evidence.browser = await cdp.send('Browser.getVersion');
    await cdp.send('Network.enable'); await cdp.send('Page.enable');
    cdp.on('Fetch.requestPaused', event => {
      (async () => {
        const request = event.request;
        if (request.method !== 'POST') { await cdp.send('Fetch.continueRequest', { requestId: event.requestId }); return; }
        if (await suppressTelemetry(event)) return;
        try {
          assert.equal(request.url, FORMSPARK_ENDPOINT, 'Only the approved Formspark form may receive this test');
          assert.equal(request.headers['Content-Type'] || request.headers['content-type'], 'application/json');
          const body = JSON.parse(request.postData);
          validatePayload(body, expected);
          if (evidence.submissions.length) assert.equal(request.url, evidence.submissions[0].url, 'Submission endpoint changed');
          const row = { phase, url: request.url, payload: body, networkId: event.networkId };
          evidence.submissions.push(row);
          if (phase === 'submit' && args.live && evidence.realPosts === 0) {
            evidence.realPosts++; row.live = true; save();
            await cdp.send('Fetch.continueRequest', { requestId: event.requestId });
          } else {
            assert(phase === 'rate-limit' || (phase === 'submit' && !args.live), 'Unexpected additional POST blocked');
            row.live = false;
            await cdp.send('Fetch.fulfillRequest', { requestId: event.requestId,
              responseCode: phase === 'rate-limit' ? 429 : 200,
              responseHeaders: [{ name: 'Content-Type', value: 'application/json' }, { name: 'Access-Control-Allow-Origin', value: 'https://getcolosseum.world' }],
              body: Buffer.from('{}').toString('base64') });
          }
        } catch (error) {
          await cdp.send('Fetch.failRequest', { requestId: event.requestId, errorReason: 'BlockedByClient' }).catch(() => {});
          throw error;
        }
      })().catch(error => { interceptionError = error; });
    });
    cdp.on('Network.responseReceived', event => {
      const row = evidence.submissions.find(s => s.live && s.networkId === event.requestId);
      if (row) row.response = { status: event.response.status, url: event.response.url };
    });
    cdp.on('Network.loadingFinished', event => {
      const row = evidence.submissions.find(s => s.live && s.networkId === event.requestId);
      if (row) cdp.send('Network.getResponseBody', { requestId: event.requestId }).then(result => {
        row.responseBody = result.base64Encoded ? Buffer.from(result.body, 'base64').toString('utf8') : result.body;
      }).catch(error => { row.responseBodyUnavailable = error.message; });
    });
    // All request-stage POSTs in this owned page are paused before leaving Qt.
    await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*', requestStage: 'Request' }] });
    const domWait = (expression, label) => until(async () => { check(); return cdp.evaluate(expression); }, label);
    await domWait('!!document.querySelector("#send:not(:disabled)")', 'hosted form configuration');
    evidence.stages.realPage = true;
    await cdp.evaluate('document.querySelector("#send").click()');
    await domWait('!!document.querySelector("#title-error")?.textContent && !!document.querySelector("#message-error")?.textContent', 'empty form validation');
    assert.equal(evidence.submissions.length, 0); evidence.stages.emptyValidation = true;
    await cdp.evaluate(`(() => {
      document.querySelector('[name="kind"][value="universe"]').click();
      document.querySelector('#area-trigger').click();
      const option = [...document.querySelectorAll('#area-menu [role="option"]')].find(x => x.textContent.trim() === 'Universes');
      if (!option) throw Error('Universe category unavailable'); option.click();
      for (const [id, value] of Object.entries(${JSON.stringify({ title: expected.title, message: expected.message, email: '' })})) {
        const field = document.getElementById(id); field.value = value; field.dispatchEvent(new Event('input', { bubbles: true }));
      }
    })()`);
    await cdp.evaluate('document.querySelector("#send").click()');
    await domWait('document.querySelector("#status").textContent.includes("Too many attempts")', 'controlled 429');
    assert.equal(evidence.realPosts, 0); assert.equal(evidence.submissions.length, 1);
    const reference = evidence.submissions[0].payload.reference;
    evidence.stages.controlled429 = true;
    await screenshot('rate-limit');
    await click('feedbackRefresh');
    await domWait(`!!document.querySelector('#send:not(:disabled)') && document.querySelector('#title').value === ${JSON.stringify(expected.title)} && document.querySelector('#message').value === ${JSON.stringify(expected.message)} && document.querySelector('[name="kind"]:checked').value === 'universe' && document.querySelector('#area').value === 'Universes'`, 'draft survives real refresh');
    evidence.stages.draftReload = true;
    phase = 'submit';
    await cdp.evaluate('document.querySelector("#send").click()');
    await domWait('!!document.querySelector("#success:not([hidden])")', 'submission success');
    check(); assert.equal(evidence.submissions.length, 2);
    assert.equal(evidence.submissions[1].payload.reference, reference);
    assert((await cdp.evaluate('document.querySelector("#reference").textContent')).includes(reference));
    if (args.live) {
      assert.equal(evidence.realPosts, 1);
      await until(() => evidence.submissions[1].response, 'provider HTTP response');
      assert(evidence.submissions[1].response.status >= 200 && evidence.submissions[1].response.status < 300);
    }
    assert.equal(await cdp.evaluate('localStorage.getItem("colosseum-feedback-v1")'), null);
    evidence.stages.success = true;
    await screenshot('success');
    phase = 'closed';
    await click('feedbackBack');
    await until(async () => !(await get('feedbackLayer', ['active'])).active, 'feedback Back');
    cdp.socket.close(); cdp = null;
    if (!(await get('colosseumTaskbar', ['open'])).open) await click('colosseumTaskbarHomeButton');
    await until(async () => (await get('colosseumTaskbar', ['open'])).open, 'taskbar opens for feedback reopen');
    await delay(300);
    await click('feedbackEntry');
    await until(async () => (await get('feedbackLayer', ['active'])).active, 'feedback reopen');
    const reopened = await until(async () => {
      check();
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(3000) })).json();
      return list.find(t => t.type === 'page' && t.url === target.url);
    }, 'reopened Qt feedback document');
    cdp = await CDP.connect(reopened.webSocketDebuggerUrl);
    cdp.on('Fetch.requestPaused', event => {
      (async () => {
        if (await suppressTelemetry(event)) return;
        if (event.request.method === 'POST') {
          interceptionError = Error('Unexpected POST on reopened feedback page');
          await cdp.send('Fetch.failRequest', { requestId: event.requestId, errorReason: 'BlockedByClient' });
        } else await cdp.send('Fetch.continueRequest', { requestId: event.requestId });
      })().catch(error => { interceptionError = error; });
    });
    await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*', requestStage: 'Request' }] });
    await domWait('!!document.querySelector("#send:not(:disabled)") && document.querySelector("#title").value === "" && document.querySelector("#message").value === ""', 'reopened form has no submitted draft');
    evidence.controls = {};
    for (const name of ['feedbackBack', 'feedbackWindowMinimize', 'feedbackWindowFullscreen', 'feedbackWindowQuit']) {
      const item = await command(address, 'ui-query', { object: name });
      assert(item.visible && item.enabled && !item.clippedByWindow && item.rect.width > 0 && item.rect.height > 0, `Window control unavailable: ${name}`);
      evidence.controls[name] = item;
    }
    evidence.stages.backReopen = true; evidence.stages.windowControlsGeometry = true;
    check(); assert.equal(evidence.submissions.length, 2);
    await click('feedbackWindowQuit');
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
    if (cdp) cdp.socket.close();
    fs.closeSync(log); save();
    console.log(`${evidence.status}: ${path.join(args.output, 'receipt.json')}`);
    if (evidence.error) console.error(evidence.error);
  }
}

if (require.main === module) main().catch(error => { console.error(error.stack); process.exitCode = 1; });
module.exports = { argumentsOf, validatePayload, isOwnedTelemetry, command, CDP };
