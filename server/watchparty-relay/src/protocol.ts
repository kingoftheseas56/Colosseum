// Colosseum Watch Party relay — protocol v3 core.
//
// Purpose (Slice 2 of docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md):
// speak protocol v3 EXACTLY — every envelope/message schema, strictly validated,
// no compatibility guessing — so the relay and the desktop client can never
// drift silently.
//
// Conformance oracle: native/watchparty/WatchPartyProtocol.{h,cpp} and
// native/watchparty/WatchPartyTypes.{h,cpp} (read-only, client is FROZEN for
// this plan). Every exact-keys rule, required/optional field, string-enum
// name, and numeric constraint below is transcribed from that client code —
// where SERVER-PROTOCOL-CONTRACT.md prose and the client code could be read
// two ways, the client's C++ validation (`exactKeys`, `readString`,
// `readNonNegativeInteger`, `readBool`, `identityKindFromName`,
// `controlModeFromName`, `syncStatusFromName`, `timelineCommandTypeFromName`,
// `sourceKindFromName`) is the literal source transcribed here.
//
// Wire error-code vocabulary (server -> client `error.code`) is transcribed
// from native/watchparty/WatchPartyRoomServiceClient.cpp's mapping (lines
// ~38-56 in the frozen client): unauthenticated, room_not_found, room_full,
// room_ended, participant_removed, not_authorized, invalid_source,
// invalid_message, protocol_version_mismatch, rate_limited. Any other wire
// code is mapped by the client to its own `serverRejected` bucket, so this
// relay never invents a new code outside that closed vocabulary.

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/** Protocol version this relay speaks. No compatibility guessing. */
export const PROTOCOL_VERSION = 3;

/**
 * Transport-level hard ceiling, transcribed from the client's
 * `kMaxWireMessageBytes` (native/watchparty/WatchPartyProtocol.h:14). The
 * service must reject/close messages above this ceiling BEFORE unbounded
 * parsing/allocation (contract "Transport").
 */
export const MAX_WIRE_MESSAGE_BYTES = 64 * 1024;

/** Contract "Room snapshot": 1..12 participant rows. */
export const MAX_PARTICIPANTS = 12;

// ---------------------------------------------------------------------------
// Message type vocabulary
// ---------------------------------------------------------------------------

export const CLIENT_TO_SERVER_TYPES = [
  "createRoom",
  "joinRoom",
  "reconnectRoom",
  "leaveRoom",
  "timelineCommand",
  "setControlMode",
  "participantState",
  "removeParticipant",
  "chat",
  "reaction",
  "endRoom",
] as const;

export const SERVER_TO_CLIENT_TYPES = [
  "sessionEstablished",
  "roomSnapshot",
  "timelineState",
  "participantState",
  "hostChanged",
  "chat",
  "reaction",
  "roomEnded",
  "error",
] as const;

export type ClientToServerType = (typeof CLIENT_TO_SERVER_TYPES)[number];
export type ServerToClientType = (typeof SERVER_TO_CLIENT_TYPES)[number];
export type MessageType = ClientToServerType | ServerToClientType;

const ALL_MESSAGE_TYPES: ReadonlySet<string> = new Set<string>([
  ...CLIENT_TO_SERVER_TYPES,
  ...SERVER_TO_CLIENT_TYPES,
]);

export type MessageDirection = "clientToServer" | "serverToClient";

/**
 * Frozen wire error-code vocabulary. Transcribed verbatim from
 * WatchPartyRoomServiceClient.cpp's code == "..." checks; any other string
 * the client receives is mapped to its own `serverRejected` bucket, so the
 * relay must never emit a code outside this set.
 */
export const ERROR_CODES = [
  "unauthenticated",
  "room_not_found",
  "room_full",
  "room_ended",
  "participant_removed",
  "not_authorized",
  "invalid_source",
  "invalid_message",
  "protocol_version_mismatch",
  "rate_limited",
] as const;

export type ErrorCode = (typeof ERROR_CODES)[number];

function isErrorCode(value: string): value is ErrorCode {
  return (ERROR_CODES as readonly string[]).includes(value);
}

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

export interface ProtocolMessage {
  version: number;
  type: MessageType;
  roomId: string;
  senderId: string;
  sequence: number;
  payload: Record<string, unknown>;
}

export interface ParseFailure {
  ok: false;
  code: ErrorCode;
  error: string;
}

export interface ParseSuccess {
  ok: true;
  message: ProtocolMessage;
}

export type ParseResult = ParseSuccess | ParseFailure;

function fail(code: ErrorCode, error: string): ParseFailure {
  return { ok: false, code, error };
}

// ---------------------------------------------------------------------------
// Field-level readers (transcribed from WatchPartyProtocol.cpp's
// readString/readBool/readNonNegativeInteger/exactKeys/exactKeysWithOptional)
// ---------------------------------------------------------------------------

interface FieldResult<T> {
  ok: boolean;
  value?: T;
  error?: string;
}

function okField<T>(value: T): FieldResult<T> {
  return { ok: true, value };
}

function errField<T>(error: string): FieldResult<T> {
  return { ok: false, error };
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value)
  );
}

