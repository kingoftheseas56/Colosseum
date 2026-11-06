"use strict";
globalThis.global = globalThis;
globalThis.module = {exports: {}};
globalThis.exports = module.exports;
globalThis.TMDB_API_KEY = "__COLOSSEUM_TMDB_KEY__";
globalThis.TMDB_ACCESS_TOKEN = TMDB_API_KEY;
globalThis.process = {env: {TMDB_API_KEY, TMDB_ACCESS_TOKEN}};
globalThis.SCRAPER_SETTINGS = {};

globalThis.TextEncoder = class TextEncoder {
  encode(value = "") {
    return Uint8Array.from(unescape(encodeURIComponent(String(value))), ch => ch.charCodeAt(0));
  }
};
globalThis.TextDecoder = class TextDecoder {
  decode(value = new Uint8Array()) {
    const bytes = ArrayBuffer.isView(value)
      ? new Uint8Array(value.buffer, value.byteOffset, value.byteLength) : new Uint8Array(value);
    return __decodeUtf8(bytes);
  }
};
globalThis.atob = value => Buffer.from(String(value), "base64").toString("latin1");
globalThis.btoa = value => Buffer.from(String(value), "latin1").toString("base64");
globalThis.crypto = {
  getRandomValues(array) {
    if (!ArrayBuffer.isView(array) || array.byteLength > 65536) throw new TypeError("invalid random buffer");
    const bytes = new Uint8Array(array.buffer, array.byteOffset, array.byteLength);
    bytes.set(Buffer.from(__random(bytes.length), "base64"));
    return array;
  }
};

globalThis.require = name => {
  if (name === "ws") return globalThis.WebSocket;
  const modules = globalThis.__vendorModules;
  if (modules && Object.prototype.hasOwnProperty.call(modules, name)) return modules[name]();
  throw new TypeError("require blocked");
};

