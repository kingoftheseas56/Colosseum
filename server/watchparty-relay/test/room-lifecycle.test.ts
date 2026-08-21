// Room lifecycle suite for src/room-do.ts (Slice 3 of docs/superpowers/
// plans/2026-08-20-watch-party-relay-plan.md): create/join/reconnect/
// leave/end, identity binding, capacity.
//
// Drives the relay exactly as a real client would: SELF.fetch() with a
// WebSocket Upgrade request (real WebSocketPair through workerd, not a
// mocked DO method call), matching the vitest-pool-workers WebSocket
// testing pattern already established by test/protocol.test.ts's use of
// the same protocol.ts module this suite reuses for envelope building.

import { env, SELF } from "cloudflare:test";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  parseMessage,
  serializeMessage,
  type ErrorCode,
  type MessageType,
  type ProtocolMessage,
} from "../src/protocol";

const RELAY_URL = "https://relay.test/";
const PROTOCOL_HEADER = "X-Colosseum-Watch-Party-Protocol";

// Matches wrangler.toml's [vars] RELAY_DEV_BEARERS test map.
const HOST_BEARER = "dev-token-host";
const SECOND_SIGNED_IN_BEARER = "dev-token-guest-signed-in";

function setDevAuth(on: boolean): void {
  const relayEnv = env as unknown as Record<string, string>;
  relayEnv.RELAY_DEV_AUTH = on ? "1" : "0";
  relayEnv.RELAY_DEV_BEARERS = on
    ? JSON.stringify({
        [HOST_BEARER]: "SignedInHost",
        [SECOND_SIGNED_IN_BEARER]: "SignedInGuest",
      })
    : "";
}

beforeEach(() => {
  // wrangler.toml ships RELAY_DEV_AUTH="0" (fail-closed default); most
  // lifecycle cases need a signed-in host, so flip it on per test and
  // restore afterward. The one test that specifically covers the
  // unconfigured-validator refusal turns it back off itself before
  // connecting.
  setDevAuth(true);
});

afterEach(() => {
  setDevAuth(false);
});

interface TestSocket {
  ws: WebSocket;
  nextSequence: number;
  waitForNext(): Promise<ProtocolMessage>;
  close(): void;
}

async function connect(bearer?: string): Promise<TestSocket> {
  const headers: Record<string, string> = {
    Upgrade: "websocket",
    [PROTOCOL_HEADER]: "3",
  };
  if (bearer) headers["Authorization"] = `Bearer ${bearer}`;

  const response = await SELF.fetch(RELAY_URL, { headers });
  if (response.status !== 101 || !response.webSocket) {
    throw new Error(
      `expected websocket upgrade, got status=${response.status}`
    );
  }

  const ws = response.webSocket;
  ws.accept();

  const queue: ProtocolMessage[] = [];
  const waiters: Array<(m: ProtocolMessage) => void> = [];

  ws.addEventListener("message", (event: MessageEvent) => {
    const raw =
      typeof event.data === "string"
        ? event.data
        : new TextDecoder().decode(event.data as ArrayBuffer);
    const parsed = parseMessage(raw, "serverToClient");
    if (!parsed.ok) {
      throw new Error(
        `relay emitted a non-conformant server message: ${parsed.error}`
      );
    }
    if (waiters.length > 0) {
      waiters.shift()!(parsed.message);
    } else {
      queue.push(parsed.message);
    }
  });

  return {
    ws,
    nextSequence: 1,
    waitForNext(): Promise<ProtocolMessage> {
      return new Promise((resolve) => {
        if (queue.length > 0) {
          resolve(queue.shift()!);
          return;
        }
        waiters.push(resolve);
      });
    },
    close(): void {
      ws.close(1000, "test-cleanup");
    },
  };
}

