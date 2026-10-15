// Node probe for the Slice 1 transport spike (still exercised as a
// regression check by later slices — see docs/superpowers/plans/
// 2026-08-20-watch-party-relay-plan.md Slice 3+).
// Connects to the relay over wss:// (or ws:// if RELAY_URL says so), sends
// the protocol header, and asserts:
//   1. WITH header "3": upgrade succeeds, and a frame carrying a
//      wire-level `version: 2` gets back a valid protocol-v3 `error`
//      envelope (code protocol_version_mismatch) before the socket closes
//      1000 — this is the one case protocol.ts treats as connection-
//      terminal (SERVER-PROTOCOL-CONTRACT.md "protocol/version mismatch is
//      terminal for that connection"). Slice 1/2's scaffold used to echo
//      this same code for ANY frame (no room logic existed yet); Slice 3's
//      real per-type dispatch made that blanket behavior go away, so this
//      probe now sends the one frame shape that is still genuinely
//      supposed to close the socket, instead of an arbitrary bogus
//      `type: "probe"` frame (which now correctly gets a non-terminal
//      `invalid_message` reply and stays connected).
//   2. WITHOUT the header (or with a wrong version "2"): the upgrade itself
//      is refused (HTTP 426, never reaches websocket open).
//
// Usage:
//   node test/probe-handshake.mjs
// Env:
//   RELAY_URL              default wss://localhost:8787
//   NODE_EXTRA_CA_CERTS     path to the local dev CA pem (required for wss
//                           against the self-issued cert; node does not read
//                           the Windows certificate store)

import WebSocket from "ws";

const RELAY_URL = process.env.RELAY_URL || "wss://localhost:8787";
const PROTOCOL_HEADER = "X-Colosseum-Watch-Party-Protocol";

function connect(headerValue) {
  return new Promise((resolve) => {
    const headers = {};
    if (headerValue !== undefined) {
      headers[PROTOCOL_HEADER] = headerValue;
    }

    const ws = new WebSocket(RELAY_URL, { headers });
    const result = {
      opened: false,
      upgradeStatusCode: null,
      closeCode: null,
      closeReason: null,
      errorFrame: null,
      unexpectedError: null,
    };

    ws.on("unexpected-response", (_req, res) => {
      result.upgradeStatusCode = res.statusCode;
      resolve(result);
    });

    ws.on("open", () => {
      result.opened = true;
      result.upgradeStatusCode = 101;
      if (headerValue === "3") {
        ws.send(
          JSON.stringify({
            version: 2,
            type: "createRoom",
            roomId: "",
            senderId: "",
            sequence: 1,
            payload: {},
          })
        );
      }
    });

    ws.on("message", (data) => {
      try {
        result.errorFrame = JSON.parse(data.toString());
      } catch {
        result.errorFrame = { parseError: true, raw: data.toString() };
      }
    });

    ws.on("close", (code, reason) => {
      result.closeCode = code;
      result.closeReason = reason.toString();
      resolve(result);
    });

    ws.on("error", (err) => {
      result.unexpectedError = String(err && err.message ? err.message : err);
      // 'close' or 'unexpected-response' still follow for most failure modes;
      // guard with a timeout in case neither fires.
      setTimeout(() => resolve(result), 500);
    });
  });
}

async function main() {
  let pass = true;

  console.log(`PROBE relay=${RELAY_URL}`);

  // Case 1: correct protocol header -> upgrade succeeds, error frame comes back.
  const withHeader = await connect("3");
  if (withHeader.opened && withHeader.errorFrame && withHeader.errorFrame.type === "error" &&
      withHeader.errorFrame.version === 3 &&
      withHeader.errorFrame.payload &&
      withHeader.errorFrame.payload.code === "protocol_version_mismatch") {
    console.log(`PROBE_OK case=with-header-3 upgrade=${withHeader.upgradeStatusCode} closeCode=${withHeader.closeCode} errorCode=${withHeader.errorFrame.payload.code}`);
  } else {
    pass = false;
    console.log(`PROBE_FAIL case=with-header-3 detail=${JSON.stringify(withHeader)}`);
  }

  // Case 2: wrong protocol header ("2") -> upgrade refused (426), never opens.
  const wrongHeader = await connect("2");
  if (!wrongHeader.opened && wrongHeader.upgradeStatusCode === 426) {
    console.log(`PROBE_OK case=wrong-header-2 upgradeStatusCode=${wrongHeader.upgradeStatusCode}`);
  } else {
    pass = false;
    console.log(`PROBE_FAIL case=wrong-header-2 detail=${JSON.stringify(wrongHeader)}`);
  }

  // Case 3: missing header entirely -> upgrade refused (426).
  const noHeader = await connect(undefined);
  if (!noHeader.opened && noHeader.upgradeStatusCode === 426) {
    console.log(`PROBE_OK case=no-header upgradeStatusCode=${noHeader.upgradeStatusCode}`);
  } else {
    pass = false;
    console.log(`PROBE_FAIL case=no-header detail=${JSON.stringify(noHeader)}`);
  }

  console.log(pass ? "PROBE_OK overall=pass" : "PROBE_FAIL overall=fail");
  process.exit(pass ? 0 : 1);
}

main();
