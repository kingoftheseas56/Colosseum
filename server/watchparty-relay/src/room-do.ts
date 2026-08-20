// RoomDO — room lifecycle (Slice 3 of docs/superpowers/plans/
// 2026-08-20-watch-party-relay-plan.md): create/join/reconnect/leave/end,
// identity binding, capacity. Built on Slice 2's protocol core
// (src/protocol.ts) — never around it.
//
// Contract reference (frozen):
// C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/03-watch-party/
// watch-party-qml-slice-08/SERVER-PROTOCOL-CONTRACT.md
//   - "Authentication and room-scoped reconnect credential"
//   - "createRoom", both "joinRoom" forms, "reconnectRoom", "leaveRoom",
//     "endRoom"
//   - "sessionEstablished", "roomSnapshot", "roomEnded", "error"
//   - "Ephemeral-state boundary"
//
// Conformance oracle for the domain rules the wire contract leaves to the
// service (host-must-end-not-leave, capacity, join-order, exactly-one-host
// invariant): native/watchparty/WatchPartyRoomController.{h,cpp}
// (read-only, client is FROZEN for this plan). Where the contract prose is
// silent, the client's own room-authority reference (RoomController) wins
// over an invented policy — e.g. `RoomController::leave()` returns
// `HostMustEndRoom` for a host with other participants present, which is
// exactly the rule this DO needs to keep every `roomSnapshot`'s
// "exactly one host" invariant intact absent grace/transfer (Slice 4).
//
// Architecture note (discrepancy, recorded per task instructions): the
// plan's header says "Durable Object per room". The frozen client only
// ever opens ONE static service URL (WatchPartyUiController::
// configureServiceUrl) and sends createRoom/joinRoom *after* the socket is
// already open — it never learns a room-specific URL/path before
// connecting, so there is no way for src/index.ts to route a fresh
// WebSocket upgrade to a per-room DO before the room is known. This slice
// therefore keeps Slice 1/2's single fixed DO instance
// (`env.ROOMS.idFromName("slice1-spike-room")`, unchanged in index.ts) and
// has that ONE DO instance hold a registry of rooms (`Map<roomId, Room>`).
// Every room is still exclusively owned by Durable Object state (in
// memory, erased per the ephemeral boundary) — only the "one DO per room"
// shard granularity is deferred; true per-room sharding would need either
// a client-side change (frozen) or a lobby-hop redirect scheme, both out
// of scope for Slice 3. Flagged for Slice 9 deployment planning.

import {
  buildErrorMessage,
  buildRoomSnapshotMessage,
  buildRoomEndedMessage,
  buildSessionEstablishedMessage,
  MAX_PARTICIPANTS,
  parseMessage,
  serializeMessage,
  sourceDescriptorFromJson,
  type ControlMode,
  type IdentityKind,
  type ParticipantState,
  type ProtocolMessage,
  type RoomSnapshot,
  type SourceDescriptor,
  type SyncStatus,
  type TimelineState,
} from "./protocol";
import { extractBearerToken, validateBearer, type Env } from "./auth";

// ---------------------------------------------------------------------------
// Domain state
// ---------------------------------------------------------------------------

interface ParticipantRecord {
  participantId: string;
  displayName: string;
  identityKind: IdentityKind;
  joinOrder: number;
  host: boolean;
  connected: boolean;
  ready: boolean;
  syncStatus: SyncStatus;
  /** Signed-in binding (null for guests). Not wire-visible. */
  username: string | null;
  /** Current valid reconnect credential. Rotated on every accepted
   * reconnect; memory-only, never logged, never broadcast. */
  reconnectToken: string;
  /** The live connection currently bound to this participant, if any. */
  connectionId: number | null;
}

interface RoomRecord {
  roomId: string;
  hostParticipantId: string;
  source: SourceDescriptor;
  controlMode: ControlMode;
  timeline: TimelineState;
  participants: Map<string, ParticipantRecord>;
  hostReconnectDeadlineMs: number;
  nextJoinOrder: number;
}

interface ConnectionState {
  id: number;
  socket: WebSocket;
  /** Resolved once at upgrade time from the Authorization header. Null for
   * guests AND for a bearer that failed to authenticate — both are
   * guest-equivalent until a signed-in action is attempted, at which point
   * the "unauthenticated" refusal fires. */
  authenticatedUsername: string | null;
  roomId: string | null;
  participantId: string | null;
  nextOutboundSequence: number;
}

const ROOM_ID_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I

function generateRoomId(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  let code = "";
  for (let i = 0; i < 8; i++) {
    code += ROOM_ID_ALPHABET[bytes[i] % ROOM_ID_ALPHABET.length];
  }
  return `WP-${code.slice(0, 4)}-${code.slice(4, 8)}`;
}

