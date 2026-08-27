// Conformance suite for src/protocol.ts (Slice 2 of
// docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md).
//
// Fixtures under test/fixtures/valid/*.json are transcribed from
// SERVER-PROTOCOL-CONTRACT.md's own message examples, cross-checked against
// native/watchparty/WatchPartyProtocol.cpp (the conformance oracle — the
// client is frozen and shipped, so where prose and code could be read two
// ways the client wins). Every valid fixture must parse -> serialize ->
// parse to a stable, identical message. Every fixture under
// test/fixtures/invalid/*.json must be rejected with the expected typed
// error category (the category string, not just "throws").

import { describe, expect, it } from "vitest";
import {
  CLIENT_TO_SERVER_TYPES,
  MAX_WIRE_MESSAGE_BYTES,
  parseMessage,
  PROTOCOL_VERSION,
  serializeMessage,
  type ErrorCode,
  type MessageDirection,
  type ProtocolMessage,
} from "../src/protocol";

// Vite/vitest-pool-workers bundle JSON imports at test-build time (no
// runtime fs access inside workerd), so fixtures are gathered via
// import.meta.glob rather than read from disk at test run.
const validModules = import.meta.glob("./fixtures/valid/*.json", {
  eager: true,
}) as Record<string, { default: Record<string, unknown> }>;

const invalidModules = import.meta.glob("./fixtures/invalid/*.json", {
  eager: true,
}) as Record<string, { default: Record<string, unknown> }>;

function fixtureName(path: string): string {
  const match = path.match(/([^/]+)\.json$/);
  return match ? match[1] : path;
}

// chat/reaction/participantState are valid MESSAGE TYPES in both
// directions (client request vs. server event/broadcast — see
// WatchPartyProtocol.cpp's validateClientMessage/validateServerMessage,
// which both have cases for these three types with different payload
// shapes). The type name alone cannot disambiguate direction for those
// three; the fixture file names carry an explicit -request/-broadcast vs.
// -event suffix for exactly this reason, and this test derives direction
// from that suffix. Every other message type has an unambiguous direction
// derived from CLIENT_TO_SERVER_TYPES membership.
function directionFor(name: string, type: string): MessageDirection {
  if (name.endsWith("-event") || name.endsWith("-broadcast")) {
    return "serverToClient";
  }
  if (name.endsWith("-request")) {
    return "clientToServer";
  }
  return (CLIENT_TO_SERVER_TYPES as readonly string[]).includes(type)
    ? "clientToServer"
    : "serverToClient";
}

const validFixtures = Object.entries(validModules).map(([path, mod]) => ({
  name: fixtureName(path),
  json: mod.default,
}));

describe("protocol conformance — valid fixtures round-trip", () => {
  it.each(validFixtures)(
    "$name: parse -> serialize -> parse is stable",
    ({ name, json }) => {
      const raw = JSON.stringify(json);
      const type = (json as { type: string }).type;
      const direction = directionFor(name, type);

      const first = parseMessage(raw, direction);
      expect(first.ok, `first parse failed: ${!first.ok ? first.error : ""}`)
        .toBe(true);
      if (!first.ok) return;

      const wire = serializeMessage(first.message);
      const second = parseMessage(wire, direction);
      expect(
        second.ok,
        `re-parse of serialized output failed: ${!second.ok ? second.error : ""}`
      ).toBe(true);
      if (!second.ok) return;

      expect(second.message).toEqual(first.message);
    }
  );

  it("covers every client->server message type at least once", () => {
    const clientTypes = new Set(
      validFixtures
        .map((f) => (f.json as { type: string }).type)
        .filter((type) =>
          (CLIENT_TO_SERVER_TYPES as readonly string[]).includes(type)
        )
    );
    for (const type of CLIENT_TO_SERVER_TYPES) {
      expect(clientTypes.has(type), `missing client fixture for ${type}`).toBe(
        true
      );
    }
  });

  it("covers every server->client message type at least once", () => {
    // chat/reaction/participantState are dual-direction; every fixture name
    // for those carries a -request/-event/-broadcast suffix, so just assert
    // the full server vocabulary is present among fixture types.
    const allTypes = new Set(
      validFixtures.map((f) => (f.json as { type: string }).type)
    );
    for (const type of [
      "sessionEstablished",
      "roomSnapshot",
      "timelineState",
      "participantState",
      "hostChanged",
      "chat",
      "reaction",
      "roomEnded",
      "error",
    ]) {
      expect(allTypes.has(type), `missing server fixture for ${type}`).toBe(
        true
      );
    }
  });
});

