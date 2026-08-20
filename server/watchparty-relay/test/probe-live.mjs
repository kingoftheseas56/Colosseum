#!/usr/bin/env node
// Colosseum Watch Party relay — real-socket conformance probe (Slice 5 of
// docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md).
//
// Proves the relay over REAL WebSockets against a live `wrangler dev`
// instance the script itself spawns and tears down: the full
// multi-participant journey (create/join/timeline/control-mode/chat/
// reaction/kick+rejoin/participantState/host-grace/reconnect/grace-expiry+
// transfer) plus the refusal matrix (bad protocol header, dev-auth-unset
// signed-in connect, guest createRoom, forged senderId, oversize frame,
// rate burst), and a kill-mid-connection transport-failure check.
//
// This script is the SCRIPTED HOST instrument Slices 6-9 reuse — see
// `--host-only` mode below, which is the exact interface Slice 6's
// acceptance script drives.
//
// Every wait is message-or-timeout (test/lib/wp-client.mjs's waitFor/
// expectNone/waitForClose) or a bounded infra-readiness poll
// (test/lib/relay-process.mjs's TCP probe) — no sleeps standing in for a
// completion signal anywhere in this file.
//
// Usage:
//   npm run probe:live
//   node test/probe-live.mjs                      # same, full 12-step run
//   node test/probe-live.mjs --host-only \
//     --info-hash <40-or-64-hex-chars> --file-idx 0 \
//     [--url wss://localhost:8787] [--bearer dev-token-host]
//     # connects to an ALREADY-RUNNING relay, creates one room, prints
//     # ROOM_ID=<id> to stdout, and holds the process open until
//     # SIGINT/stdin close — the interface Slice 6's orchestrator drives.
//
// NODE_EXTRA_CA_CERTS: this script re-execs itself once with
// NODE_EXTRA_CA_CERTS pointed at test/dev-ca.pem when the caller hasn't
// already set it (mirrors test/probe-handshake.mjs's documented recipe,
// just without requiring the caller to remember the env var every time).

import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RELAY_ROOT = path.resolve(HERE, "..");
const DEFAULT_CA = path.join(RELAY_ROOT, "test", "dev-ca.pem");

if (!process.env.NODE_EXTRA_CA_CERTS && !process.env.WP_PROBE_REEXECED) {
  const child = spawnSync(
    process.execPath,
    [process.argv[1], ...process.argv.slice(2)],
    {
      stdio: "inherit",
      env: {
        ...process.env,
        NODE_EXTRA_CA_CERTS: DEFAULT_CA,
        WP_PROBE_REEXECED: "1",
      },
    }
  );
  process.exit(child.status === null ? 1 : child.status);
}

// NOTE: the entry-point call `await main()` is at the BOTTOM of this file,
// not here. A top-level `await` suspends the rest of THIS module's
// top-level execution until it settles — so an early call would run before
// the `const` declarations further down (TORRENT_SOURCE_*, HOST_BEARER,
// SIGNED_IN_BEARER) are initialized, throwing "Cannot access before
// initialization" the instant a step closure touches them. Ground-truthed
// the hard way on the first live run of this probe; keep the call last.

// ---------------------------------------------------------------------------

function parseArgs(argv) {
  const args = {
    hostOnly: false,
    infoHash: null,
    fileIdx: 0,
    url: "wss://localhost:8787",
    bearer: "dev-token-host",
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--host-only") args.hostOnly = true;
    else if (a === "--info-hash") args.infoHash = argv[++i];
    else if (a === "--file-idx") args.fileIdx = Number(argv[++i]);
    else if (a === "--url") args.url = argv[++i];
    else if (a === "--bearer") args.bearer = argv[++i];
  }
  return args;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.hostOnly) {
    await runHostOnly(args);
    return;
  }
  await runFullProbe();
}

// ---------------------------------------------------------------------------
// --host-only mode — the scripted-host interface Slice 6's acceptance
// script drives against an already-running relay.
// ---------------------------------------------------------------------------