function exactKeys(
  object: Record<string, unknown>,
  allowed: readonly string[],
  context: string
): FieldResult<true> {
  const allowedSet = new Set(allowed);
  for (const key of Object.keys(object)) {
    if (!allowedSet.has(key)) {
      return errField(`unknown ${context} key '${key}'`);
    }
  }
  for (const required of allowed) {
    if (!(required in object)) {
      return errField(`${context} is missing required key '${required}'`);
    }
  }
  return okField(true);
}

function exactKeysWithOptional(
  object: Record<string, unknown>,
  required: readonly string[],
  optional: readonly string[],
  context: string
): FieldResult<true> {
  const allowed = new Set([...required, ...optional]);
  for (const key of Object.keys(object)) {
    if (!allowed.has(key)) {
      return errField(`unknown ${context} key '${key}'`);
    }
  }
  for (const key of required) {
    if (!(key in object)) {
      return errField(`${context} is missing required key '${key}'`);
    }
  }
  return okField(true);
}

function requireEmptyPayload(
  object: Record<string, unknown>,
  context: string
): FieldResult<true> {
  if (Object.keys(object).length !== 0) {
    return errField(`${context} payload must be empty`);
  }
  return okField(true);
}

function readString(
  object: Record<string, unknown>,
  key: string,
  allowEmpty = false
): FieldResult<string> {
  const raw = object[key];
  if (typeof raw !== "string") {
    return errField(`${key} must be a string`);
  }
  if (!allowEmpty && raw.trim().length === 0) {
    return errField(`${key} must not be empty`);
  }
  return okField(raw);
}

function readBool(
  object: Record<string, unknown>,
  key: string
): FieldResult<boolean> {
  const raw = object[key];
  if (typeof raw !== "boolean") {
    return errField(`${key} must be a boolean`);
  }
  return okField(raw);
}

function readNonNegativeInteger(
  object: Record<string, unknown>,
  key: string
): FieldResult<number> {
  const raw = object[key];
  if (typeof raw !== "number" || !Number.isFinite(raw)) {
    return errField(`${key} must be an integer`);
  }
  if (!Number.isInteger(raw) || raw < 0) {
    return errField(`${key} must be a non-negative integer`);
  }
  return okField(raw);
}

// ---------------------------------------------------------------------------
// Domain value types (mirrors WatchPartyTypes.h)
// ---------------------------------------------------------------------------

export type IdentityKind = "signedIn" | "guest";
const IDENTITY_KINDS: readonly IdentityKind[] = ["signedIn", "guest"];

export type ControlMode = "host" | "shared";
const CONTROL_MODES: readonly ControlMode[] = ["host", "shared"];

export type SyncStatus = "unknown" | "inSync" | "desynced" | "buffering";
const SYNC_STATUSES: readonly SyncStatus[] = [
  "unknown",
  "inSync",
  "desynced",
  "buffering",
];

export type TimelineCommandType = "play" | "pause" | "seek";
const TIMELINE_COMMAND_TYPES: readonly TimelineCommandType[] = [
  "play",
  "pause",
  "seek",
];

export type SourceKind = "torrent" | "debrid";

export type SourceDescriptor =
  | { kind: "torrent"; infoHash: string; fileIdx: number }
  | { kind: "debrid"; providerId: string; providerSourceId: string };

export interface ParticipantIdentity {
  participantId: string;
  displayName: string;
  identityKind: IdentityKind;
}

export interface ParticipantState extends ParticipantIdentity {
  joinOrder: number;
  host: boolean;
  connected: boolean;
  ready: boolean;
  syncStatus: SyncStatus;
}

export interface TimelineState {
  playing: boolean;
  positionMs: number;
  revision: number;
}

export interface TimelineCommand {
  command: TimelineCommandType;
  positionMs?: number;
}

export interface ChatEvent {
  sequence: number;
  participantId: string;
  displayName: string;
  message: string;
}

export interface ReactionEvent {
  sequence: number;
  participantId: string;
  displayName: string;
  reaction: string;
}

export interface RoomSnapshot {
  roomId: string;
  hostParticipantId: string;
  source: SourceDescriptor;
  controlMode: ControlMode;
  timeline: TimelineState;
  participants: ParticipantState[];
  hostReconnectDeadlineMs: number;
}

export interface SessionEstablished {
  participantId: string;
  reconnectToken: string;
}

function readEnum<T extends string>(
  object: Record<string, unknown>,
  key: string,
  values: readonly T[],
  labelSingular: string
): FieldResult<T> {
  const nameResult = readString(object, key);
  if (!nameResult.ok) return errField(nameResult.error!);
  const name = nameResult.value!;
  if (!(values as readonly string[]).includes(name)) {
    return errField(`unknown ${labelSingular} '${name}'`);
  }
  return okField(name as T);
}

// ---------------------------------------------------------------------------
// Source descriptor (mirrors sourceDescriptorFromJson/sourceDescriptorToJson)
// ---------------------------------------------------------------------------

const INFO_HASH_PATTERN = /^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$/;
const PROVIDER_ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/;
const PROVIDER_SOURCE_ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,255}$/;

