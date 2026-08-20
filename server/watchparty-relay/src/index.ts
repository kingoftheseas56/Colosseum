// Colosseum Watch Party relay — Slice 1 scaffold.
// Purpose (this slice only): prove a real WSS handshake with real certificate
// verification between a production colosseum.exe and a Cloudflare Workers
// relay running locally. No room logic yet — see docs/superpowers/plans/
// 2026-08-20-watch-party-relay-plan.md Slice 1.

import type { Env } from "./auth";

export type { Env } from "./auth";

const PROTOCOL_HEADER = "X-Colosseum-Watch-Party-Protocol";
const REQUIRED_PROTOCOL_VERSION = "3";

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const upgrade = request.headers.get("Upgrade");
    if (!upgrade || upgrade.toLowerCase() !== "websocket") {
      return new Response("Colosseum Watch Party relay (Slice 1 scaffold)", {
        status: 200,
      });
    }

    const protocolHeader = request.headers.get(PROTOCOL_HEADER);
    if (protocolHeader !== REQUIRED_PROTOCOL_VERSION) {
      // Reject the upgrade outright before it ever reaches a Durable Object.
      return new Response("missing or unsupported watch party protocol header", {
        status: 426,
        headers: { "Content-Type": "text/plain" },
      });
    }

    // Route every room to a Durable Object instance. This scaffold slice has
    // no room identity semantics yet — any path lands on the same DO id so
    // the transport spike stays trivial.
    const id = env.ROOMS.idFromName("slice1-spike-room");
    const stub = env.ROOMS.get(id);
    return stub.fetch(request);
  },
};

export { RoomDO } from "./room-do";
