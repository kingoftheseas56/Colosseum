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
  buildChatMessage,
  buildErrorMessage,
  buildHostChangedMessage,
  buildParticipantStateMessage,
  buildReactionMessage,
  buildRoomSnapshotMessage,
  buildRoomEndedMessage,
  buildSessionEstablishedMessage,
  buildTimelineStateMessage,
  MAX_PARTICIPANTS,
  parseMessage,
  serializeMessage,
  sourceDescriptorFromJson,
  timelineCommandFromJson,
  type ChatEvent,
  type ControlMode,
  type IdentityKind,
  type ParticipantState,
  type ProtocolMessage,
  type ReactionEvent,
  type RoomSnapshot,
  type SourceDescriptor,
  type SyncStatus,
  type TimelineState,
} from "./protocol";
import { extractBearerToken, validateBearer, type Env } from "./auth";

// ---------------------------------------------------------------------------
// Slice 4 constants (docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md)
// ---------------------------------------------------------------------------

/** Contract "Transport": desktop generic rate ceiling, per connection per
 * direction. The service enforces the identical ceiling (an "equivalent or
 * stricter bounded rate policy" per contract), never a looser one. */
const RATE_LIMIT_MAX_MESSAGES = 120;
const RATE_LIMIT_WINDOW_MS = 10_000;

/** Contract "Host disconnect grace and deterministic transfer": "the
 * concrete grace duration is intentionally a server configuration/runtime
 * choice ... not invented by this desktop slice." `RELAY_HOST_GRACE_MS` is
 * that deployment setting; unset/unparseable falls back to this default. */
