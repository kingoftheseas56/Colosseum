#!/usr/bin/env python3
"""soak-digest.py - turn a soak session's events.jsonl into the page Hemanth reads.

Slice S1-Digest (docs/superpowers/plans/2026-08-13-colosseum-visibility-phase2-plan.md).
Python 3, stdlib only - no third-party dependency, ever.

WHAT IT READS
--------------
One "session root" directory. S0-Pulse (native/devtools/PulseNarrator + the existing
LanistaEventLog) writes the event stream at <AppDataLocation>/lanista/events.jsonl for a
tagged/isolated session (ground-truthed against native/devtools/LanistaServer.cpp:185-187,
2026-08-13 - S0 itself is not built yet, so this is the path convention this slice targets).
Resolution order for the events file, first hit wins:
    1. --events-file PATH, if given (explicit override - this is what the test suite uses,
       since the plan's fixtures are named tests/fixtures/soak/events-*.jsonl directly and are
       not nested under a lanista/ subdirectory)
    2. <session_root>/lanista/events.jsonl   (S0's real on-disk convention)
    3. <session_root>/events.jsonl           (flat fallback, e.g. an evidence copy)

Optionally reads a warning-gate verdict: the already-decided stdout of
tests/warning_gate.ps1, captured to a text file (matching the "warnings.txt" evidence-artifact
convention used across this plan's other slices). This script does not re-implement the gate's
own log scanning/allowlist logic - it only parses the gate's own printed verdict:
    - exit 0 → a line reading exactly "WARNING_GATE_OK"
    - exit 1 → one or more lines starting with "FAIL: "
    - exit 2 → a "FAIL: " line describing a malformed allowlist (schema error)
Resolution: --warnings-file PATH, else <session_root>/warnings.txt if present, else "not
available" (never fabricated as clean).

EVENT SCHEMA (S0, as specified by the plan)
--------------------------------------------
Newline-delimited JSON objects:
    {type, at, subject, durationMs?, outcome?, bytes?, memMb?, droppedFrames?}
type is one of: open, ready, fail, download, mem, frames, nav.
`at` is an ISO-8601 timestamp with milliseconds (Qt::ISODateWithMs, e.g.
"2026-08-12T00:00:00.000") - no timezone suffix, matches Python's datetime.fromisoformat().

WORLD, derived (not a schema field): S0's schema has no top-level "world" field. The app's
existing world vocabulary (native/engine/LocalDownloads.cpp) and the soak driver's own
--worlds default (tankoban+vault+local-video, S2-Driver) both point at a small set of world
labels. This script assumes `subject` is written as "<world>:<rest>" (first colon splits it);
a subject with no colon is not tied to one world and is reported under world "unknown" (this
covers process-wide samples like memory, which are not really "in" a world). This is a stated
assumption pending S0's real implementation - if S0 lands a different convention, only
`world_of()` below needs to change.

HONESTY CONTRACT (the point of this slice)
--------------------------------------------
The digest is a pure function of its inputs (events + optional warning verdict) - it embeds
no wall-clock "generated at" timestamp, so it is exactly reproducible from the same input
files (this is what makes golden-fixture byte-equivalence testing possible at all). Coverage
(worlds visited, event/subject counts, wall-clock span actually covered) is always stated
plainly and first. Zero events renders one explicit "NO DATA" block and nothing else - never
an empty table dressed up as a clean pass. A malformed/poisoned line is skipped and counted,
never allowed to crash the run.

OUTPUT
------
Writes <out-dir>/soak-digest.md (Hemanth-language: gray/black/white plain text, no taglines,
what a number means for him before the number itself) and <out-dir>/soak-digest.json (the same
facts, machine-readable). --out-dir defaults to the session root.

USAGE
-----
    python scripts/soak-digest.py <session_root> [--events-file PATH] [--warnings-file PATH]
                                   [--out-dir DIR] [--label TEXT]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

SCHEMA = "colosseum.soak.digest.v1"

KNOWN_TYPES = ["open", "ready", "fail", "download", "mem", "frames", "nav"]

REQUIRED_EVENT_FIELDS = ("type", "at", "subject")


# ───────────────────────────── parsing / loading ─────────────────────────────


class EventsResult:
    """The honest outcome of reading one events.jsonl: what was usable, what was not."""

    def __init__(
        self,
        events: List[Dict[str, Any]],
        malformed_lines: int,
        total_lines: int,
        file_found: bool,
        path_tried: List[str],
    ):
        self.events = events
        self.malformed_lines = malformed_lines
        self.total_lines = total_lines
        self.file_found = file_found
        self.path_tried = path_tried

    @property
    def has_data(self) -> bool:
        return len(self.events) > 0


def _is_valid_event(obj: Any) -> bool:
    if not isinstance(obj, dict):
        return False
    for field in REQUIRED_EVENT_FIELDS:
        if field not in obj:
            return False
    if obj.get("type") not in KNOWN_TYPES:
        return False
    subject = obj.get("subject")
    if not isinstance(subject, str) or not subject.strip():
        return False
    at = obj.get("at")
    if not isinstance(at, str):
        return False
    try:
        parse_at(at)
    except ValueError:
        return False
    return True


def parse_at(at: str) -> datetime:
    """Parse S0's ISO-8601-with-ms timestamp. Raises ValueError on anything else."""
    return datetime.fromisoformat(at)


