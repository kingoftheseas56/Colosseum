#!/usr/bin/env python3
"""Validate the Arc 51 Colosseum intelligence map against a live checkout."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

ID_RE = re.compile(r"^[a-z0-9-]+$")
ADD_TEST_RE = re.compile(r"add_test\s*\(\s*NAME\s+([^\s\)]+)", re.I | re.S)
SET_TESTS_RE = re.compile(r"set_tests_properties\s*\((.*?)\)", re.I | re.S)
SET_PROPERTY_RE = re.compile(
    r'set_property\s*\(\s*TEST\s+(.*?)\s+PROPERTY\s+LABELS\s+"([^"]*)"\s*\)',
    re.I | re.S,
)
LABELS_RE = re.compile(r'LABELS\s+"([^"]*)"', re.I)
CONTEXT_ROLES = {"arc-status", "contract", "design", "verification", "handoff", "governance", "reference"}
CONTEXT_AUTHORITIES = {"binding", "supporting"}


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{path.name}: root must be an object")
    return data


def cmake_inventory(repo_root: Path) -> tuple[set[str], dict[str, set[str]]]:
    parts = []
    for rel in ("native/CMakeLists.txt", "tests/CMakeLists.txt"):
        p = repo_root / rel
        if not p.is_file():
            raise FileNotFoundError(rel)
        parts.append(p.read_text(encoding="utf-8", errors="replace"))
    text = "\n".join(parts)
    names = set(ADD_TEST_RE.findall(text))
    labels: dict[str, set[str]] = {}

    for match in SET_TESTS_RE.finditer(text):
        body = match.group(1)
        split = re.split(r"\bPROPERTIES\b", body, maxsplit=1, flags=re.I)
        if len(split) != 2:
            continue
        tests_part, props = split
        label_match = LABELS_RE.search(props)
        if not label_match:
            continue
        value = {x for x in label_match.group(1).split(";") if x}
        for name in re.findall(r"[^\s]+", tests_part):
            if name in names:
                labels.setdefault(name, set()).update(value)

    for match in SET_PROPERTY_RE.finditer(text):
        value = {x for x in match.group(2).split(";") if x}
        for name in re.findall(r"[^\s]+", match.group(1)):
            if name in names:
                labels.setdefault(name, set()).update(value)

    return names, labels


def git_value(repo_root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def validate_map(
    data: dict[str, Any],
    repo_root: Path,
    preflight_root: Path | None = None,
    check_head: bool = True,
) -> dict[str, Any]:
    errors: list[str] = []
    warnings: list[str] = []
    stats = {
        "domains": 0,
        "owners": 0,
        "inferred_owners": 0,
        "ctests": 0,
        "checks": 0,
        "lanista_scenarios": 0,
        "repo_paths_checked": 0,
        "preflight_paths_checked": 0,
        "provenance_anchors_checked": 0,
    }

    def err(message: str) -> None:
        errors.append(message)

    def repo_path(rel: Any, where: str) -> Path | None:
        if not isinstance(rel, str) or not rel:
            err(f"{where}: path must be a non-empty string")
            return None
        candidate = Path(rel)
        if candidate.is_absolute() or ".." in candidate.parts:
            err(f"{where}: path must be repo-relative: {rel!r}")
            return None
        full = (repo_root / candidate).resolve()
        try:
            full.relative_to(repo_root.resolve())
        except ValueError:
            err(f"{where}: path escapes repo: {rel!r}")
            return None
        stats["repo_paths_checked"] += 1
        if not full.exists():
            err(f"{where}: missing repo path: {rel}")
            return None
        return full

    required = {"schema_version", "map_id", "repo_basis", "donors", "known_gaps", "domains"}
    missing = sorted(required - data.keys())
    if missing:
        err("map missing required keys: " + ", ".join(missing))
    if data.get("schema_version") != 1:
        err("schema_version must be 1")

    basis = data.get("repo_basis")
    if not isinstance(basis, dict):
        err("repo_basis must be an object")
        basis = {}
    if basis.get("path_semantics") != "repo-relative":
        err("repo_basis.path_semantics must be repo-relative")
    for i, rel in enumerate(basis.get("source_of_truth", []) if isinstance(basis.get("source_of_truth"), list) else []):
        repo_path(rel, f"repo_basis.source_of_truth[{i}]")

    semantic = basis.get("semantic_worktree")
    if semantic is not None:
        if not isinstance(semantic, dict):
            err("repo_basis.semantic_worktree must be an object")
        else:
            if semantic.get("algorithm") not in {
                "sha256-git-semantic-v1",
                "sha256-git-semantic-v2",
            }:
                err(
                    "repo_basis.semantic_worktree.algorithm must be "
                    "sha256-git-semantic-v1 or sha256-git-semantic-v2"
                )
            watch_scopes = semantic.get("watch_scopes")
            if not isinstance(watch_scopes, list) or not watch_scopes:
                err("repo_basis.semantic_worktree.watch_scopes must be a non-empty array")
            else:
                for i, rel in enumerate(watch_scopes):
                    repo_path(rel, f"repo_basis.semantic_worktree.watch_scopes[{i}]")
            fingerprint = semantic.get("fingerprint")
            if not isinstance(fingerprint, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", fingerprint):
                err("repo_basis.semantic_worktree.fingerprint must be a SHA-256 hex digest")

    donors = data.get("donors")
    if isinstance(donors, dict):
        for key, rel in donors.items():
            repo_path(rel, f"donors.{key}")
    else:
        err("donors must be an object")

    if check_head:
        try:
            actual_head = git_value(repo_root, "rev-parse", "HEAD")
            actual_branch = git_value(repo_root, "branch", "--show-current")
        except (OSError, subprocess.CalledProcessError) as exc:
            err(f"cannot inspect git basis: {exc}")
        else:
            if actual_head != basis.get("head"):
                algorithm = (
                    semantic.get("algorithm")
                    if isinstance(semantic, dict)
                    else None
                )
                if algorithm in {
                    "sha256-git-semantic-v1",
                    "sha256-git-semantic-v2",
                }:
                    warnings.append(
                        "repo basis HEAD moved under semantic freshness: "
                        f"map={basis.get('head')} current={actual_head}"
                    )
                else:
                    err(f"repo basis HEAD mismatch: map={basis.get('head')} current={actual_head}")
            if actual_branch and actual_branch != basis.get("branch"):
                err(f"repo basis branch mismatch: map={basis.get('branch')} current={actual_branch}")

    try:
        ctest_names, ctest_labels = cmake_inventory(repo_root)
    except (OSError, FileNotFoundError) as exc:
        err(f"cannot read CMake test inventory: {exc}")
        ctest_names, ctest_labels = set(), {}

    domains = data.get("domains")
    if not isinstance(domains, list) or not domains:
        err("domains must be a non-empty array")
        domains = []
    stats["domains"] = len(domains)

    ids: set[str] = set()
    selector_owner: dict[str, str] = {}
    for index, domain in enumerate(domains):
        where = f"domains[{index}]"
        if not isinstance(domain, dict):
            err(f"{where}: must be an object")
            continue
        domain_id = domain.get("id")
        if not isinstance(domain_id, str) or not ID_RE.fullmatch(domain_id):
            err(f"{where}.id: invalid domain id {domain_id!r}")
            domain_id = f"<invalid-{index}>"
        elif domain_id in ids:
            err(f"{where}.id: duplicate domain id {domain_id}")
        ids.add(domain_id)

        aliases = domain.get("aliases")
        if not isinstance(aliases, list):
            err(f"{where}.aliases: must be an array")
            aliases = []
        for selector in [domain_id, *aliases]:
            if not isinstance(selector, str) or not selector.strip():
                err(f"{where}: selector must be a non-empty string")
                continue
            key = selector.casefold()
            prior = selector_owner.get(key)
            if prior and prior != domain_id:
                err(f"selector collision: {selector!r} belongs to {prior} and {domain_id}")
            selector_owner[key] = domain_id

        for field in ("source_roots", "entry_points"):
            values = domain.get(field)
            if not isinstance(values, list) or not values:
                err(f"{where}.{field}: must be a non-empty array")
                continue
            if len(values) != len(set(values)):
                err(f"{where}.{field}: contains duplicates")
            for i, rel in enumerate(values):
                repo_path(rel, f"{where}.{field}[{i}]")

        related_files = domain.get("related_files", [])
        if not isinstance(related_files, list):
            err(f"{where}.related_files: must be an array when present")
            related_files = []
        if len(related_files) != len(set(related_files)):
            err(f"{where}.related_files: contains duplicates")
        for i, rel in enumerate(related_files):
            repo_path(rel, f"{where}.related_files[{i}]")

        owners = domain.get("owners")
        if not isinstance(owners, list) or not owners:
            err(f"{where}.owners: must be a non-empty array")
            owners = []
        stats["owners"] += len(owners)
        for oi, owner in enumerate(owners):
            ow = f"{where}.owners[{oi}]"
            if not isinstance(owner, dict):
                err(f"{ow}: must be an object")
                continue
            repo_path(owner.get("path"), f"{ow}.path")
            confidence = owner.get("confidence")
            if confidence not in {"observed", "inferred"}:
                err(f"{ow}.confidence: must be observed or inferred")
            if confidence == "inferred":
                stats["inferred_owners"] += 1
                if not isinstance(owner.get("note"), str) or not owner["note"].strip():
                    err(f"{ow}: inferred owner requires an explanatory note")
            provenance = owner.get("provenance")
            if not isinstance(provenance, list) or not provenance:
                err(f"{ow}.provenance: must be a non-empty array")
                continue
            for pi, proof in enumerate(provenance):
                pw = f"{ow}.provenance[{pi}]"
                if not isinstance(proof, dict):
                    err(f"{pw}: must be an object")
                    continue
                full = repo_path(proof.get("path"), f"{pw}.path")
                needle = proof.get("contains")
                if needle is not None:
                    if not isinstance(needle, str) or not needle:
                        err(f"{pw}.contains: must be a non-empty string")
                    elif full and full.is_file():
                        try:
                            haystack = full.read_text(encoding="utf-8-sig", errors="replace")
                        except OSError as exc:
                            err(f"{pw}: cannot read provenance file: {exc}")
                        else:
                            stats["provenance_anchors_checked"] += 1
                            if needle not in haystack:
                                err(f"{pw}: provenance anchor not found: {needle!r}")

        ctests = domain.get("ctests")
        if not isinstance(ctests, list):
            err(f"{where}.ctests: must be an array")
            ctests = []
        stats["ctests"] += len(ctests)
        for ti, test in enumerate(ctests):
            tw = f"{where}.ctests[{ti}]"
            if not isinstance(test, dict) or not isinstance(test.get("name"), str):
                err(f"{tw}: requires name and labels")
                continue
            name = test["name"]
            expected = test.get("labels")
            if not isinstance(expected, list) or any(not isinstance(x, str) for x in expected):
                err(f"{tw}.labels: must be a string array")
                expected = []
            if name not in ctest_names:
                err(f"{tw}: CTest name not registered in current CMake: {name}")
                continue
            missing_labels = sorted(set(expected) - ctest_labels.get(name, set()))
            if missing_labels:
                err(f"{tw}: CTest {name} missing expected labels: {', '.join(missing_labels)}")

        checks = domain.get("checks")
        if not isinstance(checks, list):
            err(f"{where}.checks: must be an array")
            checks = []
        stats["checks"] += len(checks)
        for ci, rel in enumerate(checks):
            repo_path(rel, f"{where}.checks[{ci}]")

        scenarios = domain.get("lanista_scenarios")
        if not isinstance(scenarios, list):
            err(f"{where}.lanista_scenarios: must be an array")
            scenarios = []
        stats["lanista_scenarios"] += len(scenarios)
        for si, rel in enumerate(scenarios):
            scenario_where = f"{where}.lanista_scenarios[{si}]"
            if isinstance(rel, str) and not rel.startswith("tests/lanista_scenarios/"):
                err(f"{scenario_where}: must live under tests/lanista_scenarios")
            full = repo_path(rel, scenario_where)
            if full and full.is_file():
                try:
                    json.loads(full.read_text(encoding="utf-8-sig"))
                except (OSError, json.JSONDecodeError) as exc:
                    err(f"{scenario_where}: invalid scenario JSON: {exc}")

        known_constraints = domain.get("known_constraints", [])
        if (
            not isinstance(known_constraints, list)
            or any(not isinstance(value, str) or not value.strip() for value in known_constraints)
        ):
            err(f"{where}.known_constraints: must be an array of non-empty strings")

        context = domain.get("context")
        if not isinstance(context, list):
            err(f"{where}.context: must be an array")
            context = []
        for ci, item in enumerate(context):
            cw = f"{where}.context[{ci}]"
            if not isinstance(item, dict):
                err(f"{cw}: must be an object")
                continue
            role = item.get("role")
            if role is not None and role not in CONTEXT_ROLES:
                err(f"{cw}.role: must be one of {sorted(CONTEXT_ROLES)}")
            arc_id = item.get("arc_id")
            if arc_id is not None and (not isinstance(arc_id, str) or not arc_id.strip()):
                err(f"{cw}.arc_id: must be a non-empty string")
            authority = item.get("authority")
            if authority is not None and authority not in CONTEXT_AUTHORITIES:
                err(f"{cw}.authority: must be binding or supporting")
            scope = item.get("scope")
            rel = item.get("path")
            if scope == "repo":
                repo_path(rel, f"{cw}.path")
            elif scope == "preflight":
                if not isinstance(rel, str) or not rel:
                    err(f"{cw}.path: must be a non-empty string")
                elif Path(rel).is_absolute() or ".." in Path(rel).parts:
                    err(f"{cw}.path: path escapes preflight root: {rel!r}")
                elif preflight_root is None:
                    warnings.append(f"{cw}: preflight path not checked (no --preflight-root): {rel}")
                else:
                    full = (preflight_root / rel).resolve()
                    try:
                        full.relative_to(preflight_root.resolve())
                    except ValueError:
                        err(f"{cw}.path: path escapes preflight root: {rel!r}")
                        continue
                    stats["preflight_paths_checked"] += 1
                    if not full.exists():
                        err(f"{cw}: missing preflight path: {rel}")
            else:
                err(f"{cw}.scope: must be repo or preflight")

        for field in ("platform_constraints", "relations"):
            values = domain.get(field)
            if not isinstance(values, list) or any(not isinstance(x, str) for x in values):
                err(f"{where}.{field}: must be a string array")

    for index, domain in enumerate(domains):
        if not isinstance(domain, dict):
            continue
        for relation in domain.get("relations", []):
            if relation not in ids:
                err(f"domains[{index}].relations: unknown domain id {relation!r}")

    return {"ok": not errors, "errors": errors, "warnings": warnings, "stats": stats}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", dest="map_path", type=Path, default=Path(__file__).with_name("colosseum-map.json"))
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--preflight-root", type=Path)
    parser.add_argument("--allow-head-drift", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        data = load_json(args.map_path)
        result = validate_map(data, args.repo_root, args.preflight_root, check_head=not args.allow_head_drift)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print("OK" if result["ok"] else "FAILED")
        for message in result["errors"]:
            print(f"ERROR: {message}")
        for message in result["warnings"]:
            print(f"WARNING: {message}")
        print(json.dumps(result["stats"], sort_keys=True))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
