#!/usr/bin/env python3
"""lanista-mcp — stdio MCP adapter over the ColosseumLanista pipe.

Stdlib only, deliberately: the app carries no new dependency and neither does
the adapter. Exposes three tools; everything else rides lanista_call's generic
cmd/payload passthrough. Grabs come back as MCP image content so the agent
SEES the pixels inline.
"""
import base64
import json
import os
import sys

PIPE = r"\\.\pipe\{}".format(os.environ.get("COLOSSEUM_LANISTA_PIPE",
                                            "ColosseumLanista"))

def pipe_call(cmd, payload=None, seq=1):
    req = {"cmd": cmd, "seq": seq}
    if payload:
        req["payload"] = payload
    with open(PIPE, "r+b", buffering=0) as f:
        f.write((json.dumps(req) + "\n").encode())
        buf = b""
        while b"\n" not in buf:
            chunk = f.read(65536)
            if not chunk:
                break
            buf += chunk
    return json.loads(buf.split(b"\n")[0] or b"{}")

TOOLS = [
    {"name": "lanista_call",
     "description": "Send any lanista command to the running Colosseum "
                    "(ping, get-state, qml-get, ui-query, ui-snapshot, "
                    "invoke-read, events-tail, log-mark; ui-* drive commands "
                    "need COLOSSEUM_LANISTA_DRIVE=1 in the app).",
     "inputSchema": {"type": "object",
                     "properties": {"cmd": {"type": "string"},
                                    "payload": {"type": "object"}},
                     "required": ["cmd"]}},
    {"name": "lanista_grab",
     "description": "Photograph a UI element (by objectName) or the whole "
                    "window of the running Colosseum; returns the image.",
     "inputSchema": {"type": "object",
                     "properties": {"target": {"type": "string"}},
                     "required": ["target"]}},
    {"name": "lanista_snapshot",
     "description": "List every interactive element on screen right now, "
                    "with handles, positions and states — look before acting.",
     "inputSchema": {"type": "object", "properties": {}}},
]

class RpcError(Exception):
    """A JSON-RPC error with a specific code. Raised by handle(), turned into a
    proper error reply by main() — so a bad method or tool name is a clean coded
    reply, never an uncaught raise that could stall the agent."""
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message

def handle(method, params, rid):
    if method == "initialize":
        return {"protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "lanista", "version": "1.0"}}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments", {})
        if name == "lanista_call":
            r = pipe_call(args["cmd"], args.get("payload"))
            return {"content": [{"type": "text", "text": json.dumps(r, indent=2)}]}
        if name == "lanista_snapshot":
            r = pipe_call("ui-snapshot")
            return {"content": [{"type": "text", "text": json.dumps(r, indent=2)}]}
        if name == "lanista_grab":
            r = pipe_call("get-state", {"grab": {"target": args["target"]}})
            path = r.get("grabPath", "")
            if path and os.path.exists(path):
                with open(path, "rb") as f:
                    data = base64.b64encode(f.read()).decode()
                return {"content": [
                    {"type": "image", "data": data, "mimeType": "image/png"},
                    {"type": "text", "text": json.dumps(
                        {k: v for k, v in r.items() if k != "grabPath"})}]}
            return {"content": [{"type": "text",
                                 "text": json.dumps(r)}], "isError": True}
        raise RpcError(-32602, "unknown tool: " + str(name))
    raise RpcError(-32601, "method not found: " + str(method))

def _emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        # ONE try/except around the WHOLE per-line body — parse INCLUDED. This is
        # the agent's only channel to the app, so a single malformed line (bad
        # JSON, or valid JSON that isn't an object) must produce a coded reply and
        # move on, never throw out of the loop and kill the adapter.
        rid = None
        try:
            try:
                msg = json.loads(line)
            except ValueError:                 # not JSON at all
                _emit({"jsonrpc": "2.0", "id": None,
                       "error": {"code": -32700, "message": "Parse error"}})
                continue
            if not isinstance(msg, dict):       # e.g. 5 or [1] — no .get / no id
                _emit({"jsonrpc": "2.0", "id": None,
                       "error": {"code": -32600, "message": "Invalid Request"}})
                continue
            rid = msg.get("id")
            result = handle(msg.get("method", ""), msg.get("params", {}), rid)
            if rid is None:
                continue                       # notification: no reply
            out = {"jsonrpc": "2.0", "id": rid, "result": result}
        except RpcError as exc:                # a method/tool we named a code for
            if rid is None:
                continue
            out = {"jsonrpc": "2.0", "id": rid,
                   "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:               # noqa: BLE001 — any other fault
            if rid is None:
                continue
            out = {"jsonrpc": "2.0", "id": rid,
                   "error": {"code": -32603, "message": str(exc)}}
        _emit(out)

if __name__ == "__main__":
    main()
