// Colosseum Watch Party relay — `wrangler dev` process management for the
// live-socket probe (Slice 5). Spawns a local relay instance with a chosen
// port/env override set, waits for it to accept TLS connections (bounded
// polling, not a fixed sleep — the earliest successful probe connection
// wins), and kills it by exact PID at teardown (Windows: `taskkill /T /F`
// against the shell PID, matching the PID-tree kill already evidenced for
// `wrangler dev` in this relay's Slice 1-4 reports — the shell parent, its
// `node.exe` children, and their `workerd.exe` children).

import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RELAY_ROOT = path.resolve(HERE, "..", "..");
const WRANGLER_BIN =
  process.platform === "win32"
    ? path.join(RELAY_ROOT, "node_modules", ".bin", "wrangler.cmd")
    : path.join(RELAY_ROOT, "node_modules", ".bin", "wrangler");

/**
 * @param {object} opts
 * @param {string} opts.name label for logs
 * @param {number} opts.port
 * @param {number} opts.inspectorPort
 * @param {Record<string,string>} [opts.vars] --var overrides (key:value pairs)
 * @returns {Promise<{name:string, port:number, url:string, pid:number, proc: import('node:child_process').ChildProcess, log: string[], kill: () => Promise<void>}>}
 */
export async function spawnRelay({ name, port, inspectorPort, vars = {} }) {
  const args = [
    "dev",
    "--local-protocol",
    "https",
    "--port",
    String(port),
    "--inspector-port",
    String(inspectorPort),
  ];
  for (const [key, value] of Object.entries(vars)) {
    args.push("--var", `${key}:${value}`);
  }

  const proc = spawn(WRANGLER_BIN, args, {
    cwd: RELAY_ROOT,
    shell: true,
    stdio: ["ignore", "pipe", "pipe"],
    env: process.env,
  });

  const log = [];
  proc.stdout.on("data", (d) => log.push(String(d)));
  proc.stderr.on("data", (d) => log.push(String(d)));

  const handle = {
    name,
    port,
    url: `wss://localhost:${port}`,
    pid: proc.pid,
    proc,
    log,
    kill: () => killRelay(handle),
  };

  await waitForRelayReady(handle);
  return handle;
}

/**
 * Bounded polling for the relay to accept TCP connections on its port —
 * this is infrastructure readiness (the subprocess booting), not a
 * protocol-layer wait, so it has no message channel to wait on; the loop is
 * a message-or-timeout wait against the TCP handshake itself, capped and
 * never a blind fixed sleep.
 */
async function waitForRelayReady(handle, timeoutMs = 30_000, intervalMs = 250) {
  const deadline = Date.now() + timeoutMs;
  let lastErr = null;
  while (Date.now() < deadline) {
    if (handle.proc.exitCode !== null) {
      throw new Error(
        `[${handle.name}] wrangler dev exited early (code ${handle.proc.exitCode}) before becoming ready:\n${handle.log.join("")}`
      );
    }
    try {
      await tcpProbe(handle.port, 1000);
      return;
    } catch (err) {
      lastErr = err;
      await delay(intervalMs);
    }
  }
  throw new Error(
    `[${handle.name}] relay not ready on port ${handle.port} after ${timeoutMs}ms (last error: ${lastErr}):\n${handle.log.join("")}`
  );
}

function tcpProbe(port, timeoutMs) {
  return new Promise((resolve, reject) => {
    const socket = net.connect({ host: "127.0.0.1", port }, () => {
      socket.end();
      resolve();
    });
    socket.setTimeout(timeoutMs);
    socket.on("timeout", () => {
      socket.destroy();
      reject(new Error("tcp probe timeout"));
    });
    socket.on("error", (err) => {
      reject(err);
    });
  });
}

export async function killRelay(handle) {
  if (!handle || handle.proc.exitCode !== null) return;
  const pid = handle.pid;
  if (process.platform === "win32") {
    await new Promise((resolve) => {
      const killer = spawn("taskkill", ["/PID", String(pid), "/T", "/F"], {
        shell: true,
        stdio: "ignore",
      });
      killer.on("exit", () => resolve());
      killer.on("error", () => resolve());
    });
  } else {
    try {
      process.kill(-pid, "SIGKILL");
    } catch {
      try {
        handle.proc.kill("SIGKILL");
      } catch {
        // already gone
      }
    }
  }
  // Give the OS a bounded moment to reap; not relied on for correctness,
  // only so the caller's next `tasklist`/log inspection sees a clean state.
  await delay(500);
}