def _posix(p: Path) -> str:
    """Forward-slash display form - keeps JSON/markdown output byte-identical across
    Windows/POSIX invocations instead of leaking backslash-escaped paths into the digest."""
    return str(p).replace(os.sep, "/")


def resolve_events_path(session_root: Path, explicit: Optional[str]) -> Tuple[Optional[Path], List[str]]:
    """Return (path_or_None, list_of_candidate_paths_tried) honoring the resolution order."""
    tried: List[str] = []
    if explicit:
        p = Path(explicit)
        tried.append(_posix(p))
        return (p if p.is_file() else None), tried

    candidates = [session_root / "lanista" / "events.jsonl", session_root / "events.jsonl"]
    for c in candidates:
        tried.append(_posix(c))
        if c.is_file():
            return c, tried
    return None, tried


def load_events(session_root: Path, explicit_events_file: Optional[str]) -> EventsResult:
    path, tried = resolve_events_path(session_root, explicit_events_file)
    if path is None:
        return EventsResult(events=[], malformed_lines=0, total_lines=0, file_found=False, path_tried=tried)

    events: List[Dict[str, Any]] = []
    malformed = 0
    total = 0
    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue
            total += 1
            try:
                obj = json.loads(line)
            except (json.JSONDecodeError, ValueError):
                malformed += 1
                continue
            if not _is_valid_event(obj):
                malformed += 1
                continue
            events.append(obj)
    return EventsResult(
        events=events, malformed_lines=malformed, total_lines=total, file_found=True, path_tried=tried
    )


def load_warning_verdict(session_root: Path, explicit: Optional[str]) -> Dict[str, Any]:
    path = Path(explicit) if explicit else (session_root / "warnings.txt")
    if not path.is_file():
        return {"available": False, "clean": None, "failCount": 0, "failLines": [], "path": _posix(path)}

    text = path.read_text(encoding="utf-8", errors="replace")
    lines = [l.rstrip("\n") for l in text.splitlines() if l.strip()]
    fail_lines = [l for l in lines if l.startswith("FAIL:")]
    ok_seen = any(l.strip() == "WARNING_GATE_OK" for l in lines)
    clean = ok_seen and not fail_lines
    return {
        "available": True,
        "clean": clean,
        "failCount": len(fail_lines),
        "failLines": fail_lines,
        "path": _posix(path),
    }


# ───────────────────────────── pure math helpers ─────────────────────────────


def world_of(subject: str) -> str:
    """Derive the world label from a subject string ('<world>:<rest>'). See module docstring
    for why this is a documented assumption rather than a schema field."""
    if ":" in subject:
        head, _, _ = subject.partition(":")
        head = head.strip()
        if head:
            return head
    return "unknown"


def nearest_rank_percentile(sorted_values: List[float], p: float) -> float:
    """Nearest-rank percentile: rank = ceil(p/100 * n), 1-based, clamped to [1, n].
    Deterministic and hand-computable - this exact definition is what the golden fixture's
    percentiles were computed with by hand (see tests/test_soak_digest.py)."""
    n = len(sorted_values)
    if n == 0:
        raise ValueError("percentile of empty sequence")
    rank = math.ceil((p / 100.0) * n)
    rank = max(1, min(n, rank))
    return sorted_values[rank - 1]


