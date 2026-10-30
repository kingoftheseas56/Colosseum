"""Drive only an explicitly named, isolated candidate through its real Lanista CLI.

Each request and observed reply is retained as local evidence. A scenario's
transport success is never treated as an application assertion by this client.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


class Candidate:
    def __init__(self, cli: Path, pipe: str, evidence: Path):
        if not pipe.startswith("CollectionsIntegration-"):
            raise ValueError("An explicitly isolated CollectionsIntegration-* pipe is required")
        self.cli, self.pipe, self.evidence = cli.resolve(), pipe, evidence.resolve()
        self.evidence.mkdir(parents=True, exist_ok=True)
        self.commands = self.evidence / "commands"
        self.commands.mkdir(exist_ok=True)
        self.serial = int(time.time() * 1000)

    def call(self, command: str, payload: dict | None = None, *, allow_error=False):
        self.serial += 1
        request = {"cmd": command, "payload": payload or {}}
        path = self.commands / f"{self.serial}-{command}.json"
        path.write_text(json.dumps({"name": "Collections observed command", "steps": [request]},
                                   ensure_ascii=False), encoding="utf-8")
        deadline = max(15, int((payload or {}).get("timeout_ms", 0)) / 1000 + 10)
        completed = subprocess.run([str(self.cli), "--pipe", self.pipe, "--verbose", "run", str(path)],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=deadline)
        path.with_suffix(".stdout.txt").write_text(completed.stdout, encoding="utf-8")
        path.with_suffix(".stderr.txt").write_text(completed.stderr, encoding="utf-8")
        replies = [json.loads(m.group(1)) for line in completed.stdout.splitlines()
                   if (m := re.match(r"\s*reply .*?: (\{.*\})$", line))]
        if len(replies) != 1:
            raise RuntimeError(f"Lanista did not return one observed reply: {completed.stdout}\n{completed.stderr}")
        reply = replies[0]
        row = {"time": time.time(), "request": request, "reply": reply,
               "exitCode": completed.returncode, "stderr": completed.stderr}
        with (self.evidence / "observed-commands.jsonl").open("a", encoding="utf-8") as output:
            output.write(json.dumps(row, ensure_ascii=False) + "\n")
        if not allow_error and (reply.get("type") != "reply" or completed.returncode):
            raise AssertionError(json.dumps(row, ensure_ascii=False))
        return reply

    def props(self, name, *properties):
        return self.call("qml-get", {"object": name, "props": list(properties)})["props"]

    def wait(self, name, prop, value, timeout=10000):
        return self.call("ui-wait-for", {"object": name, "prop": prop, "value": value,
                                         "timeout_ms": timeout})

    def click(self, name):
        return self.call("ui-click", {"target": name})

    def key(self, sequence):
        return self.call("ui-keypress", {"key": sequence})

    def text(self, name, value):
        self.click(name)
        self.key("Ctrl+A")
        self.call("ui-text-input", {"target": name, "text": value})
        actual = self.props(name, "text")["text"]
        if actual != value:
            raise AssertionError(f"Typed value differs for {name}: {actual!r}")

    def screenshot(self, name):
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", name):
            raise ValueError("Use a simple evidence filename")
        result = self.call("get-state", {"grab": {"target": "window"}})
        source = Path(result["grabPath"])
        target = self.evidence / f"{name}.png"
        shutil.copyfile(source, target)
        (self.evidence / f"{name}.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return str(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--pipe", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--command", default="ping")
    parser.add_argument("--payload", type=Path)
    parser.add_argument("--screenshot")
    args = parser.parse_args()
    client = Candidate(args.cli, args.pipe, args.evidence)
    if args.screenshot:
        print(client.screenshot(args.screenshot))
    else:
        payload = json.loads(args.payload.read_text(encoding="utf-8")) if args.payload else {}
        print(json.dumps(client.call(args.command, payload), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
