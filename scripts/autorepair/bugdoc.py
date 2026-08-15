#!/usr/bin/env python3
"""
bugdoc.py - the document-only autonomy mode's capstone artifact (bug.md).

Hemanth directive, 2026-08-15: "can we have the night watch and guardian loop only
for documenting bugs rather than fixing them for now? [...] we can just have the
guardian pick up what the night watch flagged and document it through github issues
or just through local mds." This module is the local-md half of that: when
policy.autonomyLevel is "document-only", the orchestrator stops after diagnosis and
renders the incident's evidence into a standalone bug document a human (or a future
GitHub-issue filer) can read without touching any stage JSON.

Pure module, stdlib only (house pattern - policy.py, orchestrator.py). No I/O except
write_bug_doc()'s single write; render_bug_doc() is a pure function over the three
dicts the orchestrator already holds (incident, triage result, diagnosis result).
Defensive by design: every field is read with .get() and rendered as "<not
recorded>" when absent - a bug document must degrade honestly, never crash the
terminal state it decorates, and never invent fields the stages did not produce.

Hemanth-language discipline matches orchestrator.render_report(): plain prose, no
color, no emoji - self-checked here with the same regex (refused, never shipped).
"""

from __future__ import annotations

import json
import re
from datetime import datetime
from pathlib import Path
from typing import Any

# Same character classes as orchestrator._EMOJI_RE (copied deliberately, not
# imported: bugdoc must stay importable even if the orchestrator module moves -
# the two guards must be able to disagree loudly during review, not silently
# share a drifting definition through a live import).
_EMOJI_RE = re.compile(
    "[\U0001F300-\U0001FAFF\U00002600-\U000027BF\U0001F1E6-\U0001F1FF✀-➿☀-⛿]"
)

BUG_DOC_FILE_NAME = "bug.md"

_MISSING = "<not recorded>"


def _text(value: Any) -> str:
    """None-safe scalar rendering: JSON-ish for containers, stripped str otherwise."""
    if value is None:
        return _MISSING
    if isinstance(value, str):
        return value.strip() or _MISSING
    if isinstance(value, (dict, list)):
        return json.dumps(value, indent=2, ensure_ascii=False)
    return str(value)


def render_bug_doc(
    incident: dict[str, Any],
    triage: dict[str, Any],
    diagnosis: dict[str, Any],
    *,
    generated_at: datetime,
) -> str:
    """
    Pure Markdown renderer for bug.md. Sections mirror what a bug report needs a
    human to act on: identity, how it was observed (triage), why it happens
    (diagnosis), the proposed-but-NOT-applied repair, and where the full evidence
    lives. The "documented, not fixed" status line is mandatory - the document must
    never be mistakable for a fix.
    """
    incident_id = _text(incident.get("id"))
    scenario = _text(incident.get("scenario"))
    scenario_name = _text(incident.get("scenarioName"))
    created_at = _text(incident.get("createdAt"))
    fingerprint = _text(incident.get("fingerprint"))
    base_sha = _text(incident.get("baseSha"))

    step = incident.get("failingStep") if isinstance(incident.get("failingStep"), dict) else {}
    step_label = _text(step.get("label"))
    step_detail = _text(step.get("detail"))
    step_expected = _text(step.get("expected"))
    step_got = _text(step.get("got"))

    verdict = _text(triage.get("verdict"))
    consistency = (
        triage.get("failingStepConsistency")
        if isinstance(triage.get("failingStepConsistency"), dict)
        else {}
    )
    reproduced = _text(triage.get("reproduced"))

    root = (
        diagnosis.get("rootCause")
        if isinstance(diagnosis.get("rootCause"), dict)
        else {}
    )
    confidence = _text(diagnosis.get("confidence"))

    lines: list[str] = [
        f"# Bug - {incident_id}",
        "",
        f"Scenario: {scenario_name} ({scenario})",
        f"Failing step: {step_label}",
        f"Status: DOCUMENTED ONLY - triaged and diagnosed, deliberately not fixed",
        f"(policy.autonomyLevel is document-only; no repair was attempted or applied).",
        "",
        f"Generated {generated_at.isoformat()}.",
        "",
        "## How it was observed",
        "",
        (
            f"Triage verdict {verdict} (reproduced: {reproduced}; failing-step "
            f"consistency {consistency.get('count', _MISSING)} of "
            f"{consistency.get('totalRuns', _MISSING)} runs named the same step, "
            f"confirm threshold {consistency.get('confirmThreshold', _MISSING)})."
        ),
        "",
        (
            f"Step detail: {step_detail}. Expected {step_expected}; got {step_got}. "
            f"Incident opened {created_at} at base {base_sha} "
            f"(fingerprint {fingerprint})."
        ),
        "",
        "## Why it happens (diagnosis)",
        "",
        f"Confidence: {confidence}.",
        "",
        f"Observed: {_text(diagnosis.get('observed'))}",
        "",
        f"Expected: {_text(diagnosis.get('expected'))}",
        "",
        f"Root cause ({_text(root.get('file'))}:{_text(root.get('line'))}): "
        f"{_text(root.get('claim'))}",
        "",
        f"Seam: {_text(diagnosis.get('seam'))}",
        "",
        "## Proposed repair (NOT applied)",
        "",
        _text(diagnosis.get("proposedRepair")),
        "",
        "## Evidence",
        "",
        "- incident.json - the minted incident packet (this directory)",
        "- triage.json - the reproduction runs and their verdict",
        "- diagnosis.json - the full diagnosis record the sections above summarize",
        "- failure.log / journey.json / stdout.log - the captured failing run",
        "- grabs/, screen.png, ui-tree.json - what the screen showed at failure",
    ]

    body = "\n".join(lines).rstrip() + "\n"

    found = _EMOJI_RE.findall(body)
    if found:
        raise ValueError(
            "REFUSED (Hemanth-language: no color, no emoji, no taglines) - the "
            f"rendered bug.md would contain emoji/pictographic character(s): {found!r}"
        )
    return body


def write_bug_doc(
    incident_dir: Path,
    incident: dict[str, Any],
    triage: dict[str, Any],
    diagnosis: dict[str, Any],
    *,
    generated_at: datetime,
) -> Path:
    """Render and atomically place bug.md next to the stage files. Returns its path."""
    text = render_bug_doc(incident, triage, diagnosis, generated_at=generated_at)
    path = Path(incident_dir) / BUG_DOC_FILE_NAME
    path.write_text(text, encoding="utf-8")
    return path