globalThis.Headers = class Headers {
  constructor(source = {}) {
    this.values = Object.create(null);
    const pairs = source instanceof Headers ? source.entries()
      : Array.isArray(source) ? source : Object.entries(source);
    for (const [key, value] of pairs) this.append(key, value);
  }
  append(key, value) {
    const previous = this.get(key);
    this.set(key, previous === null ? value : previous + ", " + value);
  }
  set(key, value) { this.values[String(key).toLowerCase()] = String(value).trim(); }
  get(key) { return this.values[String(key).toLowerCase()] ?? null; }
  has(key) { return this.get(key) !== null; }
  delete(key) { delete this.values[String(key).toLowerCase()]; }
  entries() { return Object.entries(this.values)[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
  forEach(fn, self) { for (const [key, value] of this) fn.call(self, value, key, this); }
};

globalThis.Response = class Response {
  constructor(body = null, init = {}) {
    this.bytes = body instanceof Uint8Array ? body
      : body instanceof ArrayBuffer ? new Uint8Array(body)
      : new TextEncoder().encode(body ?? "");
    this.status = init.status ?? 200;
    this.statusText = init.statusText ?? "";
    this.headers = new Headers(init.headers);
    this.url = init.url ?? "";
    this.redirected = init.redirected ?? false;
    this.type = "basic";
    this.bodyUsed = false;
  }
  get ok() { return this.status >= 200 && this.status < 300; }
  get body() {
    const self = this;
    let consumed = false;
    return {
      getReader() {
        return {
          async read() {
            if (consumed) return { done: true };
            consumed = true;
            return { done: false, value: new Uint8Array(await self.arrayBuffer()) };
          },
          async cancel() { consumed = true; },
          releaseLock() {}
        };
      },
      async cancel() { consumed = true; }
    };
  }
  async arrayBuffer() {
    if (this.bodyUsed) throw new TypeError("response body already consumed");
    this.bodyUsed = true;
    return Uint8Array.from(this.bytes).buffer;
  }
  async text() { return new TextDecoder().decode(await this.arrayBuffer()); }
  async json() { return JSON.parse(await this.text()); }
  async blob() {
    const bytes = await this.arrayBuffer();
    return { size: bytes.byteLength, type: this.headers.get("content-type") || "",
      arrayBuffer: async () => bytes, text: async () => new TextDecoder().decode(bytes) };
  }
  clone() {
    if (this.bodyUsed) throw new TypeError("response body already consumed");
    return new Response(this.bytes, this);
  }
};

globalThis.AbortController = class AbortController {
  constructor() { this.signal = new AbortSignal(); }
  abort(reason = new Error("AbortError")) { this.signal.abort(reason); }
};
globalThis.AbortSignal = class AbortSignal {
  constructor() { this.aborted = false; this.listeners = new Set(); }
  addEventListener(name, callback) { if (name === "abort") this.listeners.add(callback); }
  removeEventListener(name, callback) { if (name === "abort") this.listeners.delete(callback); }
  throwIfAborted() { if (this.aborted) throw this.reason; }
  abort(reason) {
    if (this.aborted) return;
    this.aborted = true;
    this.reason = reason;
    const event = {type: "abort", target: this};
    for (const listener of this.listeners) listener(event);
    if (this.onabort) this.onabort(event);
    this.listeners.clear();
  }
  static timeout(ms) {
    const controller = new AbortController();
    setTimeout(() => controller.abort(new Error("TimeoutError")), ms);
    return controller.signal;
  }
};

{
  const pending = new Map();
  const timers = new Map();
  globalThis.fetch = async (input, options = {}) => {
    const {signal} = options;
    if (signal?.aborted) throw signal.reason;
    const headers = new Headers(options.headers);
    const body = options.body instanceof URLSearchParams
      ? String(options.body) : options.body;
    if (options.body instanceof URLSearchParams && !headers.has("content-type"))
      headers.set("content-type", "application/x-www-form-urlencoded;charset=UTF-8");
    if (body != null && typeof body !== "string") {
      throw new TypeError("unsupported fetch body");
    }
    return new Promise((resolve, reject) => {
      const id = __request(String(input), JSON.stringify({
        method: String(options.method || "GET"), headers: Object.fromEntries(headers),
        body: body ?? "", redirect: options.redirect || "follow"
      }));
      const abort = () => {
        if (!pending.delete(id)) return;
        __cancelRequest(id);
        reject(signal.reason);
      };
      pending.set(id, (packet, error) => {
        signal?.removeEventListener?.("abort", abort);
        if (error) reject(new TypeError(error));
        else resolve(new Response(Buffer.from(packet.body, "base64"), packet));
      });
      signal?.addEventListener?.("abort", abort);
    });
  };
  globalThis.__deliver = (id, packet, error) => {
    const callback = pending.get(id);
    if (callback) {
      pending.delete(id);
      callback(packet, error);
    }
  };
  function schedule(callback, delay, repeat, args) {
    if (typeof callback !== "function") throw new TypeError("timer requires a callback");
    const id = __startTimer(Math.max(1, Math.min(2147483647, Number(delay) || 1)), repeat);
    timers.set(id, {callback, args, repeat});
    return id;
  }
  globalThis.setTimeout = (fn, ms, ...args) => schedule(fn, ms, false, args);
  globalThis.setInterval = (fn, ms, ...args) => schedule(fn, ms, true, args);
  globalThis.clearTimeout = globalThis.clearInterval = id => {
    timers.delete(id);
    __clearTimer(id);
  };
  globalThis.__fireTimer = id => {
    const timer = timers.get(id);
    if (!timer) return;
    if (!timer.repeat) timers.delete(id);
    timer.callback(...timer.args);
  };
}

{
  const data = new Map();
  globalThis.localStorage = {
    get length() { return data.size; },
    getItem: key => data.get(String(key)) ?? null,
    setItem(key, value) {
      if (data.size < 256 || data.has(String(key))) data.set(String(key), String(value));
    },
    removeItem: key => data.delete(String(key)),
    key: index => [...data.keys()][index] ?? null,
    clear: () => data.clear()
  };
}


{
  const sockets = new Map();
  globalThis.WebSocket = class WebSocket {
    constructor(url) {
      this.url = String(url);
      this.readyState = 0;
      this.id = __openSocket(this.url);
      sockets.set(this.id, this);
    }
    send(data) {
      if (this.readyState !== 1 || typeof data !== "string") throw new TypeError("invalid WebSocket send");
      __sendSocket(this.id, data);
    }
    close() {
      __closeSocket(this.id);
      sockets.delete(this.id);
      this.readyState = 3;
    }
  };
  globalThis.__socketEvent = (id, type, data) => {
    const socket = sockets.get(id);
    if (!socket) return;
    if (type === "open") socket.readyState = 1;
    if (type === "close") {
      socket.readyState = 3;
      sockets.delete(id);
    }
    socket["on" + type]?.({type, data, target: socket});
  };
}
