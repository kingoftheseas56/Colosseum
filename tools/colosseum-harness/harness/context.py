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

from .repo import HarnessError, normalize_repo_paths, repo_snapshot
from .intelligence import entry_aliases, inspect_map, load_map, map_entries, map_freshness

AUTHORITY_EXCERPT_MAX_BYTES = 8192
AUTHORITY_EXCERPT_MAX_LINES = 80

def _task_mentions_selector(task: str, selector: str) -> bool:
    needle = selector.strip().casefold()
    if not needle:
        return False
    haystack = task.casefold()
    if re.fullmatch(r"[a-z0-9]+", needle):
        return re.search(rf"(?<![a-z0-9]){re.escape(needle)}(?![a-z0-9])", haystack) is not None
    return needle in haystack

def resolve_preflight_root(explicit: Path | str | None = None) -> Path | None:
    raw = explicit or os.environ.get("COLOSSEUM_PREFLIGHT_ROOT")
    if raw is None:
        return None
    try:
        candidate = Path(raw).expanduser().resolve()
    except OSError:
        return None
    return candidate if candidate.is_dir() else None

def bounded_preflight_evidence(
    item: dict[str, Any],
    preflight_root: Path | str | None,
) -> dict[str, Any]:
    rel = item.get("path")
    if not isinstance(rel, str) or not rel.strip():
        raise HarnessError(
            "MAP_INVALID",
            "Mapped Preflight context path must be a non-empty string.",
            {"context": item},
        )
    rel_path = Path(rel)
    if rel_path.is_absolute() or ".." in rel_path.parts:
        raise HarnessError(
            "MAP_INVALID",
            f"Mapped Preflight context path escapes its root: {rel}",
            {"path": rel},
        )

    resolved_root = resolve_preflight_root(preflight_root)
    if resolved_root is None:
        return {
            "available": False,
            "code": "PREFLIGHT_UNAVAILABLE",
            "path": rel.replace("\\", "/"),
        }

    candidate = (resolved_root / rel_path).resolve()
    try:
        candidate.relative_to(resolved_root)
    except ValueError as exc:
        raise HarnessError(
            "MAP_INVALID",
            f"Mapped Preflight context path escapes its root: {rel}",
            {"path": rel},
        ) from exc

    if not candidate.is_file():
        return {
            "available": False,
            "code": "PREFLIGHT_EVIDENCE_UNAVAILABLE",
            "path": rel.replace("\\", "/"),
        }

    parts: list[str] = []
    used_bytes = 0
    truncated = False
    line_count = 0
    with candidate.open("r", encoding="utf-8-sig", errors="replace") as handle:
        for line in handle:
            if line_count >= AUTHORITY_EXCERPT_MAX_LINES:
                truncated = True
                break
            encoded = line.encode("utf-8")
            remaining = AUTHORITY_EXCERPT_MAX_BYTES - used_bytes
            if remaining <= 0:
                truncated = True
                break
            if len(encoded) > remaining:
                fragment = encoded[:remaining].decode("utf-8", errors="ignore")
                if fragment:
                    parts.append(fragment)
                    used_bytes += len(fragment.encode("utf-8"))
                truncated = True
                break
            parts.append(line)
            used_bytes += len(encoded)
            line_count += 1

    excerpt = "".join(parts)
    return {
        "available": True,
        "code": "OK",
        "path": rel.replace("\\", "/"),
        "excerpt": excerpt,
        "truncated": truncated,
        "excerptBytes": len(excerpt.encode("utf-8")),
        "excerptLines": len(excerpt.splitlines()),
        "sourceSizeBytes": candidate.stat().st_size,
    }