function randomOpaqueToken(byteLength: number): string {
  const bytes = crypto.getRandomValues(new Uint8Array(byteLength));
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function generateParticipantId(): string {
  return `p_${randomOpaqueToken(16)}`;
}

function generateReconnectToken(): string {
  return randomOpaqueToken(24);
}

// Message types whose room/authority machinery is Slice 4's job (timeline
// authority, control mode, presence, moderation, chat/reactions). The
// contract's closed error-code vocabulary has no "not implemented yet"
// code, so per the plan's instruction ("respond with ... a generic typed
// internal refusal") these are refused `invalid_message` — the request is
// syntactically accepted by protocol.ts but this service does not yet
// process this operation.
const SLICE_4_MESSAGE_TYPES = new Set([
  "timelineCommand",
  "setControlMode",
  "participantState",
  "removeParticipant",
  "chat",
  "reaction",
]);

export class RoomDO {
  state: DurableObjectState;
  env: Env;

  private rooms = new Map<string, RoomRecord>();
  private connections = new Map<number, ConnectionState>();
  private nextConnectionId = 1;

  constructor(state: DurableObjectState, env: Env) {
    this.state = state;
    this.env = env;
  }

  async fetch(request: Request): Promise<Response> {
    const upgrade = request.headers.get("Upgrade");
    if (!upgrade || upgrade.toLowerCase() !== "websocket") {
      return new Response("expected websocket upgrade", { status: 400 });
    }

    const bearerToken = extractBearerToken(
      request.headers.get("Authorization")
    );
    const identity = validateBearer(this.env, bearerToken);

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair) as [WebSocket, WebSocket];
    server.accept();

    const connection: ConnectionState = {
      id: this.nextConnectionId++,
      socket: server,
      authenticatedUsername: identity ? identity.username : null,
      roomId: null,
      participantId: null,
      nextOutboundSequence: 0,
    };
    this.connections.set(connection.id, connection);

    server.addEventListener("message", (event: MessageEvent) => {
      this.handleFrame(connection, event.data);
    });

    server.addEventListener("close", () => {
      this.handleDisconnect(connection);
    });

    return new Response(null, { status: 101, webSocket: client });
  }

  // -------------------------------------------------------------------
  // Frame dispatch
  // -------------------------------------------------------------------

  private handleFrame(connection: ConnectionState, data: unknown): void {
    const raw =
      typeof data === "string"
        ? data
        : data instanceof ArrayBuffer
          ? data
          : String(data);

    const result = parseMessage(raw as string | ArrayBuffer, "clientToServer");
    if (!result.ok) {
      this.sendTo(connection, buildErrorMessage(result.code, result.error));
      if (result.code === "protocol_version_mismatch") {
        connection.socket.close(1000, "protocol-version-mismatch");
      }
      return;
    }

    const message = result.message;

    if (SLICE_4_MESSAGE_TYPES.has(message.type)) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "invalid_message",
          `message type '${message.type}' is not yet handled by this relay (Slice 4)`,
          message.roomId
        )
      );
      return;
    }

    switch (message.type) {
      case "createRoom":
        this.handleCreateRoom(connection, message);
        return;
      case "joinRoom":
        this.handleJoinRoom(connection, message);
        return;
      case "reconnectRoom":
        this.handleReconnectRoom(connection, message);
        return;
      case "leaveRoom":
        this.handleLeaveRoom(connection, message);
        return;
      case "endRoom":
        this.handleEndRoom(connection, message);
        return;
      default:
        this.sendTo(
          connection,
          buildErrorMessage(
            "invalid_message",
            `message type '${message.type}' is server-to-client only`,
            message.roomId
          )
        );
    }
  }

  private handleDisconnect(connection: ConnectionState): void {
    this.connections.delete(connection.id);

    if (!connection.roomId || !connection.participantId) return;
    const room = this.rooms.get(connection.roomId);
    if (!room) return;
    const participant = room.participants.get(connection.participantId);
    if (!participant) return;

    // A dropped socket is NOT an explicit leave — the participant keeps
    // their roster slot and reconnect token so `reconnectRoom` can restore
    // them (contract "Reconnect behavior"). Host-loss grace timing and
    // deterministic transfer are Slice 4; this slice only clears the live
    // connection binding.
    participant.connected = false;
    participant.ready = false;
    participant.syncStatus = "unknown";
    participant.connectionId = null;
  }

  // -------------------------------------------------------------------
  // createRoom
  // -------------------------------------------------------------------

  private handleCreateRoom(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    if (!connection.authenticatedUsername) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "unauthenticated",
          "only an authenticated identity can create a room"
        )
      );
      return;
    }

    if (connection.roomId) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "close the current Watch Party session before creating a new one"
        )
      );
      return;
    }

    const sourceResult = sourceDescriptorFromJson(
      (message.payload as { source?: unknown }).source
    );
    if (!sourceResult.ok) {
      this.sendTo(connection, buildErrorMessage("invalid_source", sourceResult.error!));
      return;
    }

    let roomId = generateRoomId();
    while (this.rooms.has(roomId)) roomId = generateRoomId();

    const participantId = generateParticipantId();
    const reconnectToken = generateReconnectToken();

    const host: ParticipantRecord = {
      participantId,
      displayName: connection.authenticatedUsername,
      identityKind: "signedIn",
      joinOrder: 0,
      host: true,
      connected: true,
      ready: false,
      syncStatus: "unknown",
      username: connection.authenticatedUsername,
      reconnectToken,
      connectionId: connection.id,
    };

    const room: RoomRecord = {
      roomId,
      hostParticipantId: participantId,
      source: sourceResult.value!,
      controlMode: "host",
      timeline: { playing: false, positionMs: 0, revision: 0 },
      participants: new Map([[participantId, host]]),
      hostReconnectDeadlineMs: -1,
      nextJoinOrder: 1,
    };
    this.rooms.set(roomId, room);

    connection.roomId = roomId;
    connection.participantId = participantId;

    this.sendTo(
      connection,
      buildSessionEstablishedMessage(
        roomId,
        { participantId, reconnectToken },
        connection.nextOutboundSequence++
      )
    );
    this.sendTo(
      connection,
      buildRoomSnapshotMessage(
        this.toRoomSnapshot(room),
        connection.nextOutboundSequence++
      )
    );
  }

  // -------------------------------------------------------------------
  // joinRoom (signed-in + guest)
  // -------------------------------------------------------------------

  private handleJoinRoom(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    if (connection.roomId) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "close the current Watch Party session before joining another one"
        )
      );
      return;
    }

    const roomId = message.roomId;
    const room = this.rooms.get(roomId);
    if (!room) {
      this.sendTo(connection, buildErrorMessage("room_not_found", "no such room", roomId));
      return;
    }

    const payload = message.payload as {
      identityKind: string;
      displayName?: string;
    };

    let displayName: string;
    let identityKind: IdentityKind;
    let username: string | null;

    if (payload.identityKind === "signedIn") {
      if (!connection.authenticatedUsername) {
        this.sendTo(
          connection,
          buildErrorMessage(
            "unauthenticated",
            "signed-in join requires an authenticated identity",
            roomId
          )
        );
        return;
      }
      identityKind = "signedIn";
      displayName = connection.authenticatedUsername;
      username = connection.authenticatedUsername;
    } else {
      identityKind = "guest";
      displayName = (payload.displayName ?? "").trim();
      username = null;
    }

    if (room.participants.size >= MAX_PARTICIPANTS) {
      this.sendTo(connection, buildErrorMessage("room_full", "Watch Party is full", roomId));
      return;
    }

    const participantId = generateParticipantId();
    const reconnectToken = generateReconnectToken();

    const record: ParticipantRecord = {
      participantId,
      displayName,
      identityKind,
      joinOrder: room.nextJoinOrder++,
      host: false,
      connected: true,
      ready: false,
      syncStatus: "unknown",
      username,
      reconnectToken,
      connectionId: connection.id,
    };
    room.participants.set(participantId, record);

    connection.roomId = roomId;
    connection.participantId = participantId;

    this.sendTo(
      connection,
      buildSessionEstablishedMessage(
        roomId,
        { participantId, reconnectToken },
        connection.nextOutboundSequence++
      )
    );
    this.broadcastRoomSnapshot(room);
  }

  // -------------------------------------------------------------------
  // reconnectRoom
  // -------------------------------------------------------------------

  private handleReconnectRoom(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const roomId = message.roomId;
    const room = this.rooms.get(roomId);
    if (!room) {
      this.sendTo(connection, buildErrorMessage("room_not_found", "no such room", roomId));
      return;
    }

    const token = (message.payload as { reconnectToken: string }).reconnectToken;

    let matched: ParticipantRecord | null = null;
    for (const participant of room.participants.values()) {
      if (participant.reconnectToken === token) {
        matched = participant;
        break;
      }
    }

    if (!matched) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "unauthenticated",
          "reconnect token is invalid, expired, or already rotated away",
          roomId
        )
      );
      return;
    }

    // Rotate the credential on every accepted reconnect (contract
    // "Reconnect credential"). The old token stops being a valid key into
    // any participant record the instant this line runs — a replay with
    // the stale token falls through to the `!matched` branch above.
    matched.reconnectToken = generateReconnectToken();
    matched.connected = true;
    matched.connectionId = connection.id;
    if (matched.host) room.hostReconnectDeadlineMs = -1;

    connection.roomId = roomId;
    connection.participantId = matched.participantId;

    this.sendTo(
      connection,
      buildSessionEstablishedMessage(
        roomId,
        {
          participantId: matched.participantId,
          reconnectToken: matched.reconnectToken,
        },
        connection.nextOutboundSequence++
      )
    );
    this.broadcastRoomSnapshot(room);
  }

  // -------------------------------------------------------------------
  // leaveRoom
  // -------------------------------------------------------------------

  private handleLeaveRoom(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    // Oracle: WatchPartyRoomController::leave() refuses HostMustEndRoom for
    // a host with other participants present, preserving roomSnapshot's
    // "exactly one host" invariant absent grace/transfer machinery
    // (Slice 4). A lone host leaving destroys the room via the normal
    // "everyone left" path below.
    if (participant.host && room.participants.size > 1) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "the host must end the Watch Party, not leave it",
          room.roomId
        )
      );
      return;
    }

    room.participants.delete(participant.participantId);
    connection.roomId = null;
    connection.participantId = null;

    if (room.participants.size === 0) {
      this.rooms.delete(room.roomId);
      return;
    }

    this.broadcastRoomSnapshot(room);
  }

  // -------------------------------------------------------------------
  // endRoom
  // -------------------------------------------------------------------

  private handleEndRoom(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    if (!participant.host) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "only the host can end this Watch Party",
          room.roomId
        )
      );
      return;
    }

    this.destroyRoom(room);
  }

  /** Full ephemeral-state erasure (contract "Ephemeral-state boundary"):
   * broadcast roomEnded, unbind every connection, invalidate every
   * participant's reconnect credential, drop the room record entirely. */
  private destroyRoom(room: RoomRecord): void {
    const endedMessage = buildRoomEndedMessage(room.roomId);
    for (const participant of room.participants.values()) {
      if (participant.connectionId === null) continue;
      const conn = this.connections.get(participant.connectionId);
      if (!conn) continue;
      this.sendTo(conn, endedMessage);
      conn.roomId = null;
      conn.participantId = null;
    }
    this.rooms.delete(room.roomId);
  }

  // -------------------------------------------------------------------
  // Shared helpers
  // -------------------------------------------------------------------

  /** Binds a session-scoped command to its room/participant, rejecting a
   * forged `senderId` or a stale/unbound connection. `senderId` is NEVER
   * trusted as authority on its own (contract "Protocol envelope") — the
   * connection's own bound participant is the only source of truth; a
   * client-asserted senderId that disagrees with it is refused. */
  private requireBinding(
    connection: ConnectionState,
    message: ProtocolMessage
  ): { room: RoomRecord; participant: ParticipantRecord } | null {
    if (!connection.roomId || !connection.participantId) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "no active Watch Party session on this connection",
          message.roomId
        )
      );
      return null;
    }

    if (
      message.roomId !== connection.roomId ||
      message.senderId !== connection.participantId
    ) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "sender ID does not match the participant bound to this connection",
          message.roomId
        )
      );
      return null;
    }

    const room = this.rooms.get(connection.roomId);
    if (!room) {
      this.sendTo(
        connection,
        buildErrorMessage("room_not_found", "no such room", message.roomId)
      );
      return null;
    }
    const participant = room.participants.get(connection.participantId);
    if (!participant) {
      this.sendTo(
        connection,
        buildErrorMessage("room_not_found", "no such participant", message.roomId)
      );
      return null;
    }

    return { room, participant };
  }

  private toParticipantState(record: ParticipantRecord): ParticipantState {
    return {
      participantId: record.participantId,
      displayName: record.displayName,
      identityKind: record.identityKind,
      joinOrder: record.joinOrder,
      host: record.host,
      connected: record.connected,
      ready: record.ready,
      syncStatus: record.syncStatus,
    };
  }

  private toRoomSnapshot(room: RoomRecord): RoomSnapshot {
    return {
      roomId: room.roomId,
      hostParticipantId: room.hostParticipantId,
      source: room.source,
      controlMode: room.controlMode,
      timeline: room.timeline,
      participants: [...room.participants.values()].map((p) =>
        this.toParticipantState(p)
      ),
      hostReconnectDeadlineMs: room.hostReconnectDeadlineMs,
    };
  }

  private broadcastRoomSnapshot(room: RoomRecord): void {
    const snapshot = this.toRoomSnapshot(room);
    for (const participant of room.participants.values()) {
      if (participant.connectionId === null) continue;
      const conn = this.connections.get(participant.connectionId);
      if (!conn) continue;
      this.sendTo(
        conn,
        buildRoomSnapshotMessage(snapshot, conn.nextOutboundSequence++)
      );
    }
  }

  private sendTo(connection: ConnectionState, envelope: ProtocolMessage): void {
    try {
      connection.socket.send(serializeMessage(envelope));
    } catch (err) {
      console.log(
        `watchparty-relay RoomDO send failed conn=${connection.id}:`,
        String(err)
      );
    }
  }
}
