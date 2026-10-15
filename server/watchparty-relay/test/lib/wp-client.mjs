// Colosseum Watch Party relay — reusable scripted-client library (Slice 5 of
// docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md).
//
// This module is the SCRIPTED HOST instrument Slices 6-9 reuse: a thin,
// typed wrapper over `ws` that speaks protocol v3 envelopes, gives every
// caller a message-or-timeout wait primitive (never a sleep), and exposes
// the same connect() shape whether the caller wants a signed-in bearer
// connection, a guest connection, a malformed-protocol-header probe, or a
// raw/forged frame sender for the refusal matrix.
//
// Design note: each WpClient keeps ONE pending waiter at a time (a
// FIFO-consuming cursor over its own inbound message log). The probes that
// use this library are sequential per-connection scripts — await one
// expectation before issuing the next action on that same connection — so a
// single-slot waiter is the smallest thing that is still correct; a second
// concurrent waitFor on the same client throws immediately rather than
// silently racing.

import WebSocket from "ws";

export const PROTOCOL_VERSION = 3;
export const PROTOCOL_HEADER = "X-Colosseum-Watch-Party-Protocol";

/**
 * Opens a WebSocket to `url` and resolves with a connected WpClient once the
 * upgrade completes, or rejects with an Error (carrying `.statusCode` when
 * the upgrade was refused at the HTTP layer, e.g. 426) on failure/timeout.
 *
 * @param {string} url
 * @param {object} [opts]
 * @param {string|null} [opts.bearer] Authorization: Bearer <token>. Omit for guest/no-credential.
 * @param {string|null|undefined} [opts.protocolHeader] X-Colosseum-Watch-Party-Protocol value. "3" is
 *   the valid header; pass "2" or undefined to exercise the refusal path.
 * @param {number} [opts.timeoutMs]
 * @param {string} [opts.label] Human label used in error/timeout messages.
 */
export function connect(url, opts = {}) {
  const {
    bearer = null,
    protocolHeader = "3",
    timeoutMs = 10_000,
    label = "client",
  } = opts;

  return new Promise((resolve, reject) => {
    const headers = {};
    if (protocolHeader !== undefined && protocolHeader !== null) {
      headers[PROTOCOL_HEADER] = protocolHeader;
    }
    if (bearer) {
      headers["Authorization"] = `Bearer ${bearer}`;
    }

    const ws = new WebSocket(url, { headers });
    let settled = false;

    const timer = setTimeout(() => {
      if (settled) return;
      settled = true;
      try {
        ws.terminate();
      } catch {
        // best effort
      }
      reject(new Error(`[${label}] connect timeout after ${timeoutMs}ms`));
    }, timeoutMs);

    ws.on("unexpected-response", (_req, res) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      const err = new Error(
        `[${label}] upgrade refused: HTTP ${res.statusCode}`
      );
      err.statusCode = res.statusCode;
      resolve({ refused: true, statusCode: res.statusCode });
    });

    ws.on("open", () => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve(new WpClient(ws, label));
    });

    ws.on("error", (err) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(
        new Error(
          `[${label}] connection error before open: ${err && err.message ? err.message : String(err)}`
        )
      );
    });
  });
}

export class WpClient {
  constructor(ws, label = "client") {
    this.ws = ws;
    this.label = label;
    this.messages = [];
    this.cursor = 0;
    this.closed = false;
    this.closeInfo = null;
    this._waiter = null;
    this._nextSequence = 1;

    ws.on("message", (data) => {
      let msg;
      try {
        msg = JSON.parse(data.toString());
      } catch {
        msg = { __parseError: true, raw: data.toString() };
      }
      this.messages.push(msg);
      this._pump();
    });

    ws.on("close", (code, reason) => {
      this.closed = true;
      this.closeInfo = { code, reason: reason ? reason.toString() : "" };
      this._pump();
    });
  }

  _pump() {
    if (!this._waiter) return;
    while (this.cursor < this.messages.length) {
      const msg = this.messages[this.cursor++];
      if (this._waiter.predicate(msg)) {
        const w = this._waiter;
        this._waiter = null;
        clearTimeout(w.timer);
        w.onMatch(msg);
        return;
      }
    }
    if (this.closed && this._waiter) {
      const w = this._waiter;
      this._waiter = null;
      clearTimeout(w.timer);
      w.onClosed();
    }
  }

