import { AsyncLocalStorage } from 'node:async_hooks';
import { createServer } from 'node:http';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { CONTRACT_VERSION, createWorker } from './worker.mjs';

const MAX_HEADER_BYTES = 16 * 1024;
const DEFAULT_HEADERS_TIMEOUT_MS = 10_000;
const DEFAULT_REQUEST_TIMEOUT_MS = 20_000;
const REQUEST_CONTEXT = new AsyncLocalStorage();
const HOP_BY_HOP_HEADERS = new Set([
  'connection', 'expect', 'host', 'keep-alive', 'proxy-connection',
  'te', 'trailer', 'transfer-encoding', 'upgrade'
]);

function contextualFetch(fetchImpl) {
  return (input, init = {}) => {
    const clientSignal = REQUEST_CONTEXT.getStore()?.signal;
    const signal = clientSignal
      ? (init.signal ? AbortSignal.any([init.signal, clientSignal]) : clientSignal)
      : init.signal;
    return fetchImpl(input, { ...init, ...(signal ? { signal } : {}) });
  };
}

function errorResponse(status, message) {
  return new Response(JSON.stringify({
    contractVersion: CONTRACT_VERSION,
    sourceId: 'nyaa',
    error: message
  }), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store'
    }
  });
}

function requestHeaders(incoming) {
  const headers = new Headers();
  for (const [name, value] of Object.entries(incoming.headers)) {
    if (HOP_BY_HOP_HEADERS.has(name)) continue;
    if (Array.isArray(value)) {
      for (const item of value) headers.append(name, item);
    } else if (value !== undefined) {
      headers.set(name, value);
    }
  }
  return headers;
}

function toFetchRequest(incoming, signal) {
  const method = incoming.method || 'GET';
  const hasBody = method !== 'GET' && method !== 'HEAD';
  const init = { method, headers: requestHeaders(incoming), signal };
  let bodyState;
  if (hasBody) {
    bodyState = { complete: false, cancelled: false };
    init.body = new ReadableStream({
      start(controller) {
        let finished = false;
        const cleanup = () => {
          incoming.off('data', onData);
          incoming.off('end', onEnd);
          incoming.off('error', onError);
        };
        const onData = chunk => {
          controller.enqueue(new Uint8Array(chunk));
          if (controller.desiredSize <= 0) incoming.pause();
        };
        const onEnd = () => {
          finished = true;
          bodyState.complete = true;
          cleanup();
          controller.close();
        };
        const onError = error => {
          finished = true;
          cleanup();
          controller.error(error);
        };
        incoming.on('data', onData);
        incoming.once('end', onEnd);
        incoming.once('error', onError);
        incoming.pause();
        bodyState.cancel = () => {
          if (finished) return;
          finished = true;
          bodyState.cancelled = true;
          cleanup();
          incoming.pause();
        };
      },
      pull() { incoming.resume(); },
      cancel() { bodyState.cancel?.(); }
    });
    init.duplex = 'half';
  }
  return { request: new Request(new URL(incoming.url || '/', 'http://nyaa-node.local'), init), bodyState };
}

function waitForDrain(response) {
  return new Promise(resolve => {
    const cleanup = () => {
      response.off('drain', onDrain);
      response.off('close', onClose);
      response.off('error', onError);
    };
    const finish = result => {
      cleanup();
      resolve(result);
    };
    const onDrain = () => finish(true);
    const onClose = () => finish(false);
    const onError = () => finish(false);
    response.once('drain', onDrain);
    response.once('close', onClose);
    response.once('error', onError);
  });
}

async function writeResponse(fetchResponse, outgoing, signal) {
  outgoing.statusCode = fetchResponse.status;
  fetchResponse.headers.forEach((value, name) => outgoing.setHeader(name, value));

  if (!fetchResponse.body) {
    outgoing.end();
    return;
  }

  const reader = fetchResponse.body.getReader();
  try {
    while (!signal.aborted && !outgoing.destroyed) {
      const { done, value } = await reader.read();
      if (done) break;
      if (!outgoing.write(value)) {
        if (!await waitForDrain(outgoing)) return;
      }
    }
    if (!signal.aborted && !outgoing.destroyed) outgoing.end();
  } finally {
    if (signal.aborted || outgoing.destroyed) await reader.cancel().catch(() => {});
    try { reader.releaseLock(); } catch (_) {}
  }
}

