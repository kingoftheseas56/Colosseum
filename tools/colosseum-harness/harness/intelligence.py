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

from .repo import HarnessError, git, norm, normalize_repo_paths, repo_snapshot

def load_map(path: str | None) -> tuple[Path, dict[str, Any]]:
    raw = path or os.environ.get("COLOSSEUM_MAP")
    if not raw:
        raise HarnessError("MAP_REQUIRED", "inspect requires --map or COLOSSEUM_MAP.")
    map_path = Path(raw).expanduser().resolve()
    if not map_path.is_file():
        raise HarnessError("MAP_NOT_FOUND", f"Map does not exist: {map_path}")

    try:
        doc = json.loads(map_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError("MAP_INVALID", f"Cannot read map: {map_path}", str(exc)) from exc
    if not isinstance(doc, dict):
        raise HarnessError("MAP_INVALID", "Map root must be an object.")
    return map_path, doc

def map_entries(doc: dict[str, Any]):
    for collection_name in ("domains", "entries"):
        collection = doc.get(collection_name)
        if isinstance(collection, list):
            for index, item in enumerate(collection):
                if isinstance(item, dict):
                    yield f"{collection_name}[{index}]", item
        elif isinstance(collection, dict):
            for key, item in collection.items():
                if isinstance(item, dict):
                    clone = dict(item)
                    clone.setdefault("id", key)
                    yield f"{collection_name}.{key}", clone

def entry_aliases(entry: dict[str, Any]) -> list[str]:
    aliases = entry.get("aliases", [])
    if not isinstance(aliases, list):
        return []
    return [value for value in aliases if isinstance(value, str) and value.strip()]

def entry_path_records(entry: dict[str, Any]) -> list[tuple[str, str, int]]:
    records: list[tuple[str, str, int]] = []

    def add(kind: str, values: Any, score: int) -> None:
        if not isinstance(values, list):
            return
        for value in values:
            if isinstance(value, str) and value.strip():
                records.append((kind, value, score))

    add("entry_point", entry.get("entry_points", []), 120)
    owners = entry.get("owners", [])
    if isinstance(owners, list):
        add(
            "owner",
            [owner.get("path") for owner in owners if isinstance(owner, dict)],
            110,
        )
    add("source_root", entry.get("source_roots", []), 100)
    add("related_file", entry.get("related_files", []), 90)
    add("path", entry.get("paths", []), 90)
    if isinstance(entry.get("path"), str) and entry["path"].strip():
        records.append(("path", entry["path"], 90))
    return records

def entry_paths(entry: dict[str, Any]) -> list[str]:
    return [path for _kind, path, _score in entry_path_records(entry)]

def rank_path_candidates(
    entries: list[tuple[str, dict[str, Any]]],
    query_path: str,
) -> list[dict[str, Any]]:
    ranked: list[dict[str, Any]] = []
    for source, entry in entries:
        evidence: list[dict[str, Any]] = []
        score = 0
        for kind, path, base_score in entry_path_records(entry):
            normalized = norm(path)
            if query_path == normalized:
                relation = "exact"
                item_score = base_score
            elif query_path.startswith(normalized + "/"):
                relation = "prefix"
                item_score = max(1, base_score - 35)
            else:
                continue
            score += item_score
            evidence.append({
                "kind": kind,
                "path": path,
                "relation": relation,
                "score": item_score,
            })
        if evidence:
            ranked.append({
                "source": source,
                "score": score,
                "domain": entry,
                "evidence": evidence,
            })

    ranked.sort(
        key=lambda item: (
            -int(item["score"]),
            str(item["domain"].get("id", item["source"])).casefold(),
        )
    )
    for index, item in enumerate(ranked, start=1):
        item["rank"] = index
    return ranked

def inspect_suggestions(
    doc: dict[str, Any],
    target: str,
    limit: int = 5,
) -> list[dict[str, Any]]:
    query = norm(target)
    query_name = Path(target.replace("\\", "/")).name.casefold()
    candidates: list[tuple[float, str, str, str]] = []

    for source, entry in map_entries(doc):
        domain_id = str(entry.get("id") or source)
        selectors = [
            ("domain", domain_id),
            *[("alias", alias) for alias in entry_aliases(entry)],
            *[("path", path) for path in entry_paths(entry)],
        ]
        for kind, value in selectors:
            normalized = norm(value)
            ratio = difflib.SequenceMatcher(None, query, normalized).ratio()
            if kind == "path" and query_name:
                value_name = Path(value.replace("\\", "/")).name.casefold()
                if value_name == query_name:
                    ratio += 0.65
            if ratio >= 0.35:
                candidates.append((ratio, value, kind, domain_id))

    unique: dict[str, tuple[float, str, str, str]] = {}
    for item in candidates:
        key = item[1].casefold()
        if key not in unique or item[0] > unique[key][0]:
            unique[key] = item
    ordered = sorted(unique.values(), key=lambda item: (-item[0], item[1].casefold()))
    return [
        {"target": value, "kind": kind, "domain": domain_id}
        for _score, value, kind, domain_id in ordered[:limit]
    ]

def map_basis(doc: dict[str, Any]) -> tuple[str | None, str | None]:
    basis = doc.get("repo_basis", {})
    if not isinstance(basis, dict):
        return None, None
    head = basis.get("head")
    branch = basis.get("branch")
    return (
        head if isinstance(head, str) and head else None,
        branch if isinstance(branch, str) and branch else None,
    )

def inspect_map(
    doc: dict[str, Any],
    target: str,
    current_head: str | None = None,
    freshness_state: str | None = None,
) -> dict[str, Any]:
    selector = target.strip()
    query_alias = selector.casefold()
    query_path = norm(selector)
    entries = list(map_entries(doc))
    basis_head, basis_branch = map_basis(doc)
    stale = (
        freshness_state != "FRESH"
        if freshness_state is not None
        else None if current_head is None or basis_head is None
        else current_head != basis_head
    )

    id_matches = [
        (source, entry) for source, entry in entries
        if isinstance(entry.get("id"), str) and selector.casefold() == entry["id"].casefold()
    ]
    if id_matches:
        matches = id_matches
        match = {"kind": "id", "value": selector}
        routing_evidence: list[dict[str, Any]] = []
    else:
        alias_matches = []
        for source, entry in entries:
            for alias in entry_aliases(entry):
                if alias.casefold() == query_alias:
                    alias_matches.append((source, entry, alias))
        if alias_matches:
            matches = [(source, entry) for source, entry, _alias in alias_matches]
            match = {"kind": "alias", "value": alias_matches[0][2]}
            routing_evidence = []
        else:
            ranked = rank_path_candidates(entries, query_path)
            if not ranked:
                suggestions = inspect_suggestions(doc, target)
                raise HarnessError(
                    "INSPECT_NOT_FOUND",
                    f"No exact domain, alias, or mapped path match for: {target}",
                    {
                        "query": target,
                        "suggestions": suggestions,
                        "nextActions": [
                            "Inspect one of the suggested targets.",
                            "Use a known domain id or alias.",
                            "Refresh the intelligence map if the live repository gained a new owner path.",
                        ],
                    },
                )
            if len(ranked) > 1:
                return {
                    "map_id": doc.get("map_id"),
                    "basis_head": basis_head,
                    "basis_branch": basis_branch,
                    "current_head": current_head,
                    "stale": stale,
                    "match": {"kind": "shared-path", "value": selector},
                    "candidates": ranked,
                }
            winner = ranked[0]
            matches = [(winner["source"], winner["domain"])]
            best = winner["evidence"][0]
            match = {"kind": "path", "value": best["path"]}
            routing_evidence = winner["evidence"]

    unique: dict[str, dict[str, Any]] = {}
    for source, entry in matches:
        unique[str(entry.get("id") or source)] = entry
    if len(unique) != 1:
        candidates = []
        for index, (domain_id, domain) in enumerate(
            sorted(unique.items(), key=lambda item: item[0].casefold()),
            start=1,
        ):
            candidates.append({
                "rank": index,
                "score": 0,
                "domain": domain,
                "evidence": [{"kind": match["kind"], "value": match["value"]}],
            })
        return {
            "map_id": doc.get("map_id"),
            "basis_head": basis_head,
            "basis_branch": basis_branch,
            "current_head": current_head,
            "stale": stale,
            "match": {"kind": "ambiguous-selector", "value": selector},
            "candidates": candidates,
        }
    domain = next(iter(unique.values()))
    return {
        "map_id": doc.get("map_id"),
        "basis_head": basis_head,
        "basis_branch": basis_branch,
        "current_head": current_head,
        "stale": stale,
        "match": match,
        "routing_evidence": routing_evidence,
        "domain": domain,
    }

def semantic_worktree_fingerprint(
    root: Path,
    watch_scopes: list[str],
    algorithm: str = "sha256-git-semantic-v2",
    head_override: str | None = None,
) -> str:
    scopes = sorted(
        normalize_repo_paths(watch_scopes, "watch scope"),
        key=str.casefold,
    )
    if not scopes:
        raise HarnessError("MAP_INVALID", "semantic_worktree.watch_scopes must not be empty.")

    if algorithm == "sha256-git-semantic-v2":
        proc = subprocess.run(
            [
                "git", "-C", str(root), "ls-files", "-co", "--exclude-standard", "-z",
                "--", *scopes,
            ],
            capture_output=True,
            check=False,
        )
        if proc.returncode:
            raise HarnessError(
                "GIT_ERROR",
                "Cannot compute semantic working-tree fingerprint.",
                proc.stderr.decode("utf-8", errors="replace").strip(),
            )

        paths = sorted(
            {
                part.decode("utf-8", errors="surrogateescape").replace("\\", "/").strip("/")
                for part in proc.stdout.split(b"\0")
                if part
            },
            key=str.casefold,
        )
        content: dict[str, str | None] = {}
        for normalized in paths:
            candidate = root / Path(normalized)
            content[normalized] = (
                hashlib.sha256(candidate.read_bytes()).hexdigest()
                if candidate.is_file()
                else None
            )

        payload = {
            "algorithm": algorithm,
            "scopes": scopes,
            "content": content,
        }
        encoded = json.dumps(
            payload,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()

    if algorithm != "sha256-git-semantic-v1":
        raise HarnessError(
            "MAP_INVALID",
            f"Unsupported semantic_worktree algorithm: {algorithm}",
        )

    proc = subprocess.run(
        [
            "git", "-C", str(root), "status", "--porcelain=v1", "-z",
            "--untracked-files=all", "--", *scopes,
        ],
        capture_output=True,
        check=False,
    )
    if proc.returncode:
        raise HarnessError(
            "GIT_ERROR",
            "Cannot compute semantic working-tree fingerprint.",
            proc.stderr.decode("utf-8", errors="replace").strip(),
        )

    records = sorted(
        [
            part.decode("utf-8", errors="surrogateescape")
            for part in proc.stdout.split(b"\0")
            if part
        ],
        key=str.casefold,
    )
    content_hashes: dict[str, str] = {}
    for record in records:
        raw_path = record[3:] if len(record) >= 4 and record[2] == " " else record
        normalized = raw_path.replace("\\", "/").strip("/")
        if not normalized:
            continue
        candidate = root / Path(normalized)
        if candidate.is_file():
            content_hashes[normalized] = hashlib.sha256(candidate.read_bytes()).hexdigest()

    payload = {
        "algorithm": algorithm,
        "head": head_override or git(root, "rev-parse", "HEAD"),
        "scopes": scopes,
        "records": records,
        "content": content_hashes,
    }
    encoded = json.dumps(payload, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()

def map_freshness(root: Path, doc: dict[str, Any]) -> dict[str, Any]:
    snapshot = repo_snapshot(root)
    basis = doc.get("repo_basis")
    if not isinstance(basis, dict):
        return {"state": "INVALID", "authoritative": False, "reason": "repo_basis is missing or invalid."}

    basis_head = basis.get("head")
    basis_branch = basis.get("branch")
    semantic = basis.get("semantic_worktree")
    if not isinstance(semantic, dict):
        if basis_head != snapshot["head"] or (
            isinstance(basis_branch, str) and basis_branch and basis_branch != snapshot["branch"]
        ):
            return {
                "state": "HEAD_DRIFT",
                "authoritative": False,
                "basisHead": basis_head,
                "currentHead": snapshot["head"],
                "basisBranch": basis_branch,
                "currentBranch": snapshot["branch"],
            }
        return {
            "state": "UNKNOWN",
            "authoritative": False,
            "basisHead": basis_head,
            "currentHead": snapshot["head"],
            "reason": "Map has no semantic working-tree fingerprint.",
        }

    algorithm = semantic.get("algorithm")
    if isinstance(basis_branch, str) and basis_branch and basis_branch != snapshot["branch"]:
        return {
            "state": "BRANCH_DRIFT",
            "authoritative": False,
            "basisHead": basis_head,
            "currentHead": snapshot["head"],
            "basisBranch": basis_branch,
            "currentBranch": snapshot["branch"],
        }

    scopes = semantic.get("watch_scopes")
    expected = semantic.get("fingerprint")
    if (
        algorithm not in {"sha256-git-semantic-v1", "sha256-git-semantic-v2"}
        or not isinstance(scopes, list)
        or not all(isinstance(value, str) for value in scopes)
        or not isinstance(expected, str)
        or not re.fullmatch(r"[0-9a-fA-F]{64}", expected)
    ):
        return {
            "state": "INVALID",
            "authoritative": False,
            "reason": "semantic_worktree configuration is invalid.",
        }

    actual = semantic_worktree_fingerprint(
        root,
        scopes,
        algorithm,
        head_override=(
            basis_head
            if algorithm == "sha256-git-semantic-v1" and isinstance(basis_head, str)
            else None
        ),
    )
    state = "FRESH" if actual.casefold() == expected.casefold() else "WORKTREE_DRIFT"
    return {
        "state": state,
        "authoritative": state == "FRESH",
        "algorithm": algorithm,
        "watchScopes": normalize_repo_paths(scopes, "watch scope"),
        "expectedFingerprint": expected,
        "actualFingerprint": actual,
        "basisHead": basis_head,
        "currentHead": snapshot["head"],
        "headMoved": basis_head != snapshot["head"],
    }