function send(
  socket: TestSocket,
  type: MessageType,
  roomId: string,
  senderId: string,
  payload: Record<string, unknown>
): void {
  const message: ProtocolMessage = {
    version: 3,
    type,
    roomId,
    senderId,
    sequence: socket.nextSequence++,
    payload,
  };
  socket.ws.send(serializeMessage(message));
}

const TORRENT_SOURCE = {
  kind: "torrent",
  infoHash: "a".repeat(40),
  fileIdx: 0,
};

async function createRoomAsHost(bearer = HOST_BEARER) {
  const host = await connect(bearer);
  send(host, "createRoom", "", "", { source: TORRENT_SOURCE });
  const established = await host.waitForNext();
  const snapshot = await host.waitForNext();
  expect(established.type).toBe("sessionEstablished");
  expect(snapshot.type).toBe("roomSnapshot");
  const roomId = established.roomId;
  const participantId = (
    established.payload as { participantId: string }
  ).participantId;
  const reconnectToken = (
    established.payload as { reconnectToken: string }
  ).reconnectToken;
  return { host, roomId, participantId, reconnectToken };
}

async function joinAsGuest(roomId: string, displayName: string) {
  const guest = await connect();
  send(guest, "joinRoom", roomId, "", {
    identityKind: "guest",
    displayName,
  });
  const established = await guest.waitForNext();
  expect(established.type).toBe("sessionEstablished");
  const participantId = (
    established.payload as { participantId: string }
  ).participantId;
  const reconnectToken = (
    established.payload as { reconnectToken: string }
  ).reconnectToken;
  // broadcastRoomSnapshot() fans the post-join snapshot out to every
  // connected participant, including the one who just joined — consume
  // the guest's own copy here so callers see a clean queue.
  const ownSnapshot = await guest.waitForNext();
  expect(ownSnapshot.type).toBe("roomSnapshot");
  return { guest, participantId, reconnectToken };
}

function expectError(message: ProtocolMessage, code: ErrorCode): void {
  expect(message.type).toBe("error");
  expect((message.payload as { code: string }).code).toBe(code);
}

describe("createRoom", () => {
  it("signed-in connection creates a room and receives sessionEstablished then roomSnapshot", async () => {
    const { roomId, participantId } = await createRoomAsHost();
    expect(roomId).toMatch(/^WP-/);
    expect(participantId).toMatch(/^p_/);
  });

  it("guest createRoom is refused unauthenticated", async () => {
    const guest = await connect(); // no bearer at all
    send(guest, "createRoom", "", "", { source: TORRENT_SOURCE });
    const reply = await guest.waitForNext();
    expectError(reply, "unauthenticated");
  });

  it("signed-in connect with validator unconfigured is refused unauthenticated", async () => {
    setDevAuth(false); // simulate the deployed-default: no validator configured
    const wouldBeHost = await connect(HOST_BEARER); // bearer present but nothing can validate it
    send(wouldBeHost, "createRoom", "", "", { source: TORRENT_SOURCE });
    const reply = await wouldBeHost.waitForNext();
    expectError(reply, "unauthenticated");
  });
});

