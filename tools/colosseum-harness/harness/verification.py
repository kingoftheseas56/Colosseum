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

from .repo import HarnessError, dirty_paths, norm, normalize_repo_paths, repo_snapshot, run
from .intelligence import inspect_map, load_map, map_basis, map_freshness

SCRIPT_SUFFIXES = {".ps1", ".py", ".mjs", ".js"}

def script_runner(path: Path) -> list[str]:
    suffix = path.suffix.lower()
    if suffix == ".ps1":
        runner = shutil.which("powershell.exe") or shutil.which("powershell") or "powershell.exe"
        return [runner, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(path)]
    if suffix == ".py":
        return [sys.executable, str(path)]
    if suffix in {".mjs", ".js"}:
        return [shutil.which("node") or "node", str(path)]
    raise HarnessError("UNSUPPORTED_TEST", f"Unsupported test script: {path}")

def discover_scripts(root: Path) -> list[Path]:
    tests = root / "tests"
    if not tests.is_dir():
        return []
    return sorted(
        p for p in tests.rglob("test_*")
        if p.is_file() and p.suffix.lower() in SCRIPT_SUFFIXES
    )

def discover_ctest_names(root: Path) -> list[str]:
    names: set[str] = set()
    pattern = re.compile(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_.:+-]+)", re.I)
    for rel in ("tests/CMakeLists.txt", "native/CMakeLists.txt"):
        path = root / rel
        if not path.is_file():
            continue
        try:
            names.update(pattern.findall(path.read_text(encoding="utf-8", errors="replace")))
        except OSError:
            pass
    return sorted(names)

def discover_built_ctest_registry(root: Path) -> tuple[list[str], list[str]]:
    build = root / "native" / "build-msvc"
    registry_files = sorted(build.rglob("CTestTestfile.cmake")) if build.is_dir() else []
    pattern = re.compile(
        r'add_test\s*\(\s*(?:NAME\s+)?(?:"([^"]+)"|\[=*\[([^\]]+)\]=*\]|([^\s\)]+))',
        re.I,
    )
    names: set[str] = set()
    for path in registry_files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for match in pattern.finditer(text):
            name = next((value for value in match.groups() if value), "")
            if name:
                names.add(name)
    return (
        sorted(names),
        [path.relative_to(root).as_posix() for path in registry_files],
    )

def find_ctest_runner(root: Path) -> str | None:
    explicit = os.environ.get("CTEST_COMMAND")
    if explicit and Path(explicit).is_file():
        return str(Path(explicit).resolve())
    on_path = shutil.which("ctest")
    if on_path:
        return on_path

    cache = root / "native" / "build-msvc" / "CMakeCache.txt"
    if cache.is_file():
        try:
            text = cache.read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        match = re.search(r"^CMAKE_CTEST_COMMAND:INTERNAL=(.+)$", text, re.M)
        if match:
            candidate = Path(match.group(1).strip())
            if candidate.is_file():
                return str(candidate.resolve())
    return None

