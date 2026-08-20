// RoomDO — Slice 1 scaffold only.
// Accepts the WebSocket, logs the upgrade headers, and replies to any text
// frame it receives with a valid protocol-v3 `error` envelope using the
// frozen wire code `protocol_version_mismatch` (the desktop client maps this
// to its `protocolVersionMismatch` error category — see
// native/watchparty/WatchPartyTransport.cpp). This is deliberately the ONLY
// behavior this slice implements; no room/session logic exists yet.
//
// Contract reference (frozen):
// C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/03-watch-party/
// watch-party-qml-slice-08/SERVER-PROTOCOL-CONTRACT.md
//   - "Protocol envelope" (top-level shape, exact keys)
//   - "error" (server -> client payload: { code, message })

interface ProtocolEnvelope {
  version: number;
  type: string;
  roomId: string;
  senderId: string;
  sequence: number;
  payload: Record<string, unknown>;
}

function buildErrorEnvelope(message: string): ProtocolEnvelope {
  return {
    version: 3,
    type: "error",
    roomId: "",
    senderId: "",
    sequence: 0,
    payload: {
      code: "protocol_version_mismatch",
      message,
    },
  };
}

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
      const envelope = buildErrorEnvelope(
        "Slice 1 scaffold: no room logic implemented yet"
      );
      try {
        server.send(JSON.stringify(envelope));
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