describe("joinRoom", () => {
  it("guest joins an existing room and both host and guest see an updated roomSnapshot", async () => {
    const { host, roomId } = await createRoomAsHost();
    const { participantId: guestId } = await joinAsGuest(roomId, "Guest One");

    const hostSnapshot = await host.waitForNext();
    expect(hostSnapshot.type).toBe("roomSnapshot");
    const participants = (
      hostSnapshot.payload as { participants: Array<{ participantId: string; host: boolean; identityKind: string }> }
    ).participants;
    expect(participants).toHaveLength(2);
    const guestRow = participants.find((p) => p.participantId === guestId)!;
    expect(guestRow.host).toBe(false);
    expect(guestRow.identityKind).toBe("guest");
  });

  it("signed-in joinRoom requires an authenticated identity", async () => {
    const { roomId } = await createRoomAsHost();
    const unauthed = await connect(); // no bearer
    send(unauthed, "joinRoom", roomId, "", { identityKind: "signedIn" });
    const reply = await unauthed.waitForNext();
    expectError(reply, "unauthenticated");
  });

  it("joinRoom against an unknown room is refused room_not_found", async () => {
    const guest = await connect();
    send(guest, "joinRoom", "WP-NOPE-0000", "", {
      identityKind: "guest",
      displayName: "Nobody",
    });
    const reply = await guest.waitForNext();
    expectError(reply, "room_not_found");
  });

  it("13th join attempt (capacity 12) is refused room_full, typed", async () => {
    const { roomId } = await createRoomAsHost();
    // Host is participant 1; 11 more guests reach the 12-participant cap.
    for (let i = 0; i < 11; i++) {
      await joinAsGuest(roomId, `Guest ${i}`);
    }
    const overflow = await connect();
    send(overflow, "joinRoom", roomId, "", {
      identityKind: "guest",
      displayName: "Guest 12 (overflow)",
    });
    const reply = await overflow.waitForNext();
    expectError(reply, "room_full");
  });
});

describe("identity binding — senderId is never trusted", () => {
  it("a forged senderId on leaveRoom is refused not_authorized and membership is unchanged", async () => {
    const { host, roomId } = await createRoomAsHost();
    const { guest, participantId: guestId } = await joinAsGuest(
      roomId,
      "Guest One"
    );
    await host.waitForNext(); // roomSnapshot after guest joined

    send(guest, "leaveRoom", roomId, "not-my-participant-id", {});
    const reply = await guest.waitForNext();
    expectError(reply, "not_authorized");

    // Prove membership is genuinely unchanged: a second, correctly-bound
    // leaveRoom from the same connection now succeeds.
    send(guest, "leaveRoom", roomId, guestId, {});
    const hostSnapshotAfterRealLeave = await host.waitForNext();
    expect(hostSnapshotAfterRealLeave.type).toBe("roomSnapshot");
    const remaining = (
      hostSnapshotAfterRealLeave.payload as {
        participants: Array<{ participantId: string }>;
      }
    ).participants;
    expect(remaining.some((p) => p.participantId === guestId)).toBe(false);
  });
});

describe("reconnectRoom", () => {
  it("valid token restores the same participant identity and rotates the token", async () => {
    const { roomId, participantId, reconnectToken } = await createRoomAsHost();

    const reconnected = await connect();
    send(reconnected, "reconnectRoom", roomId, "", { reconnectToken });
    const established = await reconnected.waitForNext();
    expect(established.type).toBe("sessionEstablished");
    expect(established.roomId).toBe(roomId);
    const restoredId = (
      established.payload as { participantId: string }
    ).participantId;
    const rotatedToken = (
      established.payload as { reconnectToken: string }
    ).reconnectToken;
    expect(restoredId).toBe(participantId);
    expect(rotatedToken).not.toBe(reconnectToken);

    const snapshot = await reconnected.waitForNext();
    expect(snapshot.type).toBe("roomSnapshot");
  });

  it("a reused, rotated-away reconnect token is refused unauthenticated", async () => {
    const { roomId, reconnectToken } = await createRoomAsHost();

    const first = await connect();
    send(first, "reconnectRoom", roomId, "", { reconnectToken });
    await first.waitForNext(); // sessionEstablished (rotates the token)
    await first.waitForNext(); // roomSnapshot

    const replay = await connect();
    send(replay, "reconnectRoom", roomId, "", { reconnectToken });
    const reply = await replay.waitForNext();
    expectError(reply, "unauthenticated");
  });

  it("reconnectRoom against an unknown room is refused room_not_found", async () => {
    const stray = await connect();
    send(stray, "reconnectRoom", "WP-NOPE-0000", "", {
      reconnectToken: "whatever",
    });
    const reply = await stray.waitForNext();
    expectError(reply, "room_not_found");
  });
});