async function runHostOnly(args) {
  const { connect } = await import("./lib/wp-client.mjs");

  if (!args.infoHash) {
    console.error("HOST_ONLY_FAIL missing --info-hash");
    process.exit(2);
  }

  const client = await connect(args.url, {
    bearer: args.bearer,
    protocolHeader: "3",
    label: "host-only",
  });
  if (client.refused) {
    console.error(`HOST_ONLY_FAIL upgrade refused status=${client.statusCode}`);
    process.exit(2);
  }

  client.send({
    type: "createRoom",
    payload: {
      source: { kind: "torrent", infoHash: args.infoHash, fileIdx: args.fileIdx },
    },
  });

  let established;
  try {
    established = await client.waitForType("sessionEstablished", { timeoutMs: 10_000 });
    await client.waitForType("roomSnapshot", { timeoutMs: 10_000 });
  } catch (err) {
    console.error(`HOST_ONLY_FAIL ${err.message}`);
    process.exit(2);
  }

  console.log(`ROOM_ID=${established.roomId}`);
  console.log(`PARTICIPANT_ID=${established.payload.participantId}`);
  console.log(`RECONNECT_TOKEN=${established.payload.reconnectToken}`);
  console.log("HOST_ONLY_READY");

  // Mutable across the CHAT/DROP/RECONNECT extensions below: DROP replaces
  // `client` with a terminated socket, RECONNECT replaces it with a fresh one
  // bound to the SAME participantId via the rotated reconnect token. Every
  // extension below reads/writes these two closures, never a stale capture.
  let activeClient = client;
  let reconnectToken = established.payload.reconnectToken;
  const participantId = established.payload.participantId;

  // Test-instrument-only extension (Slice 7): a passive roster mirror. This
  // listens on the raw ws directly (a SECOND "message" listener alongside
  // WpClient's own internal one — ws supports many listeners) so it never
  // competes with the single-slot waitFor() cursor the rest of this file
  // uses. Every roomSnapshot updates `latestRoster` regardless of whether
  // anything is currently awaiting one — this is how KICK_BY_NAME resolves a
  // guest's opaque participantId from the display name an orchestrator
  // actually knows.
  let latestRoster = [];
  function attachRosterMirror(c) {
    c.ws.on("message", (data) => {
      let msg;
      try {
        msg = JSON.parse(data.toString());
      } catch {
        return;
      }
      if (msg && msg.type === "roomSnapshot" && msg.payload && Array.isArray(msg.payload.participants)) {
        latestRoster = msg.payload.participants;
      }
    });
  }
  attachRosterMirror(client);

  let shuttingDown = false;
  const shutdown = () => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log("HOST_ONLY_CLOSING");
    try {
      activeClient.close(1000, "host-only-exit");
    } catch {
      // best effort — DROP may have already terminated the socket.
    }
    setTimeout(() => process.exit(0), 200);
  };
  // Test-instrument-only extension (Slice 6): a line "END" on stdin sends a
  // real endRoom command as the host before shutting down, so an orchestrator
  // can drive a clean roomEnded broadcast instead of only a host-drop/grace
  // path. This does not touch src/ — it is the probe script's own stdin
  // protocol, same file the plan names as the allowed test-instrument edit.
  const endRoom = () => {
    if (shuttingDown) return;
    console.log("HOST_ONLY_ENDING");
    activeClient.send({
      type: "endRoom",
      roomId: established.roomId,
      senderId: participantId,
      payload: {},
    });
    setTimeout(shutdown, 200);
  };
  // Test-instrument-only extension (Slice 7): a line "CHAT <text>" sends a
  // real chat message as the host — the rest of the line (after the first
  // space) is the message body verbatim, so it may itself contain spaces.
  const sendChat = (text) => {
    if (shuttingDown) return;
    console.log(`HOST_ONLY_CHAT_SENDING ${text}`);
    activeClient.send({
      type: "chat",
      roomId: established.roomId,
      senderId: participantId,
      payload: { message: text },
    });
  };
  // Test-instrument-only extension (Slice 7): "DROP" closes the host's
  // socket WITHOUT sending endRoom — this is the host-grace trigger. The
  // relay starts its RELAY_HOST_GRACE_MS clock; the room and this process
  // both stay alive so a later "RECONNECT" line can rejoin within the
  // window. terminate() (not close()) mirrors the probe's own step-9 grace
  // test — an abrupt drop, not a clean close handshake, is the honest
  // model of a host's connection actually dying.
  const dropHost = () => {
    if (shuttingDown) return;
    console.log("HOST_ONLY_DROPPING");
    activeClient.terminate();
  };
  // Test-instrument-only extension (Slice 7): "RECONNECT" opens a fresh
  // socket and sends reconnectRoom with the last-known reconnect token —
  // the same recipe as probe-live.mjs step 9's Hb reconnect. On success the
  // rotated token replaces reconnectToken and activeClient replaces client,
  // so a subsequent END/CHAT/DROP acts through the new connection.
  const reconnectHost = async () => {
    if (shuttingDown) return;
    console.log("HOST_ONLY_RECONNECTING");
    try {
      const { connect } = await import("./lib/wp-client.mjs");
      const fresh = await connect(args.url, {
        bearer: args.bearer,
        protocolHeader: "3",
        label: "host-only-reconnect",
      });
      if (fresh.refused) {
        console.error(`HOST_ONLY_RECONNECT_FAIL upgrade refused status=${fresh.statusCode}`);
        return;
      }
      fresh.send({
        type: "reconnectRoom",
        roomId: established.roomId,
        payload: { reconnectToken },
      });
      const reestablished = await fresh.waitForType("sessionEstablished", { timeoutMs: 10_000 });
      if (reestablished.payload.participantId !== participantId) {
        console.error(
          `HOST_ONLY_RECONNECT_FAIL participantId changed: expected ${participantId} got ${reestablished.payload.participantId}`
        );
        return;
      }
      reconnectToken = reestablished.payload.reconnectToken;
      activeClient = fresh;
      attachRosterMirror(fresh);
      console.log("HOST_ONLY_RECONNECTED");
    } catch (err) {
      console.error(`HOST_ONLY_RECONNECT_FAIL ${err && err.message ? err.message : String(err)}`);
    }
  };
  // Test-instrument-only extension (Slice 7): "KICK_BY_NAME <displayName>"
  // resolves the guest via `latestRoster` (never trusting an orchestrator-
  // guessed participantId) and sends the real removeParticipant command an
  // orchestrator cannot otherwise construct without first parsing relay
  // broadcast traffic itself.
  const kickByName = (displayName) => {
    if (shuttingDown) return;
    const target = latestRoster.find((p) => p.displayName === displayName && !p.host);
    if (!target) {
      console.error(`HOST_ONLY_KICK_FAIL no non-host participant named ${JSON.stringify(displayName)} in latest roster: ${JSON.stringify(latestRoster.map((p) => p.displayName))}`);
      return;
    }
    console.log(`HOST_ONLY_KICKING ${displayName} participantId=${target.participantId}`);
    activeClient.send({
      type: "removeParticipant",
      roomId: established.roomId,
      senderId: participantId,
      payload: { participantId: target.participantId },
    });
  };
  const readline = await import("node:readline");
  const rl = readline.createInterface({ input: process.stdin });
  rl.on("line", (line) => {
    const trimmed = line.trim();
    if (trimmed === "END") {
      endRoom();
    } else if (trimmed === "DROP") {
      dropHost();
    } else if (trimmed === "RECONNECT") {
      void reconnectHost();
    } else if (trimmed.startsWith("CHAT ")) {
      sendChat(trimmed.slice(5));
    } else if (trimmed.startsWith("KICK_BY_NAME ")) {
      kickByName(trimmed.slice(13));
    }
  });
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
  process.stdin.resume();
  process.stdin.on("end", shutdown);
  process.stdin.on("close", shutdown);
}