export function sourceDescriptorFromJson(
  object: unknown,
  context = "source"
): FieldResult<SourceDescriptor> {
  if (!isPlainObject(object)) {
    return errField(`${context} must be an object`);
  }

  const kindResult = readString(object, "kind");
  if (!kindResult.ok) return errField(kindResult.error!);
  const kindName = kindResult.value!;

  if (kindName === "torrent") {
    const keysResult = exactKeys(
      object,
      ["kind", "infoHash", "fileIdx"],
      "torrent source"
    );
    if (!keysResult.ok) return errField(keysResult.error!);

    const infoHashResult = readString(object, "infoHash");
    if (!infoHashResult.ok) return errField(infoHashResult.error!);
    const fileIdxResult = readNonNegativeInteger(object, "fileIdx");
    if (!fileIdxResult.ok) return errField(fileIdxResult.error!);

    // Client's SourceDescriptor::normalized(): infoHash trimmed + lowercased.
    const infoHash = infoHashResult.value!.trim().toLowerCase();
    const fileIdx = fileIdxResult.value!;

    if (!INFO_HASH_PATTERN.test(infoHash)) {
      return errField("invalid source descriptor");
    }
    if (fileIdx > Number.MAX_SAFE_INTEGER) {
      return errField("fileIdx is out of range");
    }

    return okField({ kind: "torrent", infoHash, fileIdx });
  }

  if (kindName === "debrid") {
    const keysResult = exactKeys(
      object,
      ["kind", "providerId", "providerSourceId"],
      "debrid source"
    );
    if (!keysResult.ok) return errField(keysResult.error!);

    const providerIdResult = readString(object, "providerId");
    if (!providerIdResult.ok) return errField(providerIdResult.error!);
    const providerSourceIdResult = readString(object, "providerSourceId");
    if (!providerSourceIdResult.ok)
      return errField(providerSourceIdResult.error!);

    const providerId = providerIdResult.value!.trim();
    const providerSourceId = providerSourceIdResult.value!.trim();

    if (!PROVIDER_ID_PATTERN.test(providerId)) {
      return errField("invalid source descriptor");
    }
    if (!PROVIDER_SOURCE_ID_PATTERN.test(providerSourceId)) {
      return errField("invalid source descriptor");
    }

    return okField({ kind: "debrid", providerId, providerSourceId });
  }

  return errField(`unknown source kind '${kindName}'`);
}

export function sourceDescriptorToJson(
  source: SourceDescriptor
): Record<string, unknown> {
  if (source.kind === "torrent") {
    return {
      kind: "torrent",
      infoHash: source.infoHash.trim().toLowerCase(),
      fileIdx: source.fileIdx,
    };
  }
  return {
    kind: "debrid",
    providerId: source.providerId.trim(),
    providerSourceId: source.providerSourceId.trim(),
  };
}

// ---------------------------------------------------------------------------
// Participant identity / state (mirrors participantIdentityFromJson,
// participantStateFromJson)
// ---------------------------------------------------------------------------

function readParticipantIdentityFields(
  object: Record<string, unknown>
): FieldResult<ParticipantIdentity> {
  const participantIdResult = readString(object, "participantId");
  if (!participantIdResult.ok) return errField(participantIdResult.error!);
  const displayNameResult = readString(object, "displayName");
  if (!displayNameResult.ok) return errField(displayNameResult.error!);
  const kindResult = readEnum(
    object,
    "identityKind",
    IDENTITY_KINDS,
    "identityKind"
  );
  if (!kindResult.ok) return errField(kindResult.error!);

  const participantId = participantIdResult.value!.trim();
  const displayName = displayNameResult.value!.trim();
  if (participantId.length === 0 || displayName.length === 0) {
    return errField("invalid participant identity");
  }

  return okField({
    participantId,
    displayName,
    identityKind: kindResult.value!,
  });
}

