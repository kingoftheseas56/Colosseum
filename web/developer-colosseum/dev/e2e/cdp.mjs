// The page-target CDP transport shared by attach.mjs and the parity rig.
// Qt WebEngine does not implement Playwright's browser-context commands.
export class Cdp {
  constructor(ws) {
    this.ws = ws;
    this.seq = 0;
    this.pending = new Map();
    ws.addEventListener('message', ev => {
      const m = JSON.parse(ev.data);
      if (m.id != null && this.pending.has(m.id)) {
        const { resolve, reject, timer } = this.pending.get(m.id);
        this.pending.delete(m.id);
        clearTimeout(timer);
        m.error ? reject(new Error(m.error.message || 'CDP error')) : resolve(m.result);
      }
    });
    ws.addEventListener('close', () => {
      for (const [id, p] of this.pending) {
        clearTimeout(p.timer);
        p.reject(new Error('CDP socket closed'));
        this.pending.delete(id);
      }
    });
  }
  static async connect(url) {
    const ws = new WebSocket(url);
    await new Promise((resolve, reject) => {
      ws.addEventListener('open', resolve, { once: true });
      ws.addEventListener('error', () => reject(new Error(`cannot open ${url}`)), { once: true });
    });
    return new Cdp(ws);
  }
  send(method, params = {}, timeoutMs = 15000) {
    const id = ++this.seq;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`CDP ${method} timed out`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  on(fn) {
    this.ws.addEventListener('message', ev => {
      const m = JSON.parse(ev.data);
      if (m.method) fn(m);
    });
  }
  close() { try { this.ws.close(); } catch (_) { /* already gone */ } }
}

export async function evaluate(cdp, expression) {
  const r = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: false });
  if (r.exceptionDetails) {
    const d = r.exceptionDetails;
    throw new Error('page eval failed: ' + ((d.exception && d.exception.description) || d.text));
  }
  return r.result ? r.result.value : undefined;
}

export async function connectColosseum(port) {
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find(t => t.type === 'page' && /developer-webui|developer-colosseum/.test(t.url || ''));
  if (!target) throw new Error(`No Colosseum web layer on port ${port}. Pages: ${targets.map(t => t.url).join(', ') || 'none'}`);
  const cdp = await Cdp.connect(target.webSocketDebuggerUrl);
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  return cdp;
}