def duration_stats(values: List[float]) -> Dict[str, Optional[float]]:
    if not values:
        return {"count": 0, "p50": None, "p95": None, "max": None}
    s = sorted(values)
    return {
        "count": len(s),
        "p50": nearest_rank_percentile(s, 50),
        "p95": nearest_rank_percentile(s, 95),
        "max": s[-1],
    }


def human_span(seconds: float) -> str:
    if seconds < 0:
        seconds = 0
    total = int(round(seconds))
    hours, rem = divmod(total, 3600)
    minutes, secs = divmod(rem, 60)
    parts = []
    if hours:
        parts.append(f"{hours}h")
    if minutes or hours:
        parts.append(f"{minutes}m")
    parts.append(f"{secs}s")
    return " ".join(parts)


# ───────────────────────────── digest computation ─────────────────────────────


def compute_coverage(result: EventsResult) -> Dict[str, Any]:
    cov: Dict[str, Any] = {
        "eventsFileFound": result.file_found,
        "pathsTried": result.path_tried,
        "totalLinesRead": result.total_lines,
        "malformedLinesSkipped": result.malformed_lines,
        "totalEventsValid": len(result.events),
        "hasData": result.has_data,
        "worldsVisited": [],
        "distinctSubjects": 0,
        "firstAt": None,
        "lastAt": None,
        "wallClockSpanSeconds": None,
        "wallClockSpanHuman": None,
    }
    if not result.has_data:
        return cov

    worlds = sorted({world_of(e["subject"]) for e in result.events})
    subjects = {e["subject"] for e in result.events}
    ordered = sorted(result.events, key=lambda e: parse_at(e["at"]))
    first_at = ordered[0]["at"]
    last_at = ordered[-1]["at"]
    span = (parse_at(last_at) - parse_at(first_at)).total_seconds()

    cov["worldsVisited"] = worlds
    cov["distinctSubjects"] = len(subjects)
    cov["firstAt"] = first_at
    cov["lastAt"] = last_at
    cov["wallClockSpanSeconds"] = span
    cov["wallClockSpanHuman"] = human_span(span)
    return cov


def compute_operation_counts(events: List[Dict[str, Any]]) -> Dict[str, int]:
    counts = {t: 0 for t in KNOWN_TYPES}
    for e in events:
        counts[e["type"]] += 1
    return counts


def _duration_value(e: Dict[str, Any]) -> Optional[float]:
    v = e.get("durationMs")
    if v is None:
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def compute_duration_stats_by_type(events: List[Dict[str, Any]]) -> Dict[str, Dict[str, Optional[float]]]:
    by_type: Dict[str, List[float]] = {t: [] for t in KNOWN_TYPES}
    for e in events:
        d = _duration_value(e)
        if d is not None:
            by_type[e["type"]].append(d)
    return {t: duration_stats(vals) for t, vals in by_type.items()}


def compute_duration_stats_by_type_world(
    events: List[Dict[str, Any]]
) -> "list[Dict[str, Any]]":
    grouped: Dict[Tuple[str, str], List[float]] = {}
    for e in events:
        d = _duration_value(e)
        if d is None:
            continue
        key = (e["type"], world_of(e["subject"]))
        grouped.setdefault(key, []).append(d)
    rows = []
    for (t, w), vals in sorted(grouped.items()):
        stats = duration_stats(vals)
        rows.append({"type": t, "world": w, **stats})
    return rows


