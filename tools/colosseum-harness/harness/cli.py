from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any

def add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--root", default=argparse.SUPPRESS)
    parser.add_argument("--map", default=argparse.SUPPRESS)
    parser.add_argument("--preflight-root", dest="preflight_root", default=argparse.SUPPRESS)
    parser.add_argument("--json", action="store_true", default=argparse.SUPPRESS)

def add_execution_mode(parser: argparse.ArgumentParser) -> None:
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--dry-run", dest="dry_run", action="store_true")
    group.add_argument("--run", dest="dry_run", action="store_false")
    parser.set_defaults(dry_run=True)

def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False, argument_default=argparse.SUPPRESS)
    add_common(common)
    parser = argparse.ArgumentParser(prog="colosseum-harness", parents=[common])
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", parents=[common], add_help=True)
    inspect_p = sub.add_parser("inspect", parents=[common], add_help=True)
    inspect_p.add_argument("target")

    context_p = sub.add_parser("context-for-task", parents=[common], add_help=True)
    context_p.add_argument("task")
    context_p.add_argument("--path", dest="paths", action="append")
    context_p.add_argument("--domain")
    context_p.add_argument("--record-run", action="store_true")

    bind_p = sub.add_parser("bind-session", parents=[common], add_help=True)
    bind_p.add_argument("--run-id", required=True)
    bind_p.add_argument("--session", required=True)

    test_p = sub.add_parser("test", parents=[common], add_help=True)
    test_p.add_argument("selector")
    add_execution_mode(test_p)

    sub.add_parser("journeys", parents=[common], add_help=True)
    journey_p = sub.add_parser("journey", parents=[common], add_help=True)
    journey_p.add_argument("selector", nargs="?")
    journey_p.add_argument("--run-id")
    journey_p.add_argument("--mode", choices=("session", "attached"), default="session")
    journey_p.add_argument("--drive", action="store_true")
    journey_p.add_argument("--seed")
    journey_p.add_argument("--ready-ms", type=int)
    journey_p.add_argument("--pipe")
    add_execution_mode(journey_p)

    verify_p = sub.add_parser("verify", parents=[common], add_help=True)
    verify_p.add_argument("--path", dest="paths", action="append")
    verify_p.add_argument("--run-id")
    add_execution_mode(verify_p)
    return parser

def command_text(argv: Any) -> str:
    if not isinstance(argv, list):
        return ""
    return subprocess.list2cmdline([str(value) for value in argv])

