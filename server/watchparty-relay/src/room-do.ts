// RoomDO — Slice 1 transport scaffold, now speaking through the Slice 2
// protocol core (src/protocol.ts). Behavior is UNCHANGED from Slice 1: any
// inbound text frame still gets exactly one protocol-v3 `error` envelope
// back (wire code `protocol_version_mismatch`, the frozen code the desktop
// client maps to its `protocolVersionMismatch` error category — see
// native/watchparty/WatchPartyRoomServiceClient.cpp) and the socket closes.
// No room/session logic exists yet — that is Slice 3.
//
// The only change this slice makes here: the reply envelope is now built
// and serialized via buildErrorMessage()/serializeMessage() instead of a
// hand-rolled object literal, so the DO's own output is provably a
// protocol.ts-conformant message (round-trippable through parseMessage).
//
// Contract reference (frozen):
// C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/03-watch-party/
// watch-party-qml-slice-08/SERVER-PROTOCOL-CONTRACT.md
//   - "Protocol envelope" (top-level shape, exact keys)
//   - "error" (server -> client payload: { code, message })

import { buildErrorMessage, serializeMessage } from "./protocol";

export class RoomDO {
  state: DurableObjectState;

  constructor(state: DurableObjectState) {
    this.state = state;
  }

  async fetch(request: Request): Promise<Response> {
    const upgrade = request.headers.get("Upgrade");
    if (!upgrade || upgrade.toLowerCase() !== "websocket") {
      return new Response("expected websocket upgrade", { status: 400 });
    }

    // Log the upgrade headers for the relay-side cross-check the plan asks
    // for (proof the app's connection carried the protocol header).
    const headerLog: Record<string, string> = {};
    for (const [key, value] of request.headers.entries()) {
      headerLog[key] = value;
    }
    console.log(
      "watchparty-relay RoomDO upgrade headers:",
      JSON.stringify(headerLog)
    );

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair) as [WebSocket, WebSocket];

    server.accept();

    server.addEventListener("message", (event: MessageEvent) => {
      console.log("watchparty-relay RoomDO received frame, replying with error+close");
      const envelope = buildErrorMessage(
        "protocol_version_mismatch",
        "Slice 1/2 scaffold: no room logic implemented yet"
      );
      try {
        server.send(serializeMessage(envelope));
      } catch (err) {
        console.log("watchparty-relay RoomDO send failed:", String(err));
      }
      server.close(1000, "slice1-scaffold-echo-complete");
    });

    server.addEventListener("close", (event: CloseEvent) => {
      console.log(
        `watchparty-relay RoomDO socket closed code=${event.code} reason=${event.reason}`
      );
    });

    return new Response(null, { status: 101, webSocket: client });
  }
}