def resolve_test(
    root: Path,
    selector: str,
    selection_reasons: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    query = norm(selector)
    matches: list[dict[str, Any]] = []
    reasons = list(selection_reasons or [])

    for path in discover_scripts(root):
        rel = path.relative_to(root).as_posix()
        keys = {norm(rel), path.name.lower(), path.stem.lower()}
        if query in keys:
            matches.append({
                "kind": "script",
                "name": path.stem,
                "path": rel,
                "argv": script_runner(path),
                "selectedTests": [rel],
                "registration": {
                    "kind": "script-file",
                    "exists": True,
                },
                "runnerAvailable": True,
                "canRun": True,
                "selectionReasons": reasons or [{
                    "kind": "direct-selector",
                    "selector": selector,
                    "because": "Exact existing test script selector matched.",
                }],
            })

    source_names = discover_ctest_names(root)
    source_match = next(
        (name for name in source_names if selector.strip().casefold() == name.casefold()),
        None,
    )
    if source_match:
        built_names, registry_files = discover_built_ctest_registry(root)
        built_match = next(
            (name for name in built_names if source_match.casefold() == name.casefold()),
            None,
        )
        if not registry_files:
            raise HarnessError(
                "TEST_REGISTRY_UNAVAILABLE",
                f"CTest source registration exists, but no generated CTest registry is available for: {selector}",
                {
                    "selector": selector,
                    "sourceRegistered": True,
                    "nextActions": [
                        "Configure/build the current Colosseum checkout so CTestTestfile.cmake exists.",
                        "Retry the same exact selector after the build registry is generated.",
                    ],
                },
            )
        if not built_match:
            raise HarnessError(
                "TEST_NOT_REGISTERED_IN_BUILD",
                f"CTest selector exists in source CMake but is not registered in the current build: {selector}",
                {
                    "selector": selector,
                    "sourceRegistered": True,
                    "generatedRegistryFiles": registry_files,
                    "nextActions": [
                        "Reconfigure the current build.",
                        "Do not claim this test can run until it appears in the generated CTest registry.",
                    ],
                },
            )

        build = root / "native" / "build-msvc"
        runner = find_ctest_runner(root)
        argv = [
            runner or "ctest",
            "--test-dir", str(build),
            "--no-tests=error",
            "-R", f"^{re.escape(built_match)}$",
            "--output-on-failure",
        ]
        matches.append({
            "kind": "ctest",
            "name": built_match,
            "path": "tests/CMakeLists.txt",
            "argv": argv,
            "selectedTests": [built_match],
            "registration": {
                "kind": "ctest",
                "sourceCMake": True,
                "generatedBuildRegistry": True,
                "registryFiles": registry_files,
            },
            "runnerAvailable": runner is not None,
            "canRun": runner is not None,
            "selectionReasons": reasons or [{
                "kind": "direct-selector",
                "selector": selector,
                "because": "Exact selector matched both source CMake and the generated CTest registry.",
            }],
        })

    unique = {(x["kind"], x["name"], x["path"]): x for x in matches}
    matches = list(unique.values())
    if not matches:
        raise HarnessError(
            "TEST_NOT_FOUND",
            f"No exact registered test selector match for: {selector}",
            {
                "selector": selector,
                "nextActions": [
                    "Inspect the relevant domain to see existing verification surfaces.",
                    "Implement/register the test first if this is a future test name.",
                ],
            },
        )
    if len(matches) != 1:
        raise HarnessError("TEST_AMBIGUOUS", f"Multiple exact tests match: {selector}", matches)
    return matches[0]

def execute(root: Path, item: dict[str, Any], dry_run: bool) -> dict[str, Any]:
    data = dict(item)
    data["dryRun"] = dry_run
    if dry_run:
        return data
    argv = item["argv"]
    if item.get("kind") == "ctest" and not item.get("runnerAvailable", False):
        raise HarnessError(
            "RUNNER_MISSING",
            "CTest selector is registered, but the CTest runner could not be resolved.",
            {
                "argv": argv,
                "nextActions": [
                    "Set CTEST_COMMAND or configure the build so CMAKE_CTEST_COMMAND is available.",
                    "Do not claim runtime execution from a dry-run alone.",
                ],
            },
        )
    if item.get("kind") == "journey" and not Path(argv[0]).is_file():
        raise HarnessError("RUNNER_MISSING", f"Lanista executable is missing: {argv[0]}", argv)
    proc = run(argv, root, 1800)
    data.update({
        "exitCode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    })
    return data

def verify_plan(
    root: Path,
    map_path: str | None,
    paths: list[str] | None = None,
) -> tuple[list[dict[str, Any]], list[str]]:
    snapshot = repo_snapshot(root)
    explicit_scope = paths is not None
    paths = normalize_repo_paths(paths, "verification path") if explicit_scope else dirty_paths(snapshot)
    selector_order: list[str] = []
    reasons_by_selector: dict[str, list[dict[str, Any]]] = {}
    warnings: list[str] = []

    def choose(selector: str, reason: dict[str, Any]) -> None:
        key = selector.casefold()
        if key not in reasons_by_selector:
            selector_order.append(selector)
            reasons_by_selector[key] = []
        reasons_by_selector[key].append(reason)

    for path in paths:
        p = Path(path)
        if p.name.startswith("test_") and p.suffix.lower() in SCRIPT_SUFFIXES:
            choose(
                p.stem,
                {
                    "kind": "changed-test-script",
                    "path": path,
                    "because": "The changed file is itself an executable test script.",
                },
            )
        if path.startswith("tests/lanista_scenarios/") or path.startswith("native/devtools/Lanista"):
            choose(
                "test_lanista",
                {
                    "kind": "lanista-contract",
                    "path": path,
                    "because": "Lanista scenario/bridge changes require the existing Lanista contract gate.",
                },
            )

    if map_path or os.environ.get("COLOSSEUM_MAP"):
        try:
            _, doc = load_map(map_path)
            basis_head, _basis_branch = map_basis(doc)
            basis = doc.get("repo_basis")
            semantic = basis.get("semantic_worktree") if isinstance(basis, dict) else None
            freshness = (
                map_freshness(root, doc)
                if explicit_scope or isinstance(semantic, dict)
                else None
            )
            if freshness is not None and freshness.get("state") != "FRESH":
                scope_label = "scoped " if explicit_scope else ""
                warnings.append(
                    f"Map ignored for {scope_label}verify: freshness is "
                    f"{freshness.get('state')} rather than FRESH."
                )
            elif (
                not explicit_scope
                and freshness is None
                and basis_head is not None
                and basis_head != snapshot["head"]
            ):
                warnings.append(
                    f"Map ignored for verify: stale basis {basis_head} != live HEAD {snapshot['head']}."
                )
            else:
                for path in paths:
                    try:
                        resolved = inspect_map(
                            doc,
                            path,
                            current_head=snapshot["head"],
                            freshness_state=(
                                freshness.get("state")
                                if isinstance(freshness, dict)
                                else None
                            ),
                        )
                    except HarnessError as exc:
                        warnings.append(f"No mapped verification for {path!r}: {exc.message}")
                        continue
                    entry = resolved.get("domain")
                    if not isinstance(entry, dict):
                        candidates = [
                            item for item in resolved.get("candidates", [])
                            if isinstance(item, dict) and isinstance(item.get("domain"), dict)
                        ]
                        if explicit_scope and candidates:
                            candidate_ids: list[str] = []
                            for candidate in candidates:
                                candidate_entry = candidate["domain"]
                                candidate_id = str(candidate_entry.get("id", "unknown"))
                                candidate_ids.append(candidate_id)
                                for ctest in candidate_entry.get("ctests", []):
                                    if isinstance(ctest, dict) and isinstance(ctest.get("name"), str):
                                        choose(
                                            ctest["name"],
                                            {
                                                "kind": "domain-ctest",
                                                "domain": candidate_id,
                                                "path": path,
                                                "because": (
                                                    f"{path} maps to shared domain {candidate_id}; "
                                                    "scoped verification unions all legitimate domains."
                                                ),
                                            },
                                        )
                                for check in candidate_entry.get("checks", []):
                                    if not isinstance(check, str):
                                        continue
                                    check_path = Path(check)
                                    if (
                                        check_path.name.startswith("test_")
                                        and check_path.suffix.lower() in SCRIPT_SUFFIXES
                                    ):
                                        choose(
                                            check,
                                            {
                                                "kind": "domain-check",
                                                "domain": candidate_id,
                                                "path": path,
                                                "because": (
                                                    f"{path} maps to shared domain {candidate_id}; "
                                                    "scoped verification unions all legitimate domains."
                                                ),
                                            },
                                        )
                                for key in ("tests", "verification"):
                                    values = candidate_entry.get(key, [])
                                    if isinstance(values, list):
                                        for value in values:
                                            selector = None
                                            if isinstance(value, str):
                                                selector = value
                                            elif isinstance(value, dict):
                                                selector = value.get("selector") or value.get("test")
                                            if isinstance(selector, str):
                                                choose(
                                                    selector,
                                                    {
                                                        "kind": f"domain-{key}",
                                                        "domain": candidate_id,
                                                        "path": path,
                                                        "because": (
                                                            f"{path} maps to shared domain {candidate_id}; "
                                                            "scoped verification unions all legitimate domains."
                                                        ),
                                                    },
                                                )
                            warnings.append(
                                f"Map route for {path!r} is shared across {candidate_ids}; "
                                "scoped verification unioned every mapped domain."
                            )
                            continue

                        clear_winner = None
                        if candidates:
                            top = candidates[0]
                            second_score = int(candidates[1].get("score", 0)) if len(candidates) > 1 else 0
                            top_score = int(top.get("score", 0))
                            exact_evidence = any(
                                isinstance(ev, dict)
                                and ev.get("relation") == "exact"
                                and ev.get("kind") in {"owner", "entry_point", "source_root"}
                                for ev in top.get("evidence", [])
                            )
                            if exact_evidence and top_score >= second_score + 100:
                                clear_winner = top
                        if clear_winner is None:
                            candidate_ids = [
                                str(item.get("domain", {}).get("id"))
                                for item in candidates
                            ]
                            warnings.append(
                                f"Map route for {path!r} is shared across {candidate_ids}; no checks were guessed."
                            )
                            continue
                        entry = clear_winner["domain"]
                        warnings.append(
                            f"Map route for {path!r} is shared, but {entry.get('id')} "
                            f"is the clear exact-match winner "
                            f"(score {clear_winner.get('score')}); its checks were selected."
                        )

                    domain_id = str(entry.get("id", "unknown"))
                    for ctest in entry.get("ctests", []):
                        if isinstance(ctest, dict) and isinstance(ctest.get("name"), str):
                            choose(
                                ctest["name"],
                                {
                                    "kind": "domain-ctest",
                                    "domain": domain_id,
                                    "path": path,
                                    "because": f"{path} maps to {domain_id}, which declares this CTest surface.",
                                },
                            )
                    for check in entry.get("checks", []):
                        if not isinstance(check, str):
                            continue
                        check_path = Path(check)
                        if (
                            check_path.name.startswith("test_")
                            and check_path.suffix.lower() in SCRIPT_SUFFIXES
                        ):
                            choose(
                                check,
                                {
                                    "kind": "domain-check",
                                    "domain": domain_id,
                                    "path": path,
                                    "because": f"{path} maps to {domain_id}, which declares this executable check.",
                                },
                            )
                    for key in ("tests", "verification"):
                        values = entry.get(key, [])
                        if isinstance(values, list):
                            for value in values:
                                selector = None
                                if isinstance(value, str):
                                    selector = value
                                elif isinstance(value, dict):
                                    selector = value.get("selector") or value.get("test")
                                if isinstance(selector, str):
                                    choose(
                                        selector,
                                        {
                                            "kind": f"domain-{key}",
                                            "domain": domain_id,
                                            "path": path,
                                            "because": f"{path} maps to {domain_id}, which declares this {key} selector.",
                                        },
                                    )
        except HarnessError as exc:
            warnings.append(f"Map ignored for verify: {exc.message}")

    plan = []
    for selector in selector_order:
        try:
            item = resolve_test(
                root,
                selector,
                reasons_by_selector.get(selector.casefold(), []),
            )
            item["selector"] = selector
            plan.append(item)
        except HarnessError as exc:
            warnings.append(f"Unresolved selector {selector!r}: {exc.message}")
    if not plan:
        warnings.append("No executable verification surface was inferred; nothing was guessed.")
    return plan, warnings