export function participantStateFromJson(
  object: unknown
): FieldResult<ParticipantState> {
  if (!isPlainObject(object)) {
    return errField("participant state must be an object");
  }

  const keysResult = exactKeys(
    object,
    [
      "participantId",
      "displayName",
      "identityKind",
      "joinOrder",
      "host",
      "connected",
      "ready",
      "syncStatus",
    ],
    "participant state"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const identityResult = readParticipantIdentityFields(object);
  if (!identityResult.ok) return errField(identityResult.error!);

  const joinOrderResult = readNonNegativeInteger(object, "joinOrder");
  if (!joinOrderResult.ok) return errField(joinOrderResult.error!);
  const hostResult = readBool(object, "host");
  if (!hostResult.ok) return errField(hostResult.error!);
  const connectedResult = readBool(object, "connected");
  if (!connectedResult.ok) return errField(connectedResult.error!);
  const readyResult = readBool(object, "ready");
  if (!readyResult.ok) return errField(readyResult.error!);
  const syncStatusResult = readEnum(
    object,
    "syncStatus",
    SYNC_STATUSES,
    "syncStatus"
  );
  if (!syncStatusResult.ok) return errField(syncStatusResult.error!);

  if (!readyResult.value! && syncStatusResult.value !== "unknown") {
    return errField("a non-ready participant must have unknown syncStatus");
  }

  return okField({
    ...identityResult.value!,
    joinOrder: joinOrderResult.value!,
    host: hostResult.value!,
    connected: connectedResult.value!,
    ready: readyResult.value!,
    syncStatus: syncStatusResult.value!,
  });
}

export function participantStateToJson(
  participant: ParticipantState
): Record<string, unknown> {
  return {
    participantId: participant.participantId,
    displayName: participant.displayName,
    identityKind: participant.identityKind,
    joinOrder: participant.joinOrder,
    host: participant.host,
    connected: participant.connected,
    ready: participant.ready,
    syncStatus: participant.syncStatus,
  };
}

// ---------------------------------------------------------------------------
// Timeline state / command (mirrors timelineStateFromJson,
// timelineCommandFromJson)
// ---------------------------------------------------------------------------

export function timelineStateFromJson(
  object: unknown
): FieldResult<TimelineState> {
  if (!isPlainObject(object)) {
    return errField("timeline state must be an object");
  }
  const keysResult = exactKeys(
    object,
    ["playing", "positionMs", "revision"],
    "timeline state"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const playingResult = readBool(object, "playing");
  if (!playingResult.ok) return errField(playingResult.error!);
  const positionResult = readNonNegativeInteger(object, "positionMs");
  if (!positionResult.ok) return errField(positionResult.error!);
  const revisionResult = readNonNegativeInteger(object, "revision");
  if (!revisionResult.ok) return errField(revisionResult.error!);

  return okField({
    playing: playingResult.value!,
    positionMs: positionResult.value!,
    revision: revisionResult.value!,
  });
}

export function timelineStateToJson(
  timeline: TimelineState
): Record<string, unknown> {
  return {
    playing: timeline.playing,
    positionMs: timeline.positionMs,
    revision: timeline.revision,
  };
}

export function timelineCommandFromJson(
  object: unknown
): FieldResult<TimelineCommand> {
  if (!isPlainObject(object)) {
    return errField("timeline command must be an object");
  }
  const keysResult = exactKeysWithOptional(
    object,
    ["command"],
    ["positionMs"],
    "timeline command"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const typeResult = readEnum(
    object,
    "command",
    TIMELINE_COMMAND_TYPES,
    "timeline command"
  );
  if (!typeResult.ok) return errField(typeResult.error!);

  let positionMs: number | undefined;
  if ("positionMs" in object) {
    const positionResult = readNonNegativeInteger(object, "positionMs");
    if (!positionResult.ok) return errField(positionResult.error!);
    positionMs = positionResult.value!;
  }

  if (typeResult.value === "seek" && positionMs === undefined) {
    return errField("seek requires positionMs");
  }

  const command: TimelineCommand = { command: typeResult.value! };
  if (positionMs !== undefined) command.positionMs = positionMs;
  return okField(command);
}

export function timelineCommandToJson(
  command: TimelineCommand
): Record<string, unknown> {
  const object: Record<string, unknown> = { command: command.command };
  if (command.positionMs !== undefined) {
    object.positionMs = command.positionMs;
  }
  return object;
}

// ---------------------------------------------------------------------------
// Chat / reaction events (mirrors chatEventFromJson, reactionEventFromJson)
// ---------------------------------------------------------------------------

export function chatEventFromJson(object: unknown): FieldResult<ChatEvent> {
  if (!isPlainObject(object)) {
    return errField("chat event must be an object");
  }
  const keysResult = exactKeys(
    object,
    ["sequence", "participantId", "displayName", "message"],
    "chat event"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const sequenceResult = readNonNegativeInteger(object, "sequence");
  if (!sequenceResult.ok) return errField(sequenceResult.error!);
  const participantIdResult = readString(object, "participantId");
  if (!participantIdResult.ok) return errField(participantIdResult.error!);
  const displayNameResult = readString(object, "displayName");
  if (!displayNameResult.ok) return errField(displayNameResult.error!);
  const messageResult = readString(object, "message", true);
  if (!messageResult.ok) return errField(messageResult.error!);

  return okField({
    sequence: sequenceResult.value!,
    participantId: participantIdResult.value!.trim(),
    displayName: displayNameResult.value!.trim(),
    message: messageResult.value!.trim(),
  });
}

export function chatEventToJson(event: ChatEvent): Record<string, unknown> {
  return {
    sequence: event.sequence,
    participantId: event.participantId,
    displayName: event.displayName,
    message: event.message,
  };
}

export function reactionEventFromJson(
  object: unknown
): FieldResult<ReactionEvent> {
  if (!isPlainObject(object)) {
    return errField("reaction event must be an object");
  }
  const keysResult = exactKeys(
    object,
    ["sequence", "participantId", "displayName", "reaction"],
    "reaction event"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const sequenceResult = readNonNegativeInteger(object, "sequence");
  if (!sequenceResult.ok) return errField(sequenceResult.error!);
  const participantIdResult = readString(object, "participantId");
  if (!participantIdResult.ok) return errField(participantIdResult.error!);
  const displayNameResult = readString(object, "displayName");
  if (!displayNameResult.ok) return errField(displayNameResult.error!);
  const reactionResult = readString(object, "reaction");
  if (!reactionResult.ok) return errField(reactionResult.error!);

  return okField({
    sequence: sequenceResult.value!,
    participantId: participantIdResult.value!.trim(),
    displayName: displayNameResult.value!.trim(),
    reaction: reactionResult.value!.trim(),
  });
}

export function reactionEventToJson(
  event: ReactionEvent
): Record<string, unknown> {
  return {
    sequence: event.sequence,
    participantId: event.participantId,
    displayName: event.displayName,
    reaction: event.reaction,
  };
}

// ---------------------------------------------------------------------------
// Room snapshot / session established (mirrors roomSnapshotFromJson,
// sessionEstablishedFromJson)
// ---------------------------------------------------------------------------

export function roomSnapshotFromJson(
  object: unknown,
  envelopeRoomId?: string
): FieldResult<RoomSnapshot> {
  if (!isPlainObject(object)) {
    return errField("room snapshot must be an object");
  }
  const keysResult = exactKeys(
    object,
    [
      "roomId",
      "hostParticipantId",
      "source",
      "controlMode",
      "timeline",
      "participants",
      "hostReconnectDeadlineMs",
    ],
    "room snapshot"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const roomIdResult = readString(object, "roomId");
  if (!roomIdResult.ok) return errField(roomIdResult.error!);
  const hostParticipantIdResult = readString(object, "hostParticipantId");
  if (!hostParticipantIdResult.ok)
    return errField(hostParticipantIdResult.error!);

  const sourceResult = sourceDescriptorFromJson(object.source);
  if (!sourceResult.ok) return errField(sourceResult.error!);

  const controlModeResult = readEnum(
    object,
    "controlMode",
    CONTROL_MODES,
    "controlMode"
  );
  if (!controlModeResult.ok) return errField(controlModeResult.error!);

  const timelineResult = timelineStateFromJson(object.timeline);
  if (!timelineResult.ok) return errField(timelineResult.error!);

  if (!Array.isArray(object.participants)) {
    return errField("participants must be an array");
  }
  const rows = object.participants as unknown[];
  if (rows.length === 0 || rows.length > MAX_PARTICIPANTS) {
    return errField(
      `participants must contain 1..${MAX_PARTICIPANTS} rows`
    );
  }

  const roomId = roomIdResult.value!.trim();
  const hostParticipantId = hostParticipantIdResult.value!.trim();

  let hostCount = 0;
  const participantIds = new Set<string>();
  const joinOrders = new Set<number>();
  const participants: ParticipantState[] = [];

  for (const row of rows) {
    const participantResult = participantStateFromJson(row);
    if (!participantResult.ok) return errField(participantResult.error!);
    const participant = participantResult.value!;

    if (participantIds.has(participant.participantId)) {
      return errField("participantId values must be unique");
    }
    participantIds.add(participant.participantId);

    if (joinOrders.has(participant.joinOrder)) {
      return errField("joinOrder values must be unique");
    }
    joinOrders.add(participant.joinOrder);

    if (participant.host) {
      hostCount += 1;
      if (participant.participantId !== hostParticipantId) {
        return errField("host row does not match hostParticipantId");
      }
      if (participant.identityKind !== "signedIn") {
        return errField("host must be signed in");
      }
    }

    participants.push(participant);
  }

  if (hostCount !== 1) {
    return errField("snapshot must contain exactly one host");
  }

  const deadlineResult = readSnapshotDeadline(object);
  if (!deadlineResult.ok) return errField(deadlineResult.error!);

  if (envelopeRoomId !== undefined && roomId !== envelopeRoomId.trim()) {
    return errField("roomSnapshot roomId does not match envelope roomId");
  }

  return okField({
    roomId,
    hostParticipantId,
    source: sourceResult.value!,
    controlMode: controlModeResult.value!,
    timeline: timelineResult.value!,
    participants,
    hostReconnectDeadlineMs: deadlineResult.value!,
  });
}

function readSnapshotDeadline(
  object: Record<string, unknown>
): FieldResult<number> {
  const raw = object.hostReconnectDeadlineMs;
  if (typeof raw !== "number" || !Number.isFinite(raw) || !Number.isInteger(raw)) {
    return errField("hostReconnectDeadlineMs must be an integer");
  }
  if (raw < -1) {
    return errField(
      "hostReconnectDeadlineMs must be -1 or a non-negative integer"
    );
  }
  return okField(raw);
}

export function roomSnapshotToJson(
  snapshot: RoomSnapshot
): Record<string, unknown> {
  return {
    roomId: snapshot.roomId,
    hostParticipantId: snapshot.hostParticipantId,
    source: sourceDescriptorToJson(snapshot.source),
    controlMode: snapshot.controlMode,
    timeline: timelineStateToJson(snapshot.timeline),
    participants: snapshot.participants.map(participantStateToJson),
    hostReconnectDeadlineMs: snapshot.hostReconnectDeadlineMs,
  };
}

export function sessionEstablishedFromJson(
  object: unknown
): FieldResult<SessionEstablished> {
  if (!isPlainObject(object)) {
    return errField("sessionEstablished payload must be an object");
  }
  const keysResult = exactKeys(
    object,
    ["participantId", "reconnectToken"],
    "sessionEstablished payload"
  );
  if (!keysResult.ok) return errField(keysResult.error!);

  const participantIdResult = readString(object, "participantId");
  if (!participantIdResult.ok) return errField(participantIdResult.error!);
  const reconnectTokenResult = readString(object, "reconnectToken");
  if (!reconnectTokenResult.ok) return errField(reconnectTokenResult.error!);

  return okField({
    participantId: participantIdResult.value!.trim(),
    reconnectToken: reconnectTokenResult.value!.trim(),
  });
}

export function sessionEstablishedToJson(
  session: SessionEstablished
): Record<string, unknown> {
  return {
    participantId: session.participantId,
    reconnectToken: session.reconnectToken,
  };
}

// ---------------------------------------------------------------------------
// Envelope-level require helpers (mirrors requireRoom/requireSender/
// requireNoRoom/requireNoSender)
// ---------------------------------------------------------------------------

function requireRoom(message: ProtocolMessage): FieldResult<true> {
  if (message.roomId.trim().length === 0) {
    return errField("roomId must not be empty");
  }
  return okField(true);
}

function requireSender(message: ProtocolMessage): FieldResult<true> {
  if (message.senderId.trim().length === 0) {
    return errField("senderId must not be empty");
  }
  return okField(true);
}

function requireNoRoom(message: ProtocolMessage): FieldResult<true> {
  if (message.roomId.length !== 0) {
    return errField("roomId must be empty");
  }
  return okField(true);
}

function requireNoSender(message: ProtocolMessage): FieldResult<true> {
  if (message.senderId.length !== 0) {
    return errField("senderId must be empty");
  }
  return okField(true);
}

// ---------------------------------------------------------------------------
// Per-type payload validation (mirrors validateClientMessage /
// validateServerMessage)
// ---------------------------------------------------------------------------

interface PayloadValidation {
  ok: boolean;
  error?: string;
  /** Set true when the failure is specifically an invalid source descriptor. */
  isSourceError?: boolean;
}

function okPayload(): PayloadValidation {
  return { ok: true };
}

function errPayload(error: string, isSourceError = false): PayloadValidation {
  return { ok: false, error, isSourceError };
}

function validateClientMessage(message: ProtocolMessage): PayloadValidation {
  const { type, payload } = message;

  switch (type) {
    case "createRoom": {
      const noRoom = requireNoRoom(message);
      if (!noRoom.ok) return errPayload(noRoom.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);

      const keysResult = exactKeys(payload, ["source"], "createRoom payload");
      if (!keysResult.ok) return errPayload(keysResult.error!);

      const sourceResult = sourceDescriptorFromJson(payload.source);
      if (!sourceResult.ok) return errPayload(sourceResult.error!, true);
      return okPayload();
    }

    case "joinRoom": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);

      const kindResult = readEnum(
        payload,
        "identityKind",
        IDENTITY_KINDS,
        "identityKind"
      );
      if (!kindResult.ok) return errPayload(kindResult.error!);

      if (kindResult.value === "signedIn") {
        const keysResult = exactKeys(
          payload,
          ["identityKind"],
          "signed-in join payload"
        );
        if (!keysResult.ok) return errPayload(keysResult.error!);
        return okPayload();
      }

      const keysResult = exactKeys(
        payload,
        ["identityKind", "displayName"],
        "guest join payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const displayNameResult = readString(payload, "displayName");
      if (!displayNameResult.ok) return errPayload(displayNameResult.error!);
      return okPayload();
    }

    case "reconnectRoom": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);

      const keysResult = exactKeys(
        payload,
        ["reconnectToken"],
        "reconnectRoom payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const tokenResult = readString(payload, "reconnectToken");
      if (!tokenResult.ok) return errPayload(tokenResult.error!);
      return okPayload();
    }

    case "leaveRoom": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);
      const emptyResult = requireEmptyPayload(payload, "leaveRoom");
      if (!emptyResult.ok) return errPayload(emptyResult.error!);
      return okPayload();
    }

    case "timelineCommand": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);
      const commandResult = timelineCommandFromJson(payload);
      if (!commandResult.ok) return errPayload(commandResult.error!);
      return okPayload();
    }

    case "setControlMode": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);

      const keysResult = exactKeys(
        payload,
        ["controlMode"],
        "setControlMode payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const modeResult = readEnum(
        payload,
        "controlMode",
        CONTROL_MODES,
        "controlMode"
      );
      if (!modeResult.ok) return errPayload(modeResult.error!);
      return okPayload();
    }

    case "participantState": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);

      const keysResult = exactKeys(
        payload,
        ["ready", "syncStatus"],
        "participantState request payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);

      const readyResult = readBool(payload, "ready");
      if (!readyResult.ok) return errPayload(readyResult.error!);
      const statusResult = readEnum(
        payload,
        "syncStatus",
        SYNC_STATUSES,
        "syncStatus"
      );
      if (!statusResult.ok) return errPayload(statusResult.error!);

      if (!readyResult.value! && statusResult.value !== "unknown") {
        return errPayload(
          "a non-ready participant must have unknown syncStatus"
        );
      }
      return okPayload();
    }

    case "removeParticipant": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);

      const keysResult = exactKeys(
        payload,
        ["participantId"],
        "removeParticipant payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const idResult = readString(payload, "participantId");
      if (!idResult.ok) return errPayload(idResult.error!);
      return okPayload();
    }

    case "chat": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);

      const keysResult = exactKeys(
        payload,
        ["message"],
        "chat request payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const textResult = readString(payload, "message");
      if (!textResult.ok) return errPayload(textResult.error!);
      return okPayload();
    }

    case "reaction": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);

      const keysResult = exactKeys(
        payload,
        ["reaction"],
        "reaction request payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const reactionResult = readString(payload, "reaction");
      if (!reactionResult.ok) return errPayload(reactionResult.error!);
      return okPayload();
    }

    case "endRoom": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const sender = requireSender(message);
      if (!sender.ok) return errPayload(sender.error!);
      const emptyResult = requireEmptyPayload(payload, "endRoom");
      if (!emptyResult.ok) return errPayload(emptyResult.error!);
      return okPayload();
    }

    default:
      return errPayload(
        `message type '${type}' is server-to-client only`
      );
  }
}