describe("protocol conformance — invalid fixtures are rejected typed", () => {
  it("envelope-unknown-key.json rejects invalid_message (extra top-level key)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/envelope-unknown-key.json"].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("envelope-missing-required.json rejects invalid_message (missing payload key)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/envelope-missing-required.json"]
        .default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("envelope-wrong-type-sequence.json rejects invalid_message (sequence not a number)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/envelope-wrong-type-sequence.json"]
        .default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("envelope-bad-version.json rejects protocol_version_mismatch", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/envelope-bad-version.json"].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok)
      expect(result.code).toBe("protocol_version_mismatch" satisfies ErrorCode);
  });

  it("createRoom-bad-source-kind.json rejects invalid_source (unknown source kind)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/createRoom-bad-source-kind.json"]
        .default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_source" satisfies ErrorCode);
  });

  it("payload-unknown-key.json rejects invalid_message (extra payload key)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/payload-unknown-key.json"].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("payload-missing-required.json rejects invalid_message (missing reconnectToken)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/payload-missing-required.json"]
        .default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("payload-wrong-type.json rejects invalid_message (positionMs as string)", () => {
    const raw = JSON.stringify(
      invalidModules["./fixtures/invalid/payload-wrong-type.json"].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("direction-server-message-from-client.json rejects invalid_message (server-only type sent as client->server)", () => {
    const raw = JSON.stringify(
      invalidModules[
        "./fixtures/invalid/direction-server-message-from-client.json"
      ].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("a client-only type sent as server->client is rejected invalid_message", () => {
    // leaveRoom is client-to-server only; feeding it through with direction
    // 'serverToClient' must fall through validateServerMessage's default.
    const raw = JSON.stringify({
      version: PROTOCOL_VERSION,
      type: "leaveRoom",
      roomId: "room-abc123",
      senderId: "participant-1",
      sequence: 2,
      payload: {},
    });
    const result = parseMessage(raw, "serverToClient");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });

  it("oversize marker: a message beyond the 64 KiB wire ceiling is rejected before JSON parsing", () => {
    const base = validModules["./fixtures/valid/chat-request.json"]
      .default as unknown as ProtocolMessage;
    const padded = {
      ...base,
      payload: { message: "x".repeat(MAX_WIRE_MESSAGE_BYTES + 1) },
    };
    const raw = JSON.stringify(padded);
    expect(raw.length).toBeGreaterThan(MAX_WIRE_MESSAGE_BYTES);

    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.code).toBe("invalid_message" satisfies ErrorCode);
      expect(result.error).toContain("64");
    }
  });

  it("a dual-direction type (chat) with the client request shape is accepted server->client only when its shape matches the server event schema", () => {
    // chat-request.json is the CLIENT shape ({message}); parsing it with
    // direction 'serverToClient' must fail because the server chat schema
    // requires {sequence, participantId, displayName, message}.
    const raw = JSON.stringify(
      validModules["./fixtures/valid/chat-request.json"].default
    );
    const result = parseMessage(raw, "serverToClient");
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe("invalid_message" satisfies ErrorCode);
  });
});

describe("protocol conformance — negative-control support (see README for the flip-one-guard transcript)", () => {
  it("baseline sanity: a known-good envelope round-trips (control for the negative-control drill)", () => {
    const raw = JSON.stringify(
      validModules["./fixtures/valid/leaveRoom.json"].default
    );
    const result = parseMessage(raw, "clientToServer");
    expect(result.ok).toBe(true);
  });
});