const DEFAULT_HOST_GRACE_MS = 60_000;

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
  /** Contract "chat"/"reaction": "Service stamps ... an event sequence
   * before broadcasting." One shared counter for both chat and reaction
   * events in the room, mirroring the client-side conformance oracle
   * (WatchPartyRoomController::m_nextEventSequence, one counter for both
   * event kinds). */
  nextEventSequence: number;
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
  /** Timestamps (ms) of recent inbound frames on this connection, for the
   * per-connection rolling rate ceiling (contract "Transport": <=120
   * messages/10s per connection per direction). Pruned to the current
   * window on every frame. */
  messageTimestamps: number[];
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
    const identity = await validateBearer(this.env, bearerToken);

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
      messageTimestamps: [],
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
    if (!this.consumeRateLimit(connection)) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "rate_limited",
          `rate limit exceeded (${RATE_LIMIT_MAX_MESSAGES} messages / ${RATE_LIMIT_WINDOW_MS / 1000}s)`
        )
      );
      return;
    }

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
      case "timelineCommand":
        this.handleTimelineCommand(connection, message);
        return;
      case "setControlMode":
        this.handleSetControlMode(connection, message);
        return;
      case "participantState":
        this.handleParticipantState(connection, message);
        return;
      case "removeParticipant":
        this.handleRemoveParticipant(connection, message);
        return;
      case "chat":
        this.handleChat(connection, message);
        return;
      case "reaction":
        this.handleReaction(connection, message);
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

  /** Contract "Transport": <=120 messages/10s per connection per direction,
   * a rolling window. Pruned-then-push, so the ceiling is exact rather than
   * bucketed. Runs BEFORE parseMessage — a client hammering malformed
   * frames still consumes its own rate budget rather than getting free
   * retries. */
  private consumeRateLimit(connection: ConnectionState): boolean {
    const now = Date.now();
    const windowStart = now - RATE_LIMIT_WINDOW_MS;
    connection.messageTimestamps = connection.messageTimestamps.filter(
      (t) => t > windowStart
    );
    if (connection.messageTimestamps.length >= RATE_LIMIT_MAX_MESSAGES) {
      return false;
    }
    connection.messageTimestamps.push(now);
    return true;
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
    // them (contract "Reconnect behavior").
    participant.connected = false;
    participant.ready = false;
    participant.syncStatus = "unknown";
    participant.connectionId = null;

    if (participant.host) {
      // Oracle: WatchPartyRoomController::disconnect() — starts the host
      // grace clock; disconnect() itself calls advanceTime() immediately
      // when hostGraceMs == 0 (a deployment choosing zero grace transfers
      // instantly rather than scheduling a redundant alarm).
      const graceMs = this.hostGraceMs();
      if (graceMs <= 0) {
        this.expireHostGrace(room);
        void this.scheduleGraceAlarm();
        return;
      }
      room.hostReconnectDeadlineMs = Date.now() + graceMs;
      void this.scheduleGraceAlarm();
    }

    // roomSnapshot is the authoritative connectivity/grace-state carrier
    // (contract "roomSnapshot": "send a fresh snapshot for membership/
    // control-mode/grace-state changes"). Remaining connected participants
    // must see this participant go offline (and, for a host, the new
    // hostReconnectDeadlineMs).
    this.broadcastRoomSnapshot(room);
  }

  private hostGraceMs(): number {
    const raw = this.env.RELAY_HOST_GRACE_MS;
    if (!raw) return DEFAULT_HOST_GRACE_MS;
    const parsed = Number.parseInt(raw, 10);
    return Number.isFinite(parsed) && parsed >= 0
      ? parsed
      : DEFAULT_HOST_GRACE_MS;
  }

  // -------------------------------------------------------------------
  // Host-grace alarm (Slice 4): a single DO instance holds a REGISTRY of
  // rooms (Slice 3's recorded architecture discrepancy — see file header),
  // but a Durable Object has only one alarm clock. This DO always keeps the
  // alarm set to the EARLIEST pending hostReconnectDeadlineMs across every
  // room it holds, and on each firing re-evaluates every room whose
  // deadline has passed, then reschedules for whatever is still pending.
  // -------------------------------------------------------------------

  private async scheduleGraceAlarm(): Promise<void> {
    let earliest: number | null = null;
    for (const room of this.rooms.values()) {
      if (room.hostReconnectDeadlineMs < 0) continue;
      if (earliest === null || room.hostReconnectDeadlineMs < earliest) {
        earliest = room.hostReconnectDeadlineMs;
      }
    }
    if (earliest === null) {
      await this.state.storage.deleteAlarm();
    } else {
      await this.state.storage.setAlarm(earliest);
    }
  }

  /** Durable Object alarm entry point — invoked by the runtime when the
   * scheduled time arrives, or forced immediately in tests via
   * `runDurableObjectAlarm()` (@cloudflare/vitest-pool-workers). Must not
   * assume the forced-early case never happens: every room is re-checked
   * against the CURRENT clock, not fired unconditionally. */
  async alarm(): Promise<void> {
    const now = Date.now();
    const due: RoomRecord[] = [];
    for (const room of this.rooms.values()) {
      if (room.hostReconnectDeadlineMs >= 0 && room.hostReconnectDeadlineMs <= now) {
        due.push(room);
      }
    }
    for (const room of due) {
      if (!this.rooms.has(room.roomId)) continue; // erased by an earlier iteration's roomEnded
      this.expireHostGrace(room);
    }
    await this.scheduleGraceAlarm();
  }

  /** Oracle: WatchPartyRoomController::advanceTime() — the deposed host is
   * erased from the roster entirely (not merely marked disconnected; a
   * disconnected NON-host participant keeps their roster slot for
   * reconnectRoom, but a host who has exhausted their grace window has no
   * further path back to that identity). Contract "Host disconnect grace
   * and deterministic transfer": earliest-joined CONNECTED SIGNED-IN
   * participant succeeds; guests are never eligible; no eligible successor
   * ends and destroys the room. */
  private expireHostGrace(room: RoomRecord): void {
    const previousHostId = room.hostParticipantId;
    room.participants.delete(previousHostId);

    let successor: ParticipantRecord | null = null;
    for (const candidate of room.participants.values()) {
      if (!candidate.connected || candidate.identityKind !== "signedIn") {
        continue;
      }
      if (!successor || candidate.joinOrder < successor.joinOrder) {
        successor = candidate;
      }
    }

    if (!successor) {
      this.destroyRoom(room);
      return;
    }

    successor.host = true;
    room.hostParticipantId = successor.participantId;
    room.hostReconnectDeadlineMs = -1;

    this.broadcastToRoom(room, (sequence) =>
      buildHostChangedMessage(room.roomId, successor!.participantId, sequence)
    );
    // Membership changed (the previous host's row is gone) — contract
    // "roomSnapshot": membership changes get a fresh snapshot, not just the
    // incremental hostChanged event.
    this.broadcastRoomSnapshot(room);
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
      nextEventSequence: 1,
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
    if (matched.host) {
      // Oracle: WatchPartyRoomController::reconnect() clears the deadline
      // unconditionally on a bound host reconnect (contract "if host
      // reconnects validly before expiry, retain host authority").
      room.hostReconnectDeadlineMs = -1;
      void this.scheduleGraceAlarm();
    }

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
    // The destroyed room may have been the alarm's only pending deadline
    // (or its erasure/expireHostGrace call chain may have just finished
    // handling it) — recompute so the DO never sits with a stale alarm
    // pointed at a room that no longer exists.
    void this.scheduleGraceAlarm();
  }

  // -------------------------------------------------------------------
  // timelineCommand
  // -------------------------------------------------------------------

  /** Contract "timelineCommand": "The service applies the existing room
   * authorization rule: Host Control -> host only; Shared Control -> any
   * current participant. On success, mutate the authoritative room
   * timeline and emit timelineState with a newer timeline revision."
   * Oracle: WatchPartyRoomController::applyTimelineCommand() increments
   * the revision by exactly 1 per accepted command — never reissued
   * (unchanged on refusal), never regressed, never skipped. */
  private handleTimelineCommand(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    const authorized = participant.host || room.controlMode === "shared";
    if (!authorized) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "only the host may control the timeline in Host Control mode",
          room.roomId
        )
      );
      return;
    }

    // protocol.ts's parseMessage already ran this exact parser as part of
    // validateClientMessage before dispatch reached this handler, so this
    // re-parse cannot fail in practice — kept as a typed refusal (never a
    // silent throw) rather than a non-null assertion, in case that
    // invariant ever drifts.
    const commandResult = timelineCommandFromJson(message.payload);
    if (!commandResult.ok) {
      this.sendTo(
        connection,
        buildErrorMessage("invalid_message", commandResult.error!, room.roomId)
      );
      return;
    }
    const command = commandResult.value!;

    switch (command.command) {
      case "play":
        room.timeline.playing = true;
        if (command.positionMs !== undefined) {
          room.timeline.positionMs = command.positionMs;
        }
        break;
      case "pause":
        room.timeline.playing = false;
        if (command.positionMs !== undefined) {
          room.timeline.positionMs = command.positionMs;
        }
        break;
      case "seek":
        // protocol.ts's timelineCommandFromJson already required
        // positionMs to be present for "seek".
        room.timeline.positionMs = command.positionMs!;
        break;
    }
    room.timeline.revision += 1;

    this.broadcastToRoom(room, (sequence) =>
      buildTimelineStateMessage(room.roomId, room.timeline, sequence)
    );
  }

  // -------------------------------------------------------------------
  // setControlMode
  // -------------------------------------------------------------------

  private handleSetControlMode(
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
          "only the host can change control mode",
          room.roomId
        )
      );
      return;
    }

    const mode = (message.payload as { controlMode: ControlMode }).controlMode;
    room.controlMode = mode;
    // Contract "setControlMode": "Broadcast resulting authoritative room
    // state" — the full snapshot, not an incremental message type.
    this.broadcastRoomSnapshot(room);
  }

  // -------------------------------------------------------------------
  // participantState
  // -------------------------------------------------------------------

  /** Contract "participantState": "participant-local readiness/sync
   * reporting, not playback authority. The service binds it to the
   * connection's participant; it must not accept a client-selected
   * participant ID." requireBinding() above is exactly that binding —
   * the payload carries no participantId field at all (protocol.ts's
   * exactKeys on this payload only allows ready/syncStatus). */
  private handleParticipantState(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    const payload = message.payload as {
      ready: boolean;
      syncStatus: SyncStatus;
    };
    participant.ready = payload.ready;
    participant.syncStatus = payload.syncStatus;

    const updated = this.toParticipantState(participant);
    this.broadcastToRoom(room, (sequence) =>
      buildParticipantStateMessage(room.roomId, updated, sequence)
    );
  }

  // -------------------------------------------------------------------
  // removeParticipant (kick)
  // -------------------------------------------------------------------

  /** Contract "removeParticipant": host-only, cannot remove the host.
   * "On accepted removal the service must: terminate the target
   * participant's current room membership; invalidate that participant's
   * reconnect credential immediately; send the target a terminal error
   * with code participant_removed when the connection is still reachable;
   * exclude the target from the next authoritative membership snapshot;
   * broadcast the resulting authoritative room state to remaining
   * participants." Locked Arc-3 policy (LIFECYCLE-POLICY-GATE.md, recorded
   * in the plan's Slice 4 section): no ban/blacklist — the target is fully
   * erased from this room's membership, so a fresh joinRoom afterward is
   * evaluated under the same rules as any stranger. */
  private handleRemoveParticipant(
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
          "only the host can remove a participant",
          room.roomId
        )
      );
      return;
    }

    const targetId = (
      message.payload as { participantId: string }
    ).participantId.trim();
    const target = room.participants.get(targetId);
    if (!target) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "invalid_message",
          "no such participant in this room",
          room.roomId
        )
      );
      return;
    }
    if (target.host) {
      this.sendTo(
        connection,
        buildErrorMessage(
          "not_authorized",
          "the host cannot be removed through this operation",
          room.roomId
        )
      );
      return;
    }

    // Erase membership (and, implicitly, the reconnect token — it is a key
    // into this Map entry and this Map entry is gone) BEFORE touching the
    // target's socket, so a `close` event racing this call finds no
    // roomId/participantId bound and no participant record to act on.
    room.participants.delete(target.participantId);

    if (target.connectionId !== null) {
      const targetConn = this.connections.get(target.connectionId);
      if (targetConn) {
        this.sendTo(
          targetConn,
          buildErrorMessage(
            "participant_removed",
            "removed by the host",
            room.roomId
          )
        );
        // Any frame already queued on this connection now finds no active
        // session bound (requireBinding's "no active session" refusal) —
        // "reject any queued/future commands from that session" without a
        // separate ban list.
        targetConn.roomId = null;
        targetConn.participantId = null;
        try {
          targetConn.socket.close(1000, "participant-removed");
        } catch {
          // Socket already closing/closed on the target's side — nothing
          // further to do; handleDisconnect's own no-op guards cover it.
        }
      }
    }

    this.broadcastRoomSnapshot(room);
  }

  // -------------------------------------------------------------------
  // chat / reaction
  // -------------------------------------------------------------------

  /** Contract "chat": "Ephemeral only. Service stamps authoritative
   * participant identity/display name and an event sequence before
   * broadcasting the server chat event. No chat history persistence." —
   * this relay never writes the event anywhere but the outbound broadcast
   * (no `this.rooms`-external store, nothing in Durable Object storage). */
  private handleChat(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    const text = (message.payload as { message: string }).message.trim();
    const event: ChatEvent = {
      sequence: room.nextEventSequence++,
      participantId: participant.participantId,
      displayName: participant.displayName,
      message: text,
    };
    this.broadcastToRoom(room, (sequence) =>
      buildChatMessage(room.roomId, event, sequence)
    );
  }

  /** Same identity/sequence/ephemeral rule as chat (contract "reaction"). */
  private handleReaction(
    connection: ConnectionState,
    message: ProtocolMessage
  ): void {
    const bound = this.requireBinding(connection, message);
    if (!bound) return;
    const { room, participant } = bound;

    const text = (message.payload as { reaction: string }).reaction.trim();
    const event: ReactionEvent = {
      sequence: room.nextEventSequence++,
      participantId: participant.participantId,
      displayName: participant.displayName,
      reaction: text,
    };
    this.broadcastToRoom(room, (sequence) =>
      buildReactionMessage(room.roomId, event, sequence)
    );
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
    this.broadcastToRoom(room, (sequence) =>
      buildRoomSnapshotMessage(snapshot, sequence)
    );
  }

  /** Sends one envelope, built fresh per recipient (each connection keeps
   * its own outbound sequence counter — contract "sequence": "Client-
   * generated operational messages use a monotonically advancing positive
   * sequence within the client process"; the server side mirrors that
   * per-connection, not room-wide), to every currently-connected
   * participant in the room. */
  private broadcastToRoom(
    room: RoomRecord,
    build: (sequence: number) => ProtocolMessage
  ): void {
    for (const participant of room.participants.values()) {
      if (participant.connectionId === null) continue;
      const conn = this.connections.get(participant.connectionId);
      if (!conn) continue;
      this.sendTo(conn, build(conn.nextOutboundSequence++));
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