function validateServerMessage(message: ProtocolMessage): PayloadValidation {
  const { type, payload } = message;

  switch (type) {
    case "sessionEstablished": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = sessionEstablishedFromJson(payload);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "roomSnapshot": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = roomSnapshotFromJson(payload, message.roomId);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "timelineState": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = timelineStateFromJson(payload);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "participantState": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = participantStateFromJson(payload);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "hostChanged": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);

      const keysResult = exactKeys(
        payload,
        ["hostParticipantId"],
        "hostChanged payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const idResult = readString(payload, "hostParticipantId");
      if (!idResult.ok) return errPayload(idResult.error!);
      return okPayload();
    }

    case "chat": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = chatEventFromJson(payload);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "reaction": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const result = reactionEventFromJson(payload);
      if (!result.ok) return errPayload(result.error!);
      return okPayload();
    }

    case "roomEnded": {
      const room = requireRoom(message);
      if (!room.ok) return errPayload(room.error!);
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);
      const emptyResult = requireEmptyPayload(payload, "roomEnded");
      if (!emptyResult.ok) return errPayload(emptyResult.error!);
      return okPayload();
    }

    case "error": {
      const noSender = requireNoSender(message);
      if (!noSender.ok) return errPayload(noSender.error!);

      const keysResult = exactKeys(
        payload,
        ["code", "message"],
        "error payload"
      );
      if (!keysResult.ok) return errPayload(keysResult.error!);
      const codeResult = readString(payload, "code");
      if (!codeResult.ok) return errPayload(codeResult.error!);
      const messageResult = readString(payload, "message", true);
      if (!messageResult.ok) return errPayload(messageResult.error!);
      return okPayload();
    }

    default:
      return errPayload(
        `message type '${type}' is client-to-server only`
      );
  }
}