def compute_failures(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    fails = [e for e in events if e["type"] == "fail"]
    by_subject: Dict[str, Dict[str, Any]] = {}
    for e in fails:
        subj = e["subject"]
        row = by_subject.setdefault(
            subj, {"subject": subj, "count": 0, "firstAt": None, "lastAt": None, "outcomes": []}
        )
        row["count"] += 1
        at = e["at"]
        if row["firstAt"] is None or parse_at(at) < parse_at(row["firstAt"]):
            row["firstAt"] = at
        if row["lastAt"] is None or parse_at(at) > parse_at(row["lastAt"]):
            row["lastAt"] = at
        if e.get("outcome"):
            row["outcomes"].append(e["outcome"])

    rows = sorted(by_subject.values(), key=lambda r: (-r["count"], r["subject"]))
    return {"totalFailEvents": len(fails), "bySubject": rows}


def compute_memory(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    mem_events = [e for e in events if e["type"] == "mem" and e.get("memMb") is not None]
    if not mem_events:
        return {
            "sampleCount": 0,
            "startMb": None,
            "endMb": None,
            "peakMb": None,
            "growthSlopeMbPerHour": None,
            "firstAt": None,
            "lastAt": None,
        }
    ordered = sorted(mem_events, key=lambda e: parse_at(e["at"]))
    values = [float(e["memMb"]) for e in ordered]
    first_at, last_at = ordered[0]["at"], ordered[-1]["at"]
    start_mb, end_mb, peak_mb = values[0], values[-1], max(values)

    slope: Optional[float] = None
    if len(ordered) >= 2:
        elapsed_hours = (parse_at(last_at) - parse_at(first_at)).total_seconds() / 3600.0
        if elapsed_hours > 0:
            slope = (end_mb - start_mb) / elapsed_hours

    return {
        "sampleCount": len(ordered),
        "startMb": start_mb,
        "endMb": end_mb,
        "peakMb": peak_mb,
        "growthSlopeMbPerHour": slope,
        "firstAt": first_at,
        "lastAt": last_at,
    }


def compute_dropped_frames(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    episodes = []
    for e in events:
        if e["type"] != "frames":
            continue
        dropped = e.get("droppedFrames")
        if dropped is None:
            continue
        try:
            dropped = float(dropped)
        except (TypeError, ValueError):
            continue
        if dropped > 0:
            episodes.append({"at": e["at"], "subject": e["subject"], "droppedFrames": dropped})
    episodes.sort(key=lambda r: parse_at(r["at"]))
    return {
        "episodeCount": len(episodes),
        "totalDroppedFrames": sum(ep["droppedFrames"] for ep in episodes),
        "episodes": episodes,
    }


def compute_top_slowest(events: List[Dict[str, Any]], limit: int = 10) -> Dict[str, Any]:
    worst: Dict[str, Dict[str, Any]] = {}
    for e in events:
        d = _duration_value(e)
        if d is None:
            continue
        subj = e["subject"]
        cur = worst.get(subj)
        if cur is None or d > cur["worstDurationMs"]:
            worst[subj] = {"subject": subj, "worstDurationMs": d, "type": e["type"]}
    ranked = sorted(worst.values(), key=lambda r: (-r["worstDurationMs"], r["subject"]))
    return {
        "totalSubjectsWithDurationData": len(ranked),
        "shown": ranked[:limit],
    }


def build_digest(
    events_result: EventsResult, warning_verdict: Dict[str, Any], label: Optional[str] = None
) -> Dict[str, Any]:
    events = events_result.events
    digest: Dict[str, Any] = {
        "schema": SCHEMA,
        "label": label,
        "coverage": compute_coverage(events_result),
    }
    if not events_result.has_data:
        digest["warningGate"] = warning_verdict
        return digest

    digest["operationCounts"] = compute_operation_counts(events)
    digest["durationStatsByType"] = compute_duration_stats_by_type(events)
    digest["durationStatsByTypeWorld"] = compute_duration_stats_by_type_world(events)
    digest["failures"] = compute_failures(events)
    digest["memory"] = compute_memory(events)
    digest["droppedFrameEpisodes"] = compute_dropped_frames(events)
    digest["topSlowestSubjects"] = compute_top_slowest(events)
    digest["warningGate"] = warning_verdict
    return digest


# ───────────────────────────── markdown rendering ─────────────────────────────


def _fmt_ms(v: Optional[float]) -> str:
    if v is None:
        return "-"
    if float(v).is_integer():
        return f"{int(v)} ms"
    return f"{v:.1f} ms"


def _fmt_mb(v: Optional[float]) -> str:
    if v is None:
        return "-"
    if float(v).is_integer():
        return f"{int(v)} MB"
    return f"{v:.1f} MB"


def render_markdown(digest: Dict[str, Any]) -> str:
    lines: List[str] = []
    lines.append("# Soak Digest")
    lines.append("")
    if digest.get("label"):
        lines.append(f"Run: {digest['label']}")
        lines.append("")

    cov = digest["coverage"]

    if not cov["hasData"]:
        lines.append("## No data")
        lines.append("")
        if not cov["eventsFileFound"]:
            tried = ", ".join(cov["pathsTried"]) if cov["pathsTried"] else "(no path given)"
            lines.append(f"No events file was found for this run. Looked at: {tried}.")
        elif cov["totalLinesRead"] == 0:
            lines.append("The events file for this run was empty.")
        else:
            lines.append(
                f"This run's events file had {cov['totalLinesRead']} line(s), but every one of "
                f"them was unreadable ({cov['malformedLinesSkipped']} malformed) - nothing usable "
                "came out of it."
            )
        lines.append("")
        lines.append(
            "Nothing below is reported, because there is nothing honest to report. This is not "
            "a clean pass - treat it as a run that produced no evidence."
        )
        lines.append("")
        _append_warning_section(lines, digest["warningGate"])
        return "\n".join(lines).rstrip() + "\n"

    # ── Coverage - what this run actually saw, up front, always ──
    lines.append("## Coverage")
    lines.append("")
    lines.append(f"- Worlds visited: {', '.join(cov['worldsVisited']) if cov['worldsVisited'] else '(none)'}")
    lines.append(f"- Different things it touched: {cov['distinctSubjects']}")
    malformed_note = ""
    if cov["malformedLinesSkipped"]:
        malformed_note = f" ({cov['malformedLinesSkipped']} line(s) could not be read and were skipped)"
    lines.append(f"- Events recorded: {cov['totalEventsValid']}{malformed_note}")
    lines.append(
        f"- Time span covered: {cov['wallClockSpanHuman']} "
        f"(from {cov['firstAt']} to {cov['lastAt']})"
    )
    lines.append("")
    lines.append(
        "If this span looks short for how long the soak was meant to run, treat this as a "
        "partial or aborted run, not a clean pass."
    )
    lines.append("")

    # ── Operation counts ──
    lines.append("## What it did")
    lines.append("")
    for t in KNOWN_TYPES:
        lines.append(f"- {t}: {digest['operationCounts'][t]}")
    lines.append("")

    # ── Duration by type ──
    lines.append("## How long things took")
    lines.append("")
    lines.append(
        "Typical (p50) is what most operations look like; bad-case (p95) is the one-in-twenty "
        "worst case; worst is the single slowest thing seen."
    )
    lines.append("")
    dur_rows = [(t, s) for t, s in digest["durationStatsByType"].items() if s["count"] > 0]
    if not dur_rows:
        lines.append("No timed operations were recorded.")
    else:
        lines.append("| Operation | Count | Typical (p50) | Bad case (p95) | Worst |")
        lines.append("|---|---|---|---|---|")
        for t in KNOWN_TYPES:
            s = digest["durationStatsByType"][t]
            if s["count"] == 0:
                continue
            lines.append(
                f"| {t} | {s['count']} | {_fmt_ms(s['p50'])} | {_fmt_ms(s['p95'])} | {_fmt_ms(s['max'])} |"
            )
    lines.append("")

    lines.append("## How long things took, by world")
    lines.append("")
    tw_rows = digest["durationStatsByTypeWorld"]
    if not tw_rows:
        lines.append("No timed operations were recorded.")
    else:
        lines.append("| Operation | World | Count | Typical (p50) | Bad case (p95) | Worst |")
        lines.append("|---|---|---|---|---|---|")
        for r in tw_rows:
            lines.append(
                f"| {r['type']} | {r['world']} | {r['count']} | {_fmt_ms(r['p50'])} | "
                f"{_fmt_ms(r['p95'])} | {_fmt_ms(r['max'])} |"
            )
    lines.append("")

    # ── Failures ──
    lines.append("## Failures")
    lines.append("")
    fails = digest["failures"]
    if fails["totalFailEvents"] == 0:
        lines.append(f"No failures recorded - 0 fail events out of {cov['totalEventsValid']} total.")
    else:
        lines.append(f"{fails['totalFailEvents']} failure event(s) total.")
        lines.append("")
        lines.append("| Subject | Times it failed | First time | Last time |")
        lines.append("|---|---|---|---|")
        for r in fails["bySubject"]:
            lines.append(f"| {r['subject']} | {r['count']} | {r['firstAt']} | {r['lastAt']} |")
    lines.append("")

    # ── Memory ──
    lines.append("## Memory")
    lines.append("")
    mem = digest["memory"]
    if mem["sampleCount"] == 0:
        lines.append("No memory samples were recorded.")
    else:
        lines.append(f"- Start: {_fmt_mb(mem['startMb'])}")
        lines.append(f"- End: {_fmt_mb(mem['endMb'])}")
        lines.append(f"- Peak: {_fmt_mb(mem['peakMb'])}")
        slope = mem["growthSlopeMbPerHour"]
        if slope is None:
            lines.append("- Trend: not enough samples (or not enough time between them) to compute one.")
        elif abs(slope) < 0.05:
            lines.append("- Trend: flat, no meaningful change over this run.")
        elif slope > 0:
            lines.append(f"- Trend: climbing, about {slope:.1f} MB per hour over this run.")
        else:
            lines.append(f"- Trend: dropping, about {abs(slope):.1f} MB per hour over this run.")
    lines.append("")

    # ── Dropped frames ──
    lines.append("## Dropped frames")
    lines.append("")
    frames = digest["droppedFrameEpisodes"]
    if frames["episodeCount"] == 0:
        lines.append("No dropped-frame episodes recorded.")
    else:
        lines.append(
            f"{frames['episodeCount']} episode(s), {int(frames['totalDroppedFrames'])} dropped "
            "frame(s) total."
        )
        lines.append("")
        lines.append("| Subject | When | Dropped frames |")
        lines.append("|---|---|---|")
        for ep in frames["episodes"]:
            lines.append(f"| {ep['subject']} | {ep['at']} | {int(ep['droppedFrames'])} |")
    lines.append("")

    # ── Top slowest ──
    lines.append("## Top 10 slowest things")
    lines.append("")
    top = digest["topSlowestSubjects"]
    if not top["shown"]:
        lines.append("No timed operations were recorded.")
    else:
        total = top["totalSubjectsWithDurationData"]
        if total > len(top["shown"]):
            lines.append(f"(showing the worst {len(top['shown'])} of {total} timed subjects)")
            lines.append("")
        lines.append("| Subject | Worst time seen | Operation |")
        lines.append("|---|---|---|")
        for r in top["shown"]:
            lines.append(f"| {r['subject']} | {_fmt_ms(r['worstDurationMs'])} | {r['type']} |")
    lines.append("")

    _append_warning_section(lines, digest["warningGate"])
    return "\n".join(lines).rstrip() + "\n"


def _append_warning_section(lines: List[str], verdict: Dict[str, Any]) -> None:
    lines.append("## Warning gate (W0)")
    lines.append("")
    if not verdict["available"]:
        lines.append("Not available for this run - no warnings.txt was found.")
    elif verdict["clean"]:
        lines.append("Clean - no unsuppressed Qt warnings, criticals, or fatals during this run.")
    else:
        lines.append(f"Failed - {verdict['failCount']} unsuppressed warning line(s):")
        lines.append("")
        for l in verdict["failLines"]:
            lines.append(f"- {l}")
    lines.append("")


# ───────────────────────────── CLI ─────────────────────────────


def run(session_root: str, events_file: Optional[str], warnings_file: Optional[str],
        out_dir: Optional[str], label: Optional[str]) -> Dict[str, Any]:
    root = Path(session_root)
    events_result = load_events(root, events_file)
    warning_verdict = load_warning_verdict(root, warnings_file)
    digest = build_digest(events_result, warning_verdict, label=label)
    markdown = render_markdown(digest)

    target_dir = Path(out_dir) if out_dir else root
    target_dir.mkdir(parents=True, exist_ok=True)
    (target_dir / "soak-digest.md").write_text(markdown, encoding="utf-8", newline="\n")
    (target_dir / "soak-digest.json").write_text(
        json.dumps(digest, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    return digest


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Turn a soak session's events.jsonl into a triage digest.")
    parser.add_argument("session_root", help="Directory identifying the soak session.")
    parser.add_argument("--events-file", default=None, help="Explicit events.jsonl path override.")
    parser.add_argument("--warnings-file", default=None, help="Explicit warnings.txt path override.")
    parser.add_argument("--out-dir", default=None, help="Where to write soak-digest.{md,json} (default: session_root).")
    parser.add_argument("--label", default=None, help="Optional human-readable run label for the report header.")
    args = parser.parse_args(argv)

    run(args.session_root, args.events_file, args.warnings_file, args.out_dir, args.label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