  /**
   * Resolves with the first unconsumed message satisfying `predicate`, or
   * rejects on timeout / socket close first. Never sleeps — every outcome is
   * either a matching message or the timer.
   */
  waitFor(predicate, { timeoutMs = 5000, label = "message" } = {}) {
    if (this._waiter) {
      return Promise.reject(
        new Error(
          `[${this.label}] waitFor(${label}) called while already waiting for '${this._waiter.label}'`
        )
      );
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this._waiter = null;
        reject(
          new Error(`[${this.label}] timeout after ${timeoutMs}ms waiting for ${label}`)
        );
      }, timeoutMs);
      this._waiter = {
        predicate,
        label,
        timer,
        onMatch: resolve,
        onClosed: () =>
          reject(
            new Error(
              `[${this.label}] socket closed (${JSON.stringify(this.closeInfo)}) while waiting for ${label}`
            )
          ),
      };
      this._pump();
    });
  }

  waitForType(type, opts = {}) {
    return this.waitFor((m) => m && m.type === type, {
      timeoutMs: opts.timeoutMs,
      label: opts.label || type,
    });
  }

  /**
   * Asserts NO unconsumed/future message satisfies `predicate` within
   * `windowMs`. Resolves (the expected outcome) on timeout; rejects if a
   * matching message arrives first. This IS a message-or-timeout wait, just
   * with the timeout as the pass condition — used for "no broadcast to the
   * bystander" assertions.
   */
  expectNone(predicate, { windowMs = 1500, label = "unexpected message" } = {}) {
    if (this._waiter) {
      return Promise.reject(
        new Error(
          `[${this.label}] expectNone(${label}) called while already waiting for '${this._waiter.label}'`
        )
      );
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this._waiter = null;
        resolve();
      }, windowMs);
      this._waiter = {
        predicate,
        label,
        timer,
        onMatch: (msg) =>
          reject(
            new Error(`[${this.label}] unexpected ${label}: ${JSON.stringify(msg)}`)
          ),
        onClosed: () => {
          // Socket closing while we were only waiting to see it NOT arrive is
          // not itself a failure of this expectation.
          resolve();
        },
      };
      this._pump();
    });
  }

  /** Waits (message-or-timeout) for the socket to stop being usable — a
   * clean close OR a transport-level error (e.g. the server process was
   * killed mid-connection, which surfaces as ECONNRESET/'error' and may
   * never emit a normal 'close' frame). Either counts as "transport
   * failure observed", which is exactly what a killed-relay assertion
   * needs — never a hang either way. */
  waitForClose({ timeoutMs = 5000, label = "close" } = {}) {
    if (this.closed) return Promise.resolve(this.closeInfo);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.ws.removeListener("close", onClose);
        this.ws.removeListener("error", onError);
        reject(new Error(`[${this.label}] timeout after ${timeoutMs}ms waiting for ${label}`));
      }, timeoutMs);
      const onClose = () => {
        clearTimeout(timer);
        this.ws.removeListener("error", onError);
        resolve(this.closeInfo);
      };
      const onError = (err) => {
        clearTimeout(timer);
        this.ws.removeListener("close", onClose);
        resolve({ code: null, reason: `transport error: ${err && err.message ? err.message : String(err)}` });
      };
      this.ws.once("close", onClose);
      this.ws.once("error", onError);
    });
  }

  /** Builds and sends a well-formed envelope, filling version/sequence. */
  send(partial) {
    const full = {
      version: PROTOCOL_VERSION,
      type: partial.type,
      roomId: partial.roomId ?? "",
      senderId: partial.senderId ?? "",
      sequence: partial.sequence ?? this._nextSequence++,
      payload: partial.payload ?? {},
    };
    this.ws.send(JSON.stringify(full));
    return full;
  }

  /** Sends a raw pre-built object or string, bypassing envelope defaults —
   * for forged/malformed frames in the refusal matrix. */
  sendRaw(objectOrString) {
    const text =
      typeof objectOrString === "string"
        ? objectOrString
        : JSON.stringify(objectOrString);
    this.ws.send(text);
  }

  close(code = 1000, reason = "") {
    try {
      this.ws.close(code, reason);
    } catch {
      // best effort
    }
  }

  terminate() {
    try {
      this.ws.terminate();
    } catch {
      // best effort
    }
  }
}