// ---------------------------------------------------------------------------
// Envelope parse (mirrors decodeMessage) + full validate (mirrors
// validateMessage)
// ---------------------------------------------------------------------------

const ENVELOPE_KEYS = [
  "version",
  "type",
  "roomId",
  "senderId",
  "sequence",
  "payload",
] as const;

/**
 * Byte-size check ahead of any parsing, mirroring the client's ordering in
 * decodeMessage (size checked before QJsonDocument::fromJson is ever
 * called). Callers should invoke this on the RAW wire bytes (UTF-8 encoded)
 * before calling parseEnvelope.
 */
export function exceedsWireLimit(raw: string | ArrayBuffer | Uint8Array): boolean {
  let byteLength: number;
  if (typeof raw === "string") {
    byteLength = new TextEncoder().encode(raw).length;
  } else if (raw instanceof ArrayBuffer) {
    byteLength = raw.byteLength;
  } else {
    byteLength = raw.byteLength;
  }
  return byteLength > MAX_WIRE_MESSAGE_BYTES;
}

/**
 * Parses and fully validates one wire message (envelope + direction-specific
 * payload schema) in one pass. This is the entry point the DO/Worker code
 * uses on every inbound frame.
 */
export function parseMessage(
  raw: string | ArrayBuffer | Uint8Array,
  direction: MessageDirection
): ParseResult {
  if (exceedsWireLimit(raw)) {
    return fail(
      "invalid_message",
      `message exceeds ${MAX_WIRE_MESSAGE_BYTES}-byte (64 KiB) wire limit`
    );
  }

  const text =
    typeof raw === "string" ? raw : new TextDecoder().decode(raw as ArrayBuffer | Uint8Array);

  let parsedJson: unknown;
  try {
    parsedJson = JSON.parse(text);
  } catch (err) {
    return fail(
      "invalid_message",
      `invalid JSON object: ${err instanceof Error ? err.message : String(err)}`
    );
  }

  if (!isPlainObject(parsedJson)) {
    return fail("invalid_message", "invalid JSON object: not an object");
  }

  const envelopeKeysResult = exactKeys(
    parsedJson,
    ENVELOPE_KEYS,
    "protocol envelope"
  );
  if (!envelopeKeysResult.ok) {
    return fail("invalid_message", envelopeKeysResult.error!);
  }

  const versionRaw = parsedJson.version;
  if (typeof versionRaw !== "number" || !Number.isInteger(versionRaw)) {
    return fail("invalid_message", "version must be an integer");
  }
  if (versionRaw !== PROTOCOL_VERSION) {
    return fail(
      "protocol_version_mismatch",
      `unsupported protocol version ${versionRaw}; expected ${PROTOCOL_VERSION}`
    );
  }

  const typeRaw = parsedJson.type;
  if (typeof typeRaw !== "string") {
    return fail("invalid_message", "type must be a string");
  }
  if (!ALL_MESSAGE_TYPES.has(typeRaw)) {
    return fail("invalid_message", `unknown message type '${typeRaw}'`);
  }
  const type = typeRaw as MessageType;

  const roomIdResult = readString(parsedJson, "roomId", true);
  if (!roomIdResult.ok) {
    return fail("invalid_message", roomIdResult.error!);
  }
  const senderIdResult = readString(parsedJson, "senderId", true);
  if (!senderIdResult.ok) {
    return fail("invalid_message", senderIdResult.error!);
  }
  const sequenceResult = readNonNegativeInteger(parsedJson, "sequence");
  if (!sequenceResult.ok) {
    return fail("invalid_message", sequenceResult.error!);
  }

  const payloadRaw = parsedJson.payload;
  if (!isPlainObject(payloadRaw)) {
    return fail("invalid_message", "payload must be an object");
  }

  const message: ProtocolMessage = {
    version: versionRaw,
    type,
    roomId: roomIdResult.value!.trim(),
    senderId: senderIdResult.value!.trim(),
    sequence: sequenceResult.value!,
    payload: payloadRaw,
  };

  // Some type names (chat, reaction, participantState) are valid message
  // types in BOTH directions with different payload shapes (client request
  // vs. server broadcast/event) — mirroring the client's single MessageType
  // enum shared across MessageDirection::ClientToServer/ServerToClient. The
  // per-direction validator switches below are the authority on whether a
  // given type is acceptable for the requested direction; a type absent from
  // the relevant switch falls through to that validator's own default case
  // ("... is client-to-server only" / "... is server-to-client only"),
  // exactly mirroring validateClientMessage/validateServerMessage in
  // WatchPartyProtocol.cpp. No separate membership pre-check is needed (and
  // one keyed only on CLIENT_TO_SERVER_TYPES would wrongly reject valid
  // server-side chat/reaction/participantState messages).
  const payloadValidation =
    direction === "clientToServer"
      ? validateClientMessage(message)
      : validateServerMessage(message);

  if (!payloadValidation.ok) {
    const code: ErrorCode = payloadValidation.isSourceError
      ? "invalid_source"
      : "invalid_message";
    return fail(code, payloadValidation.error!);
  }

  return { ok: true, message };
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------

/**
 * Serializes an already-valid ProtocolMessage back to wire JSON. Callers
 * that build outbound (server -> client) messages from typed payload
 * builders (buildRoomSnapshotMessage, buildErrorMessage, ...) below get an
 * envelope shape guaranteed to round-trip through parseMessage.
 */
export function serializeMessage(message: ProtocolMessage): string {
  return JSON.stringify({
    version: message.version,
    type: message.type,
    roomId: message.roomId,
    senderId: message.senderId,
    sequence: message.sequence,
    payload: message.payload,
  });
}

// ---------------------------------------------------------------------------
// Typed envelope builders for server -> client messages (used by room-do.ts
// and by tests to build fixtures/replies without hand-assembling payload
// objects).
// ---------------------------------------------------------------------------

function baseEnvelope(
  type: ServerToClientType,
  roomId: string,
  sequence = 0
): Omit<ProtocolMessage, "payload"> {
  return { version: PROTOCOL_VERSION, type, roomId, senderId: "", sequence };
}

export function buildErrorMessage(
  code: ErrorCode,
  message = "",
  roomId = ""
): ProtocolMessage {
  return {
    ...baseEnvelope("error", roomId),
    payload: { code, message },
  };
}

export function buildSessionEstablishedMessage(
  roomId: string,
  session: SessionEstablished,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("sessionEstablished", roomId, sequence),
    payload: sessionEstablishedToJson(session),
  };
}