describe("leaveRoom", () => {
  it("a non-host leaving with others remaining broadcasts an updated snapshot, not roomEnded", async () => {
    const { host, roomId } = await createRoomAsHost();
    const { guest, participantId: guestId } = await joinAsGuest(
      roomId,
      "Guest One"
    );
    await host.waitForNext(); // roomSnapshot after join

    send(guest, "leaveRoom", roomId, guestId, {});
    const hostSnapshot = await host.waitForNext();
    expect(hostSnapshot.type).toBe("roomSnapshot");
    const participants = (
      hostSnapshot.payload as { participants: Array<{ participantId: string }> }
    ).participants;
    expect(participants).toHaveLength(1);
  });

  it("the host cannot leaveRoom while other participants remain — must endRoom instead", async () => {
    const { host, roomId, participantId: hostId } = await createRoomAsHost();
    await joinAsGuest(roomId, "Guest One");
    await host.waitForNext(); // roomSnapshot after join

    send(host, "leaveRoom", roomId, hostId, {});
    const reply = await host.waitForNext();
    expectError(reply, "not_authorized");
  });

  it("a lone host leaving destroys the room (everyone-left erasure)", async () => {
    const { host, roomId, participantId: hostId } = await createRoomAsHost();
    send(host, "leaveRoom", roomId, hostId, {});

    // The room is gone: a fresh join attempt against the same id is
    // room_not_found.
    const prober = await connect();
    send(prober, "joinRoom", roomId, "", {
      identityKind: "guest",
      displayName: "Prober",
    });
    const reply = await prober.waitForNext();
    expectError(reply, "room_not_found");
  });
});

describe("endRoom", () => {
  it("host-only: a non-host endRoom attempt is refused not_authorized", async () => {
    const { roomId } = await createRoomAsHost();
    const { guest, participantId: guestId } = await joinAsGuest(
      roomId,
      "Guest One"
    );
    send(guest, "endRoom", roomId, guestId, {});
    const reply = await guest.waitForNext();
    expectError(reply, "not_authorized");
  });

  it("host endRoom broadcasts roomEnded to everyone and fully erases room state", async () => {
    const { host, roomId, participantId: hostId } = await createRoomAsHost();
    const { guest } = await joinAsGuest(roomId, "Guest One");
    await host.waitForNext(); // roomSnapshot after guest joined

    send(host, "endRoom", roomId, hostId, {});
    const hostEnded = await host.waitForNext();
    const guestEnded = await guest.waitForNext();
    expect(hostEnded.type).toBe("roomEnded");
    expect(guestEnded.type).toBe("roomEnded");

    // Ephemeral-state boundary: the room is fully gone, including for a
    // reconnect attempt with what used to be a valid token.
    const prober = await connect();
    send(prober, "joinRoom", roomId, "", {
      identityKind: "guest",
      displayName: "Prober",
    });
    const reply = await prober.waitForNext();
    expectError(reply, "room_not_found");
  });
});

// Slice 4 (docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md)
// implements timelineCommand/setControlMode/participantState/
// removeParticipant/chat/reaction for real — see test/room-authority.test.ts
// for their coverage. The placeholder "not yet handled" typed-refusal test
// that lived here through Slice 3 is superseded, not silently dropped: it
// asserted exactly the stopgap behavior this slice replaces.

describe("negative-control support (see slice report for the flip-one-guard transcript)", () => {
  it("baseline sanity: create -> join -> leave -> end all succeed in sequence", async () => {
    const { host, roomId, participantId: hostId } = await createRoomAsHost();
    const { guest, participantId: guestId } = await joinAsGuest(
      roomId,
      "Sanity Guest"
    );
    await host.waitForNext(); // roomSnapshot after join

    send(guest, "leaveRoom", roomId, guestId, {});
    await host.waitForNext(); // roomSnapshot after leave

    send(host, "endRoom", roomId, hostId, {});
    const ended = await host.waitForNext();
    expect(ended.type).toBe("roomEnded");
  });
});