/** Create an HTTP adapter around the unchanged Worker handler. */
export function createNodeServer({
  fetchImpl = globalThis.fetch,
  timeoutMs,
  bodyTimeoutMs,
  headersTimeoutMs = DEFAULT_HEADERS_TIMEOUT_MS,
  requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS
} = {}) {
  if (typeof fetchImpl !== 'function') throw new TypeError('A fetch implementation is required');
  if (!Number.isFinite(headersTimeoutMs) || headersTimeoutMs <= 0)
    throw new TypeError('headersTimeoutMs must be positive');
  if (!Number.isFinite(requestTimeoutMs) || requestTimeoutMs <= 0)
    throw new TypeError('requestTimeoutMs must be positive');

  const worker = createWorker({ fetchImpl: contextualFetch(fetchImpl), timeoutMs, bodyTimeoutMs });
  const activeResponses = new Set();
  let draining = false;
  let closePromise;

  const server = createServer({ maxHeaderSize: MAX_HEADER_BYTES }, async (incoming, outgoing) => {
    activeResponses.add(outgoing);
    const controller = new AbortController();
    const abortOnRequestClose = () => {
      if (!incoming.complete) controller.abort(new Error('Client disconnected'));
    };
    const abortOnResponseClose = () => {
      if (!outgoing.writableEnded) controller.abort(new Error('Client disconnected'));
    };
    incoming.once('aborted', abortOnRequestClose);
    incoming.once('close', abortOnRequestClose);
    outgoing.once('close', abortOnResponseClose);

    let bodyState;
    try {
      if (draining) {
        await writeResponse(errorResponse(503, 'Service is shutting down'), outgoing, controller.signal);
        return;
      }
      const adapted = toFetchRequest(incoming, controller.signal);
      bodyState = adapted.bodyState;
      const response = await REQUEST_CONTEXT.run({ signal: controller.signal }, () => worker.fetch(adapted.request));
      await writeResponse(response, outgoing, controller.signal);
    } catch (_) {
      if (!controller.signal.aborted && !outgoing.destroyed) {
        await writeResponse(errorResponse(502, 'Nyaa request failed'), outgoing, controller.signal).catch(() => {});
      }
    } finally {
      activeResponses.delete(outgoing);
      incoming.off('aborted', abortOnRequestClose);
      incoming.off('close', abortOnRequestClose);
      outgoing.off('close', abortOnResponseClose);
      const requestHasBody = !['GET', 'HEAD'].includes(incoming.method || 'GET');
      if (requestHasBody && !bodyState?.complete && !outgoing.destroyed) {
        outgoing.shouldKeepAlive = false;
        const closeIncompleteRequest = () => incoming.socket?.destroy();
        if (outgoing.writableFinished) closeIncompleteRequest();
        else outgoing.once('finish', closeIncompleteRequest);
      }
    }
  });

  server.headersTimeout = headersTimeoutMs;
  server.requestTimeout = requestTimeoutMs;
  server.keepAliveTimeout = 5_000;
  server.closeGracefully = () => {
    if (closePromise) return closePromise;
    draining = true;
    for (const outgoing of activeResponses) {
      outgoing.shouldKeepAlive = false;
      if (outgoing.headersSent) outgoing.once('finish', () => outgoing.socket?.end());
    }
    closePromise = new Promise((resolve, reject) => {
      server.close(error => error ? reject(error) : resolve());
      server.closeIdleConnections?.();
    });
    return closePromise;
  };
  return server;
}

/** Start the adapter and return its address and graceful close function. */
export async function listenNodeServer({
  host = '0.0.0.0',
  port = process.env.PORT ? Number(process.env.PORT) : 3000,
  ...serverOptions
} = {}) {
  if (!Number.isInteger(port) || port < 0 || port > 65_535)
    throw new TypeError('port must be an integer between 0 and 65535');
  const server = createNodeServer(serverOptions);
  await new Promise((resolveListen, rejectListen) => {
    const onError = error => {
      server.off('listening', onListening);
      rejectListen(error);
    };
    const onListening = () => {
      server.off('error', onError);
      resolveListen();
    };
    server.once('error', onError);
    server.once('listening', onListening);
    server.listen(port, host);
  });
  return { server, address: server.address(), close: () => server.closeGracefully() };
}

async function main() {
  const { address, close } = await listenNodeServer();
  console.log(`Nyaa Node host listening on ${typeof address === 'object' && address ? address.address : address}:${typeof address === 'object' && address ? address.port : ''}`);

  let stopping = false;
  const stop = () => {
    if (stopping) return;
    stopping = true;
    close().then(() => process.exit(0), error => {
      console.error('Nyaa Node host shutdown failed:', error.message);
      process.exitCode = 1;
    });
  };
  process.once('SIGTERM', stop);
  process.once('SIGINT', stop);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main().catch(error => {
    console.error('Could not start Nyaa Node host:', error.message);
    process.exitCode = 1;
  });
}