export function buildRoomSnapshotMessage(
  snapshot: RoomSnapshot,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("roomSnapshot", snapshot.roomId, sequence),
    payload: roomSnapshotToJson(snapshot),
  };
}

export function buildTimelineStateMessage(
  roomId: string,
  timeline: TimelineState,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("timelineState", roomId, sequence),
    payload: timelineStateToJson(timeline),
  };
}

export function buildParticipantStateMessage(
  roomId: string,
  participant: ParticipantState,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("participantState", roomId, sequence),
    payload: participantStateToJson(participant),
  };
}

export function buildHostChangedMessage(
  roomId: string,
  hostParticipantId: string,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("hostChanged", roomId, sequence),
    payload: { hostParticipantId },
  };
}

export function buildChatMessage(
  roomId: string,
  event: ChatEvent,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("chat", roomId, sequence),
    payload: chatEventToJson(event),
  };
}

export function buildReactionMessage(
  roomId: string,
  event: ReactionEvent,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("reaction", roomId, sequence),
    payload: reactionEventToJson(event),
  };
}

export function buildRoomEndedMessage(
  roomId: string,
  sequence = 0
): ProtocolMessage {
  return {
    ...baseEnvelope("roomEnded", roomId, sequence),
    payload: {},
  };
}

export { isErrorCode };