def emit(payload: dict[str, Any], json_mode: bool) -> None:
    if json_mode:
        # Machine mode stays ASCII-safe so Windows pipes cannot turn valid UTF-8
        # words into replacement characters such as "facade".
        print(json.dumps(payload, indent=2, ensure_ascii=True))
        return

    command = str(payload.get("command", "unknown"))
    repo = payload.get("repo", {}) if isinstance(payload.get("repo"), dict) else {}
    data = payload.get("data", {}) if isinstance(payload.get("data"), dict) else {}

    if not payload.get("ok"):
        error = payload.get("error", {}) if isinstance(payload.get("error"), dict) else {}
        print(
            f"{command}: {error.get('code', 'ERROR')}: {error.get('message', '')}",
            file=sys.stderr,
        )
        details = error.get("details")
        if isinstance(details, dict):
            suggestions = details.get("suggestions", [])
            if suggestions:
                print("suggestions:", file=sys.stderr)
                for item in suggestions:
                    if isinstance(item, dict):
                        print(f"  {item.get('target')} [{item.get('domain')}]", file=sys.stderr)
            next_actions = details.get("nextActions", [])
            if next_actions:
                print("next:", file=sys.stderr)
                for action in next_actions:
                    print(f"  {action}", file=sys.stderr)
        return

    print(f"{command}: OK")
    if command == "status":
        print(f"repo: {repo.get('root')}")
        print(f"branch: {repo.get('branch')}")
        print(f"head: {repo.get('head')}")
        dirty = repo.get("dirty", [])
        print(f"dirty: {len(dirty)}")
        for row in dirty:
            print(f"  {row}")
    elif command == "inspect":
        match = data.get("match", {}) if isinstance(data.get("match"), dict) else {}
        candidates = data.get("candidates", [])
        if candidates:
            print(f"target: {data.get('target')} ({match.get('kind')})")
            for item in candidates:
                if not isinstance(item, dict):
                    continue
                domain = item.get("domain", {}) if isinstance(item.get("domain"), dict) else {}
                evidence = item.get("evidence", [])
                evidence_text = ", ".join(
                    f"{ev.get('kind')}:{ev.get('path', ev.get('value', ''))}"
                    for ev in evidence if isinstance(ev, dict)
                )
                print(
                    f"  {item.get('rank')}. {domain.get('id')} "
                    f"score={item.get('score')} [{evidence_text}]"
                )
        else:
            domain = data.get("domain", {}) if isinstance(data.get("domain"), dict) else {}
            print(f"domain: {domain.get('id')} - {domain.get('display_name')}")
            if domain.get("feature_status"):
                print(f"status: {domain.get('feature_status')}")
            owners = domain.get("owners", [])
            if owners:
                print("owners:")
                for owner in owners:
                    if isinstance(owner, dict):
                        print(
                            f"  {owner.get('name')}: {owner.get('path')} "
                            f"({owner.get('confidence')})"
                        )
            surfaces = [
                item.get("name")
                for item in domain.get("ctests", [])
                if isinstance(item, dict) and item.get("name")
            ]
            surfaces.extend(
                item for item in domain.get("checks", [])
                if isinstance(item, str)
            )
            if surfaces:
                print("verify:")
                for surface in surfaces:
                    print(f"  {surface}")
    elif command == "context-for-task":
        freshness = data.get("freshness", {}) if isinstance(data.get("freshness"), dict) else {}
        print(f"freshness: {freshness.get('state')}")
        for domain in data.get("domains", []):
            if isinstance(domain, dict):
                print(f"domain: {domain.get('id')} - {domain.get('displayName')}")
        for arc in data.get("activeArcs", []):
            if isinstance(arc, dict):
                print(f"arc: {arc.get('id')} -> {arc.get('path')}")
        print(f"verification: {len(data.get('verification', []))}")
    elif command == "test":
        print(f"selected: {', '.join(data.get('selectedTests', []))}")
        print(f"kind: {data.get('kind')}")
        print(f"dry-run: {data.get('dryRun')}")
        print(f"runnable: {data.get('canRun', True)}")
        for reason in data.get("selectionReasons", []):
            if isinstance(reason, dict):
                print(f"why: {reason.get('because')}")
        print(f"command: {command_text(data.get('argv'))}")
    elif command == "verify":
        selected = data.get("selectedChecks", [])
        print(f"dry-run: {data.get('dryRun')}")
        print(f"checks: {len(selected)}")
        for item in selected:
            if not isinstance(item, dict):
                continue
            print(f"  {item.get('selector')} -> {item.get('name')}")
            for reason in item.get("selectionReasons", []):
                if isinstance(reason, dict):
                    print(f"    why: {reason.get('because')}")
    elif command == "journeys":
        print(
            f"journeys: {data.get('count')} "
            f"(valid={data.get('validCount')}, invalid={data.get('invalidCount')})"
        )
    elif command == "journey":
        print(f"journey: {data.get('name')}")
        print(f"dry-run: {data.get('dryRun')}")
        print(f"command: {command_text(data.get('argv'))}")

    for warning in payload.get("warnings", []):
        print(f"warning: {warning}", file=sys.stderr)

def option_value(argv: list[str], flag: str) -> str | None:
    for index, value in enumerate(argv):
        if value == flag and index + 1 < len(argv):
            return argv[index + 1]
    return None
