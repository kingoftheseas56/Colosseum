#!/usr/bin/env python3
"""Convert Codex/Claude conversation JSON or JSONL to dialogue-only Markdown.

Directory exports are scoped to this repository's workspace by default.
Pass --all-workspaces to export sessions from every working directory.
Raw inputs are read-only; system, tool, and reasoning records are omitted.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

EXTENSIONS = {".json", ".jsonl", ".ndjson"}
HIDDEN_CHANNELS = {"analysis", "justify", "confidence", "summary", "notification"}
HIDDEN_CONTENT = {"thinking", "redacted_thinking", "tool_use", "tool_result",
                  "function_call", "function_call_output", "image", "input_image",
                  "output_image", "input_audio", "audio", "video", "file", "input_file"}
INJECTED_PREFIXES = ("# agents.md instructions", "<environment_context>",
                     "<codex_internal_context", "<external_codex_apps_",
                     "<recommended_plugins", "<instructions>", "<system_reminder>",
                     "<system-reminder>", "<claude-mem-context>")

SECRET_PATTERNS = [
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----.*?-----END (?:RSA |EC |OPENSSH )?PRIVATE KEY-----", re.S),
    re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-(?:proj-)?[A-Za-z0-9_-]{20,})\b"),
    re.compile(r"\beyJ[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}\b"),
    re.compile(r"(?i)\b(?:postgres(?:ql)?|mysql|mongodb(?:\+srv)?)://[^\s\"'<>]+"),
    re.compile(r"(?i)(\bauthorization\s*[:=]\s*[\"']?(?:bearer|basic)\s+)[A-Za-z0-9_./+=:-]+"),
    re.compile(r"(?i)(\b(?:api[_-]?key|access[_-]?token|refresh[_-]?token|client[_-]?secret|password|passwd)\s*[:=]\s*[\"']?)([^\s\"',;\x60<>]{8,})"),
    re.compile(r"(?i)([?&](?:token|access_token|refresh_token|code|signature|password)=)[^&\s\"'<>]+"),
]


def redact_secrets(text):
    count = 0
    for pattern in SECRET_PATTERNS:
        def replace(match):
            nonlocal count
            if "[REDACTED" in match.group(0):
                return match.group(0)
            count += 1
            return match.group(1) + "[REDACTED]" if pattern.groups else "[REDACTED]"
        text = pattern.sub(replace, text)
    return text, count


def normalized_path(value):
    value = str(value).replace("\\\\?\\", "")
    return os.path.normcase(os.path.normpath(value))


def restricted_path(value):
    return "preflight-architect" in str(value).lower().replace("\\", "/")


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def text_parts(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, list):
        result = []
        for part in value:
            result.extend(text_parts(part))
        return result
    if not isinstance(value, dict):
        return []
    kind = str(value.get("type", "")).lower()
    if kind in HIDDEN_CONTENT:
        return []
    if kind in {"text", "input_text", "output_text"}:
        return [value["text"]] if isinstance(value.get("text"), str) else []
    if "content" in value:
        return text_parts(value["content"])
    if isinstance(value.get("text"), str) and not kind:
        return [value["text"]]
    return []


def clean_text(value):
    if not isinstance(value, str):
        return ""
    value = value.replace("\r\n", "\n").replace("\r", "\n").replace("\x00", "").strip()
    if value.lower().startswith(INJECTED_PREFIXES):
        return ""
    for tag in ("environment_context", "codex_internal_context", "recommended_plugins",
                "system_reminder", "system-reminder", "claude-mem-context"):
        value = re.sub(r"(?is)<" + tag + r"\b[^>]*>.*?</" + tag + r">", "", value)
    value = re.sub(r"\n{4,}", "\n\n\n", value).strip()
    return value


def event(record):
    """Return (role, text) for visible dialogue, or None for internal events."""
    if not isinstance(record, dict):
        return None
    kind = record.get("type")
    payload = record.get("payload")
    if record.get("isCompactSummary"):
        content = record.get("message", {}).get("content", "")
        return "context_summary", clean_text("\n\n".join(text_parts(content)))
    if kind == "compacted" and isinstance(payload, dict):
        return "context_summary", clean_text("\n\n".join(text_parts(payload.get("message", ""))))
    if kind == "response_item":
        if not isinstance(payload, dict) or payload.get("type") != "message":
            return None
        record = payload
        kind = "message"
    elif kind == "event_msg":
        if not isinstance(payload, dict):
            return None
        name = payload.get("type")
        if name in ("user_message", "agent_message"):
            role = "user" if name == "user_message" else "assistant"
            return role, clean_text("\n\n".join(text_parts(
                payload.get("message", payload.get("text", "")))))
        return None

    if kind in ("user", "assistant") and isinstance(record.get("message"), dict):
        message = record["message"]
        role = message.get("role", kind)
        if message.get("channel") in HIDDEN_CHANNELS:
            return None
        content = message.get("content")
    elif kind == "message" or (not kind and record.get("role")):
        role = record.get("role")
        if record.get("channel") in HIDDEN_CHANNELS:
            return None
        content = record.get("content", record.get("text", ""))
    else:
        return None
    if role not in ("user", "assistant"):
        return None
    return role, clean_text("\n\n".join(text_parts(content)))


def records(path):
    if path.suffix.lower() == ".json":
        with path.open(encoding="utf-8-sig") as source:
            data = json.load(source)
        if isinstance(data, dict):
            for key in ("messages", "events", "conversation"):
                if isinstance(data.get(key), list):
                    for number, record in enumerate(data[key]):
                        if isinstance(record, dict):
                            yield {**record, "_raw_reference": f"#/{key}/{number}"}
                    return
        if isinstance(data, list):
            for number, record in enumerate(data):
                if isinstance(record, dict):
                    yield {**record, "_raw_reference": f"#/{number}"}
        else:
            if isinstance(data, dict):
                yield {**data, "_raw_reference": "#"}
        return
    with path.open(encoding="utf-8-sig") as source:
        for number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
                if isinstance(record, dict):
                    yield {**record, "_raw_reference": f"L{number}"}
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path.name}:{number}: invalid JSONL: {exc}") from exc


def parse_session(path):
    metadata = {}
    dialogue = []
    entries = []
    previous = None
    previous_kind = None
    for item in records(path):
        if not isinstance(item, dict):
            continue
        if item.get("type") == "session_meta" and isinstance(item.get("payload"), dict):
            metadata.update(item["payload"])
            metadata["platform"] = "codex"
        if item.get("type") == "turn_context" and isinstance(item.get("payload"), dict):
            if item["payload"].get("cwd"):
                metadata.setdefault("cwd", item["payload"]["cwd"])
        if item.get("cwd"):
            metadata.setdefault("cwd", item["cwd"])
        if item.get("sessionId"):
            metadata.setdefault("id", item["sessionId"])
            metadata.setdefault("platform", "claude")
        item_cwd = item.get("cwd", "")
        if item.get("type") == "turn_context" and isinstance(item.get("payload"), dict):
            item_cwd = item["payload"].get("cwd", "")
        if restricted_path(metadata.get("cwd", "")) or restricted_path(item_cwd):
            metadata["_restricted"] = True
            break
        pair = event(item)
        if not pair or not pair[1]:
            continue
        text, redactions = redact_secrets(pair[1])
        pair = (pair[0], text)
        stamp = item.get("timestamp")
        if not stamp and isinstance(item.get("payload"), dict):
            stamp = item["payload"].get("timestamp")
        stamp = stamp if isinstance(stamp, str) else ""
        kind = item.get("type")
        reference = item.get("_raw_reference", "")
        if pair == previous and {kind, previous_kind} == {"response_item", "event_msg"}:
            entries[-1]["raw_references"].append(reference)
            if stamp and not entries[-1]["timestamp"]:
                entries[-1]["timestamp"] = stamp
            previous_kind = kind
            continue
        dialogue.append(pair)
        entries.append({"role": pair[0], "text": pair[1], "timestamp": stamp,
                        "raw_references": [reference], "redactions": redactions})
        if stamp:
            metadata.setdefault("timestamp", stamp)
        previous, previous_kind = pair, kind
    metadata["_entries"] = entries
    return metadata, dialogue


def scope_metadata(path):
    """Inspect only the initial records needed for workspace attribution."""
    metadata = {}
    for number, item in enumerate(records(path)):
        if item.get("type") in {"session_meta", "turn_context"} and isinstance(item.get("payload"), dict):
            metadata.update({k: v for k, v in item["payload"].items()
                             if k in {"cwd", "id", "timestamp"}})
        if item.get("cwd"):
            metadata["cwd"] = item["cwd"]
        if item.get("sessionId"):
            metadata["id"] = item["sessionId"]
        if metadata.get("cwd"):
            break
    return metadata


def matches_workspace(cwd, root):
    if not cwd:
        return False
    cwd = normalized_path(cwd)
    root = normalized_path(root)
    if not (cwd == root or cwd.startswith(root + os.sep)):
        return False
    # Colosseum is a separate nested Git repository, not Brotherhood history.
    if Path(root).name.lower() == "brotherhood":
        child = os.path.normcase(os.path.join(root, "Colosseum"))
        if cwd == child or cwd.startswith(child + os.sep):
            return False
    return True


def heading(dialogue, path):
    for role, message in dialogue:
        if role == "user":
            line = next((s.strip() for s in message.splitlines() if s.strip()), "")
            line = re.sub(r"^#{1,6}\s*", "", re.sub(r"<[^>]*>", "", line))
            line = re.sub(r"\s+", " ", line).strip(" -*#:|")
            if len(line) >= 8:
                return line[:96].rstrip()
    return path.stem[:96]


def render(path, metadata, dialogue):
    lines = ["# " + heading(dialogue, path), "", "**Source:** " + str(path), ""]
    if metadata.get("id"):
        lines += ["**Session:** " + str(metadata["id"]), ""]
    if metadata.get("timestamp"):
        lines += ["**Date:** " + str(metadata["timestamp"]), ""]
    if metadata.get("raw_sha256"):
        lines += ["**Raw SHA-256:** " + metadata["raw_sha256"], ""]
    if metadata.get("cwd"):
        lines += ["**Recorded workspace:** " + str(metadata["cwd"]), ""]
    lines += ["**Evidence scope:** User/assistant dialogue with generated context summaries labeled."
              " Tool results, system instructions, reasoning and attachments are omitted."
              " Credentials are redacted on a best-effort basis. Claims require source verification.", ""]
    lines += ["---", ""]
    entries = metadata.get("_entries", [])
    for index, (role, content) in enumerate(dialogue):
        label = "Generated Context Summary" if role == "context_summary" else role.title()
        lines += ["## " + label, ""]
        if index < len(entries):
            entry = entries[index]
            lines += ["**Message timestamp:** " + (entry["timestamp"] or "Not recorded"),
                      "**Raw reference:** " + ", ".join(entry["raw_references"]), ""]
        lines += [content, ""]
    return "\n".join(lines).rstrip() + "\n"


def input_files(source):
    if source.is_file():
        if source.suffix.lower() not in EXTENSIONS:
            raise ValueError("Input must be a .json, .jsonl, or .ndjson file")
        return [source]
    if not source.is_dir():
        raise FileNotFoundError(source)
    return sorted(p for p in source.rglob("*")
                  if p.is_file() and p.suffix.lower() in EXTENSIONS
                  and not restricted_path(p)
                  and "tool-results" not in p.parts
                  and "memory" not in p.parts)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="JSON/JSONL file or a directory of session files")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Output directory")
    parser.add_argument("--workspace-root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="Directory whose sessions to select (default: this repository)")
    parser.add_argument("--all-workspaces", action="store_true",
                        help="Disable workspace filtering for directory input")
    parser.add_argument("--include-workspace", action="append", type=Path, default=[],
                        help="Also include a worktree/project path; repeat as needed")
    parser.add_argument("--exclude-workspace", action="append", type=Path, default=[],
                        help="Exclude another workspace even with --all-workspaces")
    parser.add_argument("--stable-seconds", type=int, default=0,
                        help="Skip files modified within this many seconds")
    args = parser.parse_args(argv)
    source = args.input.expanduser().resolve()
    output = args.output.expanduser().resolve()
    workspace = args.workspace_root.expanduser().resolve()
    if output == source or source in output.parents:
        parser.error("Output must be outside the raw input directory")
    if output.exists() and any(output.iterdir()):
        parser.error("Output must be a new or empty directory; existing exports are preserved")
    files = input_files(source)
    rows, skipped, failures = [], 0, []
    skipped_files = []
    scopes = [workspace, *args.include_workspace]
    for path in files:
        try:
            before = path.stat()
            if args.stable_seconds and time.time() - before.st_mtime < args.stable_seconds:
                skipped += 1
                skipped_files.append({"source": str(path), "reason": "recently_modified"})
                continue
            initial = scope_metadata(path)
            cwd = initial.get("cwd", "")
            reason = ""
            if restricted_path(cwd) or restricted_path(path):
                reason = "restricted"
            elif any(matches_workspace(cwd, root) for root in args.exclude_workspace):
                reason = "excluded_workspace"
            elif source.is_dir() and not args.all_workspaces and not any(
                    matches_workspace(cwd, root) for root in scopes):
                reason = "unknown_workspace" if not cwd else "other_workspace"
            if reason:
                skipped += 1
                skipped_files.append({"source": str(path), "reason": reason})
                continue
            source_hash = file_hash(path)
            meta, dialogue = parse_session(path)
            cwd = meta.get("cwd", "")
            reason = ""
            if meta.get("_restricted") or restricted_path(path):
                reason = "restricted"
            elif any(matches_workspace(cwd, root) for root in args.exclude_workspace):
                reason = "excluded_workspace"
            elif source.is_dir() and not args.all_workspaces and not any(
                    matches_workspace(cwd, root) for root in scopes):
                reason = "unknown_workspace" if not cwd else "other_workspace"
            elif not dialogue:
                reason = "no_dialogue"
            after = path.stat()
            if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns:
                reason = "changed_during_read"
            elif not reason and file_hash(path) != source_hash:
                reason = "changed_during_read"
            if reason:
                skipped += 1
                skipped_files.append({"source": str(path), "reason": reason})
                continue
            meta["raw_sha256"] = source_hash
            rel = path.relative_to(source) if source.is_dir() else Path(path.name)
            target = output / rel.parent / (rel.stem + "-" + rel.suffix[1:] + ".md")
            target.parent.mkdir(parents=True, exist_ok=True)
            body = render(path, meta, dialogue)
            target.write_text(body, encoding="utf-8", newline="\n")
            rows.append({"source": str(path), "markdown": str(target),
                         "workspace": meta.get("cwd", ""), "session": meta.get("id", ""),
                         "messages": len(dialogue),
                         "sha256": hashlib.sha256(body.encode("utf-8")).hexdigest(),
                         "raw_sha256": source_hash, "raw_bytes": before.st_size,
                         "platform": meta.get("platform", "unknown"),
                         "first_message_at": next((e["timestamp"] for e in meta["_entries"] if e["timestamp"]), ""),
                         "last_message_at": next((e["timestamp"] for e in reversed(meta["_entries"]) if e["timestamp"]), ""),
                         "generated_summaries": sum(e["role"] == "context_summary" for e in meta["_entries"]),
                         "redactions": sum(e["redactions"] for e in meta["_entries"])})
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            failures.append({"source": str(path), "error": str(exc)})
    output.mkdir(parents=True, exist_ok=True)
    with (output / "MANIFEST.csv").open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=["source", "markdown", "workspace",
                                                  "session", "messages", "sha256",
                                                  "raw_sha256", "raw_bytes", "platform",
                                                  "first_message_at", "last_message_at",
                                                  "generated_summaries", "redactions"])
        writer.writeheader()
        writer.writerows(rows)
    index = ["# Clean session transcripts", "",
             f"**Exported:** {len(rows)}  |  **Skipped:** {skipped}  |  **Failures:** {len(failures)}", ""]
    for row in rows:
        rel = Path(row["markdown"]).relative_to(output).as_posix()
        index.append(f"- [{Path(row['markdown']).stem}]({rel}) ({row['messages']} messages)")
    (output / "INDEX.md").write_text("\n".join(index) + "\n", encoding="utf-8")
    report = {"input_files": len(files), "exported": len(rows), "skipped": skipped,
              "failed": failures, "workspace_root": str(workspace),
              "included_workspaces": [str(p) for p in scopes],
              "skipped_files": skipped_files, "schema_version": 2,
              "exported_at_utc": datetime.now(timezone.utc).isoformat(),
              "generated_summaries": sum(r["generated_summaries"] for r in rows),
              "redactions": sum(r["redactions"] for r in rows)}
    (output / "REPORT.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