// ---------------------------------------------------------------------------
// Full 12-step probe
// ---------------------------------------------------------------------------

function assert(cond, msg) {
  if (!cond) throw new Error(`assertion failed: ${msg}`);
}

const TORRENT_SOURCE_A = { kind: "torrent", infoHash: "a".repeat(40), fileIdx: 0 };
const TORRENT_SOURCE_B = { kind: "torrent", infoHash: "b".repeat(40), fileIdx: 1 };
const TORRENT_SOURCE_C = { kind: "torrent", infoHash: "c".repeat(40), fileIdx: 0 };

const HOST_BEARER = "dev-token-host";
const SIGNED_IN_BEARER = "dev-token-guest-signed-in";

async function runFullProbe() {
  const { connect } = await import("./lib/wp-client.mjs");
  const { spawnRelay, killRelay } = await import("./lib/relay-process.mjs");

  const results = [];
  async function step(n, name, fn) {
    try {
      await fn();
      results.push({ n, name, ok: true });
      console.log(`STEP_OK ${n} ${name}`);
    } catch (err) {
      results.push({ n, name, ok: false, error: err && err.message ? err.message : String(err) });
      console.log(`STEP_FAIL ${n} ${name}: ${err && err.message ? err.message : String(err)}`);
    }
  }

  console.log("PROBE_LIVE starting relay (wrangler dev)...");
  const main = await spawnRelay({
    name: "main",
    port: 8797,
    inspectorPort: 9797,
    vars: { RELAY_DEV_AUTH: "1", RELAY_HOST_GRACE_MS: "2000" },
  });
  console.log(`PROBE_LIVE main relay ready pid=${main.pid} url=${main.url}`);

  // Cross-step shared state.
  let H, G1, G2, G2b, roomId, Hid, Htoken, G1id, G2id, G2bId;

  try {
  await step(1, "createRoom", async () => {
    H = await connect(main.url, { bearer: HOST_BEARER, label: "H" });
    H.send({ type: "createRoom", payload: { source: TORRENT_SOURCE_A } });
    const established = await H.waitForType("sessionEstablished", { label: "H sessionEstablished" });
    Hid = established.payload.participantId;
    Htoken = established.payload.reconnectToken;
    roomId = established.roomId;
    assert(roomId && roomId.length > 0, "roomId assigned");

    const snap = await H.waitForType("roomSnapshot", { label: "H roomSnapshot (create)" });
    assert(snap.payload.hostParticipantId === Hid, "host is the creator");
    assert(snap.payload.controlMode === "host", "controlMode starts host");
    assert(snap.payload.participants.length === 1, "solo roster after create");
    assert(snap.payload.participants[0].host === true, "creator row is host");
  });

  await step(2, "G1 joins guest, roster=2", async () => {
    G1 = await connect(main.url, { label: "G1" });
    G1.send({
      type: "joinRoom",
      roomId,
      payload: { identityKind: "guest", displayName: "Guest-One" },
    });
    const established = await G1.waitForType("sessionEstablished", { label: "G1 sessionEstablished" });
    G1id = established.payload.participantId;

    const hSnap = await H.waitForType("roomSnapshot", { label: "H roomSnapshot (G1 joined)" });
    assert(hSnap.payload.participants.length === 2, "H sees roster 2");

    const g1Snap = await G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (own join)" });
    assert(g1Snap.payload.participants.length === 2, "G1 sees roster 2");
  });

  await step(3, "G2 joins guest, roster=3", async () => {
    G2 = await connect(main.url, { label: "G2" });
    G2.send({
      type: "joinRoom",
      roomId,
      payload: { identityKind: "guest", displayName: "Guest-Two" },
    });
    const established = await G2.waitForType("sessionEstablished", { label: "G2 sessionEstablished" });
    G2id = established.payload.participantId;

    const hSnap = await H.waitForType("roomSnapshot", { label: "H roomSnapshot (G2 joined)" });
    const g1Snap = await G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (G2 joined)" });
    const g2Snap = await G2.waitForType("roomSnapshot", { label: "G2 roomSnapshot (own join)" });

    // NEGATIVE-CONTROL ANCHOR: this exact literal (3) is the assertion the
    // probe-self negative control flips to 4 and restores — see the Slice 5
    // report for the flip/run/revert transcript. Do not change this to a
    // computed value; the drill depends on it being a bare literal here.
    assert(hSnap.payload.participants.length === 3, "H sees roster 3");
    assert(g1Snap.payload.participants.length === 3, "G1 sees roster 3");
    assert(g2Snap.payload.participants.length === 3, "G2 sees roster 3");
  });

  await step(4, "host timelineCommand play; guest timelineCommand refused under host mode", async () => {
    H.send({ type: "timelineCommand", roomId, senderId: Hid, payload: { command: "play" } });
    const [hTs, g1Ts, g2Ts] = await Promise.all([
      H.waitForType("timelineState", { label: "H timelineState (play)" }),
      G1.waitForType("timelineState", { label: "G1 timelineState (play)" }),
      G2.waitForType("timelineState", { label: "G2 timelineState (play)" }),
    ]);
    for (const [who, ts] of [["H", hTs], ["G1", g1Ts], ["G2", g2Ts]]) {
      assert(ts.payload.playing === true, `${who} sees playing=true`);
      assert(ts.payload.revision === 1, `${who} sees revision 1`);
    }

    G1.send({ type: "timelineCommand", roomId, senderId: G1id, payload: { command: "pause" } });
    const refusal = await G1.waitForType("error", { label: "G1 refusal under host mode" });
    assert(refusal.payload.code === "not_authorized", "guest timelineCommand refused not_authorized");

    await Promise.all([
      H.expectNone((m) => m.type === "timelineState", { label: "no timelineState to H" }),
      G2.expectNone((m) => m.type === "timelineState", { label: "no timelineState to G2" }),
    ]);
  });

  await step(5, "setControlMode shared; guest timelineCommand now accepted", async () => {
    H.send({ type: "setControlMode", roomId, senderId: Hid, payload: { controlMode: "shared" } });
    const [hSnap, g1Snap, g2Snap] = await Promise.all([
      H.waitForType("roomSnapshot", { label: "H roomSnapshot (shared)" }),
      G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (shared)" }),
      G2.waitForType("roomSnapshot", { label: "G2 roomSnapshot (shared)" }),
    ]);
    for (const [who, s] of [["H", hSnap], ["G1", g1Snap], ["G2", g2Snap]]) {
      assert(s.payload.controlMode === "shared", `${who} sees controlMode shared`);
    }

    G1.send({ type: "timelineCommand", roomId, senderId: G1id, payload: { command: "seek", positionMs: 5000 } });
    const [hTs, g1Ts, g2Ts] = await Promise.all([
      H.waitForType("timelineState", { label: "H timelineState (seek)" }),
      G1.waitForType("timelineState", { label: "G1 timelineState (seek)" }),
      G2.waitForType("timelineState", { label: "G2 timelineState (seek)" }),
    ]);
    for (const [who, ts] of [["H", hTs], ["G1", g1Ts], ["G2", g2Ts]]) {
      assert(ts.payload.positionMs === 5000, `${who} sees positionMs 5000`);
      assert(ts.payload.revision === 2, `${who} sees revision 2 (monotonic)`);
    }
  });

  await step(6, "chat from G2 and reaction from G1, both stamped+broadcast", async () => {
    G2.send({ type: "chat", roomId, senderId: G2id, payload: { message: "hello from G2" } });
    const [hChat, g1Chat, g2Chat] = await Promise.all([
      H.waitForType("chat", { label: "H chat" }),
      G1.waitForType("chat", { label: "G1 chat" }),
      G2.waitForType("chat", { label: "G2 chat" }),
    ]);
    for (const [who, c] of [["H", hChat], ["G1", g1Chat], ["G2", g2Chat]]) {
      assert(c.payload.participantId === G2id, `${who} chat stamped with G2's participantId`);
      assert(c.payload.message === "hello from G2", `${who} chat message intact`);
    }

    G1.send({ type: "reaction", roomId, senderId: G1id, payload: { reaction: "cheer" } });
    const [hRx, g1Rx, g2Rx] = await Promise.all([
      H.waitForType("reaction", { label: "H reaction" }),
      G1.waitForType("reaction", { label: "G1 reaction" }),
      G2.waitForType("reaction", { label: "G2 reaction" }),
    ]);
    for (const [who, r] of [["H", hRx], ["G1", g1Rx], ["G2", g2Rx]]) {
      assert(r.payload.participantId === G1id, `${who} reaction stamped with G1's participantId`);
      assert(r.payload.reaction === "cheer", `${who} reaction text intact`);
    }
  });

  await step(7, "host removeParticipant G2 (kick); fresh rejoin accepted", async () => {
    H.send({ type: "removeParticipant", roomId, senderId: Hid, payload: { participantId: G2id } });

    const [removedError, closeInfo] = await Promise.all([
      G2.waitForType("error", { label: "G2 participant_removed" }),
      G2.waitForClose({ label: "G2 socket closed" }),
    ]);
    assert(removedError.payload.code === "participant_removed", "G2 gets typed participant_removed");
    assert(closeInfo !== undefined, "G2 socket actually closed");

    const [hSnap, g1Snap] = await Promise.all([
      H.waitForType("roomSnapshot", { label: "H roomSnapshot (G2 kicked)" }),
      G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (G2 kicked)" }),
    ]);
    assert(hSnap.payload.participants.length === 2, "H sees roster 2 after kick");
    assert(g1Snap.payload.participants.length === 2, "G1 sees roster 2 after kick");
    assert(!hSnap.payload.participants.some((p) => p.participantId === G2id), "kicked participantId gone from roster");

    G2b = await connect(main.url, { label: "G2b" });
    G2b.send({
      type: "joinRoom",
      roomId,
      payload: { identityKind: "guest", displayName: "Guest-Two-Again" },
    });
    const established = await G2b.waitForType("sessionEstablished", { label: "G2b sessionEstablished (fresh rejoin)" });
    G2bId = established.payload.participantId;
    assert(G2bId !== G2id, "kick-not-ban: fresh rejoin gets a new participantId");

    const [hSnap2, g1Snap2, g2bSnap] = await Promise.all([
      H.waitForType("roomSnapshot", { label: "H roomSnapshot (G2b rejoined)" }),
      G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (G2b rejoined)" }),
      G2b.waitForType("roomSnapshot", { label: "G2b roomSnapshot (own rejoin)" }),
    ]);
    for (const [who, s] of [["H", hSnap2], ["G1", g1Snap2], ["G2b", g2bSnap]]) {
      assert(s.payload.participants.length === 3, `${who} sees roster 3 after fresh rejoin`);
    }
  });

  await step(8, "participantState from G1 fans out to everyone", async () => {
    G1.send({ type: "participantState", roomId, senderId: G1id, payload: { ready: true, syncStatus: "inSync" } });
    const [hPs, g1Ps, g2bPs] = await Promise.all([
      H.waitForType("participantState", { label: "H participantState" }),
      G1.waitForType("participantState", { label: "G1 participantState" }),
      G2b.waitForType("participantState", { label: "G2b participantState" }),
    ]);
    for (const [who, p] of [["H", hPs], ["G1", g1Ps], ["G2b", g2bPs]]) {
      assert(p.payload.participantId === G1id, `${who} sees the update tagged with G1's participantId`);
      assert(p.payload.ready === true, `${who} sees ready=true`);
      assert(p.payload.syncStatus === "inSync", `${who} sees syncStatus inSync`);
    }
  });

  await step(9, "host socket drops (grace visible); host reconnects within grace", async () => {
    H.terminate();

    const [g1Snap, g2bSnap] = await Promise.all([
      G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (host grace started)" }),
      G2b.waitForType("roomSnapshot", { label: "G2b roomSnapshot (host grace started)" }),
    ]);
    for (const [who, s] of [["G1", g1Snap], ["G2b", g2bSnap]]) {
      assert(s.payload.hostReconnectDeadlineMs > 0, `${who} sees an active grace deadline`);
      const hostRow = s.payload.participants.find((p) => p.participantId === Hid);
      assert(hostRow !== undefined, `${who} still sees the host's roster row during grace`);
      assert(hostRow.connected === false, `${who} sees host row marked disconnected`);
    }

    const Hb = await connect(main.url, { bearer: HOST_BEARER, label: "Hb" });
    Hb.send({ type: "reconnectRoom", roomId, payload: { reconnectToken: Htoken } });
    const established = await Hb.waitForType("sessionEstablished", { label: "Hb sessionEstablished (reconnect)" });
    assert(established.payload.participantId === Hid, "reconnect restores the same participantId");
    assert(established.payload.reconnectToken !== Htoken, "reconnect token rotated");
    Htoken = established.payload.reconnectToken;

    const [g1Snap2, g2bSnap2, hbSnap] = await Promise.all([
      G1.waitForType("roomSnapshot", { label: "G1 roomSnapshot (host reconnected)" }),
      G2b.waitForType("roomSnapshot", { label: "G2b roomSnapshot (host reconnected)" }),
      Hb.waitForType("roomSnapshot", { label: "Hb roomSnapshot (own reconnect)" }),
    ]);
    for (const [who, s] of [["G1", g1Snap2], ["G2b", g2bSnap2], ["Hb", hbSnap]]) {
      assert(s.payload.hostReconnectDeadlineMs === -1, `${who} sees grace cleared`);
      const hostRow = s.payload.participants.find((p) => p.participantId === Hid);
      assert(hostRow.connected === true, `${who} sees host row connected again`);
    }

    H = Hb;
  });

  await step(10, "grace expiry ends a guest-only room; grace expiry transfers host to a signed-in participant", async () => {
    // Part A: H drops again; only guests remain -> no eligible successor ->
    // roomEnded to all + erasure.
    H.terminate();
    const [g1Ended, g2bEnded] = await Promise.all([
      G1.waitForType("roomEnded", { timeoutMs: 6000, label: "G1 roomEnded (grace expired, guests only)" }),
      G2b.waitForType("roomEnded", { timeoutMs: 6000, label: "G2b roomEnded (grace expired, guests only)" }),
    ]);
    assert(g1Ended.type === "roomEnded", "G1 got roomEnded");
    assert(g2bEnded.type === "roomEnded", "G2b got roomEnded");

    // Part B: a NEW room with a signed-in second participant (G3, the
    // second dev bearer) — host drops, grace expires, hostChanged to G3,
    // room lives.
    const H2 = await connect(main.url, { bearer: HOST_BEARER, label: "H2" });
    H2.send({ type: "createRoom", payload: { source: TORRENT_SOURCE_B } });
    const h2Established = await H2.waitForType("sessionEstablished", { label: "H2 sessionEstablished" });
    const roomId2 = h2Established.roomId;
    const H2id = h2Established.payload.participantId;
    await H2.waitForType("roomSnapshot", { label: "H2 roomSnapshot (create)" });

    const G3 = await connect(main.url, { bearer: SIGNED_IN_BEARER, label: "G3" });
    G3.send({ type: "joinRoom", roomId: roomId2, payload: { identityKind: "signedIn" } });
    const g3Established = await G3.waitForType("sessionEstablished", { label: "G3 sessionEstablished" });
    const G3id = g3Established.payload.participantId;

    await Promise.all([
      H2.waitForType("roomSnapshot", { label: "H2 roomSnapshot (G3 joined)" }),
      G3.waitForType("roomSnapshot", { label: "G3 roomSnapshot (own join)" }),
    ]);

    H2.terminate();
    const hostChanged = await G3.waitForType("hostChanged", { timeoutMs: 6000, label: "G3 hostChanged (grace expired)" });
    assert(hostChanged.payload.hostParticipantId === G3id, "host transferred to the earliest-joined signed-in participant");

    const g3Snap = await G3.waitForType("roomSnapshot", { label: "G3 roomSnapshot (post-transfer)" });
    assert(g3Snap.payload.hostParticipantId === G3id, "snapshot confirms new host");
    assert(g3Snap.payload.hostReconnectDeadlineMs === -1, "grace cleared after transfer");
    assert(!g3Snap.payload.participants.some((p) => p.participantId === H2id), "deposed host erased from roster");

    // Cleanup: G3 is host now, end the room cleanly.
    G3.send({ type: "endRoom", roomId: roomId2, senderId: G3id, payload: {} });
    await G3.waitForType("roomEnded", { label: "G3 roomEnded (cleanup)" });
  });

  await step(11, "refusal matrix", async () => {
    const failures = [];
    async function refusal(name, fn) {
      try {
        await fn();
        console.log(`  REFUSAL_OK ${name}`);
      } catch (err) {
        failures.push(name);
        console.log(`  REFUSAL_FAIL ${name}: ${err && err.message ? err.message : String(err)}`);
      }
    }

    await refusal("protocol header 2 refused at upgrade (426)", async () => {
      const result = await connect(main.url, { protocolHeader: "2", label: "bad-header" });
      assert(result.refused === true, "upgrade must be refused, not accepted");
      assert(result.statusCode === 426, `expected HTTP 426, got ${result.statusCode}`);
    });

    await refusal("signed-in connect refused when RELAY_DEV_AUTH is unset", async () => {
      const noauth = await spawnRelay({
        name: "noauth",
        port: 8798,
        inspectorPort: 9798,
        vars: {},
      });
      try {
        const client = await connect(noauth.url, { bearer: HOST_BEARER, label: "noauth-signed-in" });
        assert(client.refused !== true, "the WebSocket upgrade itself succeeds (auth is checked after)");
        client.send({ type: "createRoom", payload: { source: TORRENT_SOURCE_C } });
        const error = await client.waitForType("error", { label: "unauthenticated refusal (dev auth off)" });
        assert(error.payload.code === "unauthenticated", `expected unauthenticated, got ${error.payload.code}`);
        client.close();
      } finally {
        await killRelay(noauth);
      }
    });

    await refusal("guest createRoom refused", async () => {
      const client = await connect(main.url, { label: "guest-create" });
      client.send({ type: "createRoom", payload: { source: TORRENT_SOURCE_C } });
      const error = await client.waitForType("error", { label: "guest createRoom refusal" });
      assert(error.payload.code === "unauthenticated", `expected unauthenticated, got ${error.payload.code}`);
      client.close();
    });

    await refusal("forged senderId refused", async () => {
      const forgeHost = await connect(main.url, { bearer: HOST_BEARER, label: "forge-host" });
      forgeHost.send({ type: "createRoom", payload: { source: TORRENT_SOURCE_C } });
      const hostEstablished = await forgeHost.waitForType("sessionEstablished", { label: "forge-host sessionEstablished" });
      const forgeRoomId = hostEstablished.roomId;

      const forgeGuest = await connect(main.url, { label: "forge-guest" });
      forgeGuest.send({ type: "joinRoom", roomId: forgeRoomId, payload: { identityKind: "guest", displayName: "Forger" } });
      const guestEstablished = await forgeGuest.waitForType("sessionEstablished", { label: "forge-guest sessionEstablished" });
      await forgeHost.waitForType("roomSnapshot", { label: "forge-host roomSnapshot (guest joined)" });
      await forgeGuest.waitForType("roomSnapshot", { label: "forge-guest roomSnapshot (own join)" });

      forgeGuest.send({
        type: "timelineCommand",
        roomId: forgeRoomId,
        senderId: "p_not-actually-my-participant-id",
        payload: { command: "play" },
      });
      const error = await forgeGuest.waitForType("error", { label: "forged senderId refusal" });
      assert(error.payload.code === "not_authorized", `expected not_authorized, got ${error.payload.code}`);

      forgeHost.send({ type: "endRoom", roomId: forgeRoomId, senderId: hostEstablished.payload.participantId, payload: {} });
      await forgeHost.waitForType("roomEnded", { label: "forge-host roomEnded (cleanup)" });
      void guestEstablished;
    });

    await refusal("oversize frame (>64KiB) refused", async () => {
      const client = await connect(main.url, { label: "oversize" });
      const filler = "A".repeat(70_000);
      const oversized = JSON.stringify({
        version: 3,
        type: "chat",
        roomId: "",
        senderId: "",
        sequence: 1,
        payload: { message: filler },
      });
      assert(Buffer.byteLength(oversized, "utf8") > 65536, "constructed frame is actually over the 64 KiB ceiling");
      client.sendRaw(oversized);
      const error = await client.waitForType("error", { label: "oversize refusal" });
      assert(error.payload.code === "invalid_message", `expected invalid_message, got ${error.payload.code}`);
      client.close();
    });

    await refusal("rate burst (>120 msgs/10s) refused", async () => {
      const client = await connect(main.url, { label: "rate-burst" });
      for (let i = 0; i < 130; i++) {
        client.sendRaw(JSON.stringify({ version: 3, type: "leaveRoom", roomId: "", senderId: "", sequence: i, payload: {} }));
      }
      // The first ~120 malformed-envelope frames each earn their own
      // invalid_message error before the ceiling trips, so scan for the
      // specific rate_limited code rather than the first error frame.
      const limited = await client.waitFor(
        (m) => m.type === "error" && m.payload && m.payload.code === "rate_limited",
        { timeoutMs: 8000, label: "rate_limited observed" }
      );
      assert(limited.payload.code === "rate_limited", `expected rate_limited eventually, got ${limited.payload.code}`);
      client.close();
    });

    if (failures.length > 0) {
      throw new Error(`refusal matrix failures: ${failures.join(", ")}`);
    }
  });

  await step(12, "kill relay mid-connection reports transport failure cleanly (no hang)", async () => {
    const client = await connect(main.url, { label: "kill-target" });
    await killRelay(main);
    const closeInfo = await client.waitForClose({ timeoutMs: 8000, label: "transport failure after relay kill" });
    assert(closeInfo !== undefined, "waitForClose resolved (did not hang) after the relay process was killed");
  });

  } finally {
    // Step 12 already kills `main` on the happy path; killRelay() is a
    // no-op against an already-exited process, so this unconditionally
    // guarantees no wrangler/workerd process survives the run even if an
    // earlier step threw somewhere killRelay(main) hadn't run yet.
    await killRelay(main);
  }

  const passCount = results.filter((r) => r.ok).length;
  console.log(`PROBE_LIVE_OK ${passCount}/${results.length}`);
  if (passCount !== results.length) {
    process.exitCode = 1;
  }
}

// Entry point — deliberately last; see the top-level-await ordering note
// near the top of this file.
await main();