def context_for_task(
    root: Path,
    map_path: str | None,
    task: str,
    paths: list[str] | None = None,
    domain: str | None = None,
    preflight_root: Path | str | None = None,
) -> dict[str, Any]:
    if not isinstance(task, str) or not task.strip():
        raise HarnessError("INVALID_TASK", "task must not be empty.")

    resolved_map_path, doc = load_map(map_path)
    freshness = map_freshness(root, doc)
    if freshness.get("state") != "FRESH":
        raise HarnessError(
            "MAP_STALE",
            "The intelligence map is not fresh enough for authoritative task context.",
            {
                "map": str(resolved_map_path),
                "freshness": freshness,
                "nextActions": [
                    "Use live source and direct authorities while the map is stale.",
                    "Refresh and validate the intelligence map at a stable integration boundary.",
                ],
            },
        )

    selected: dict[str, dict[str, Any]] = {}
    unresolved_paths: list[str] = []

    def add_domain(entry: dict[str, Any], reason: dict[str, Any]) -> None:
        domain_id = str(entry.get("id", "")).strip()
        if not domain_id:
            return
        existing = selected.setdefault(
            domain_id,
            {"domain": entry, "reasons": []},
        )
        existing["reasons"].append(reason)

    if domain:
        resolved = inspect_map(
            doc,
            domain,
            current_head=repo_snapshot(root)["head"],
            freshness_state=str(freshness.get("state")),
        )
        entry = resolved.get("domain")
        if not isinstance(entry, dict):
            raise HarnessError(
                "DOMAIN_AMBIGUOUS",
                f"Explicit domain did not resolve uniquely: {domain}",
                {"domain": domain, "candidates": resolved.get("candidates", [])},
            )
        add_domain(entry, {"kind": "explicit-domain", "value": domain})

    explicit_paths = normalize_repo_paths(paths or [], "task path")
    for path in explicit_paths:
        try:
            resolved = inspect_map(
                doc,
                path,
                current_head=repo_snapshot(root)["head"],
                freshness_state=str(freshness.get("state")),
            )
        except HarnessError:
            unresolved_paths.append(path)
            continue
        entry = resolved.get("domain")
        if isinstance(entry, dict):
            add_domain(entry, {"kind": "explicit-path", "path": path})
            continue
        for candidate in resolved.get("candidates", []):
            if isinstance(candidate, dict) and isinstance(candidate.get("domain"), dict):
                add_domain(
                    candidate["domain"],
                    {
                        "kind": "shared-path",
                        "path": path,
                        "score": candidate.get("score"),
                        "evidence": candidate.get("evidence", []),
                    },
                )

    if not selected:
        for _source, entry in map_entries(doc):
            selectors = [entry.get("id"), *entry_aliases(entry)]
            matched = [
                selector for selector in selectors
                if isinstance(selector, str) and _task_mentions_selector(task, selector)
            ]
            if matched:
                add_domain(
                    entry,
                    {
                        "kind": "task-text",
                        "selectors": matched,
                    },
                )

    if not selected:
        raise HarnessError(
            "DOMAIN_UNRESOLVED",
            "No Colosseum domain could be resolved deterministically for the task.",
            {
                "task": task,
                "paths": explicit_paths,
                "unresolvedPaths": unresolved_paths,
                "nextActions": [
                    "Provide an explicit mapped domain or repo-relative path.",
                    "Inspect live source if the task concerns a new unmapped domain.",
                ],
            },
        )

    domains: list[dict[str, Any]] = []
    active_arcs: dict[str, dict[str, Any]] = {}
    constraints: list[str] = []
    verification: list[dict[str, Any]] = []
    degradations: list[dict[str, Any]] = []
    for domain_id in sorted(selected):
        entry = selected[domain_id]["domain"]
        contexts: list[dict[str, Any]] = []
        for raw_context in entry.get("context", []):
            if not isinstance(raw_context, dict):
                continue
            item = dict(raw_context)
            if item.get("scope") == "preflight":
                evidence = bounded_preflight_evidence(item, preflight_root)
                item["evidence"] = evidence
                if not evidence.get("available"):
                    degradations.append({
                        "code": evidence.get("code"),
                        "domain": domain_id,
                        "path": item.get("path"),
                        "role": item.get("role"),
                        "authority": item.get("authority"),
                    })
            contexts.append(item)
        for item in contexts:
            arc_id = item.get("arc_id")
            if isinstance(arc_id, str) and arc_id:
                active_arcs.setdefault(
                    arc_id,
                    {
                        "id": arc_id,
                        "path": item.get("path"),
                        "role": item.get("role"),
                        "authority": item.get("authority"),
                    },
                )
        for value in entry.get("platform_constraints", []):
            if isinstance(value, str) and value not in constraints:
                constraints.append(value)
        for value in entry.get("known_constraints", []):
            if isinstance(value, str) and value not in constraints:
                constraints.append(value)

        domain_verification: list[dict[str, Any]] = []
        for ctest in entry.get("ctests", []):
            if isinstance(ctest, dict) and isinstance(ctest.get("name"), str):
                domain_verification.append({"kind": "ctest", "selector": ctest["name"]})
        for check in entry.get("checks", []):
            if isinstance(check, str):
                domain_verification.append({"kind": "check", "selector": check})
        for journey in entry.get("lanista_scenarios", []):
            if isinstance(journey, str):
                domain_verification.append({"kind": "journey", "selector": journey})
        verification.extend(
            {"domain": domain_id, **item} for item in domain_verification
        )

        domains.append({
            "id": domain_id,
            "displayName": entry.get("display_name"),
            "matchReasons": selected[domain_id]["reasons"],
            "owners": entry.get("owners", []),
            "context": contexts,
            "verification": domain_verification,
        })

    return {
        "task": task,
        "map": str(resolved_map_path),
        "freshness": freshness,
        "domains": domains,
        "ambiguous": len(domains) > 1,
        "activeArcs": [active_arcs[key] for key in sorted(active_arcs)],
        "knownConstraints": constraints,
        "verification": verification,
        "degradations": degradations,
        "unresolvedPaths": unresolved_paths,
    }
