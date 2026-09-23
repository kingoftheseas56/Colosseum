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

SCRIPT_SUFFIXES = {".ps1", ".py", ".mjs", ".js"}
AUTHORITY_EXCERPT_MAX_BYTES = 8192
AUTHORITY_EXCERPT_MAX_LINES = 80
DRIVE_COMMANDS = {
    "ui-click", "ui-keypress", "ui-text-input",
    "ui-scroll", "window-set-state", "reader2-hot-switch",
}


class HarnessError(RuntimeError):
    def __init__(self, code: str, message: str, details: Any = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details


def norm(value: str | Path) -> str:
    return str(value).replace("\\", "/").strip("/").lower()


def looks_like_colosseum(path: Path) -> bool:
    return all((path / part).exists() for part in ("native", "qml", "tests"))


def resolve_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env_root = os.environ.get("COLOSSEUM_ROOT")
    if env_root:
        candidates.append(Path(env_root))
    repo_from_harness = Path(__file__).resolve().parents[2]
    candidates.extend([
        Path.cwd(),
        repo_from_harness,
    ])
    for candidate in candidates:
        try:
            resolved = candidate.expanduser().resolve()
        except OSError:
            continue
        if looks_like_colosseum(resolved):
            return resolved
    raise HarnessError(
        "COLOSSEUM_ROOT_NOT_FOUND",
        "Pass --root or set COLOSSEUM_ROOT.",
        [str(p) for p in candidates],
    )


def run(argv: list[str], cwd: Path, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            argv, cwd=str(cwd), capture_output=True,
            text=True, timeout=timeout, check=False,
        )
    except FileNotFoundError as exc:
        raise HarnessError("RUNNER_MISSING", f"Runner not found: {argv[0]}", argv) from exc
    except subprocess.TimeoutExpired as exc:
        raise HarnessError("COMMAND_TIMEOUT", f"Timed out after {timeout}s", argv) from exc


def git(root: Path, *args: str) -> str:
    proc = run(["git", "-C", str(root), *args], root, 30)
    if proc.returncode:
        raise HarnessError(
            "GIT_ERROR", f"git {' '.join(args)} failed",
            {"exitCode": proc.returncode, "stderr": proc.stderr.strip()},
        )
    return proc.stdout.rstrip("\r\n")


def repo_snapshot(root: Path) -> dict[str, Any]:
    return {
        "root": str(Path(git(root, "rev-parse", "--show-toplevel")).resolve()),
        "head": git(root, "rev-parse", "HEAD"),
        "branch": git(root, "branch", "--show-current") or "(detached)",
        "dirty": [x for x in git(root, "status", "--porcelain=v1", "-z").split("\0") if x],
    }


def _run_source_identity(root: Path, relative_path: str) -> dict[str, Any]:
    root = root.resolve()
    candidate = (root / relative_path).resolve()
    if candidate != root and root not in candidate.parents:
        raise HarnessError(
            "RUN_SCOPE_OUTSIDE_REPO",
            f"Run path resolves outside the repository: {relative_path}",
        )
    if candidate.exists() and candidate.is_dir():
        raise HarnessError(
            "RUN_SCOPE_NOT_FILE",
            f"Run paths must name files, not directories: {relative_path}",
        )
    if not candidate.exists():
        return {
            "path": relative_path,
            "exists": False,
            "sha256": None,
            "sizeBytes": None,
        }
    if not candidate.is_file():
        raise HarnessError(
            "RUN_SCOPE_NOT_FILE",
            f"Run path is not a regular file: {relative_path}",
        )
    raw = candidate.read_bytes()
    return {
        "path": relative_path,
        "exists": True,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "sizeBytes": len(raw),
    }


def create_run_receipt(
    root: Path,
    task: str,
    paths: list[str],
    map_path: str,
    selected_checks: list[dict[str, Any]],
    warnings: list[str],
) -> tuple[dict[str, Any], Path]:
    normalized_paths = normalize_repo_paths(paths, "run path")
    if not normalized_paths:
        raise HarnessError(
            "RUN_SCOPE_REQUIRED",
            "--record-run requires at least one explicit --path.",
        )
    resolved_map = Path(map_path).resolve()
    if not resolved_map.is_file():
        raise HarnessError("MAP_NOT_FOUND", f"Map not found: {resolved_map}")

    snapshot = repo_snapshot(root)
    run_id = f"run_{uuid.uuid4().hex}"
    receipt = {
        "schema": "colosseum.harness.run.v1",
        "runId": run_id,
        "task": task.strip(),
        "repo": {
            "root": snapshot["root"],
            "head": snapshot["head"],
            "branch": snapshot["branch"],
        },
        "paths": normalized_paths,
        "source": [_run_source_identity(root, path) for path in normalized_paths],
        "map": {
            "path": str(resolved_map),
            "sha256": hashlib.sha256(resolved_map.read_bytes()).hexdigest(),
        },
        "verification": {
            "selectedChecks": selected_checks,
            "warnings": list(warnings),
        },
        "build": None,
        "runtime": None,
        "desktopEvidence": [],
        "result": None,
        "completionReady": False,
    }
    receipt_path = root / "artifacts" / "harness-runs" / run_id / "run.json"
    receipt_path.parent.mkdir(parents=True, exist_ok=False)
    with receipt_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(receipt, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    return receipt, receipt_path


def load_run_receipt(path: Path | str) -> dict[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError("RUN_RECEIPT_INVALID", f"Cannot read run receipt: {path}") from exc
    if not isinstance(value, dict) or value.get("schema") != "colosseum.harness.run.v1":
        raise HarnessError("RUN_RECEIPT_INVALID", f"Invalid run receipt: {path}")
    return value


def _run_receipt_path(root: Path, run_id: str) -> Path:
    if not re.fullmatch(r"run_[0-9a-f]{32}", run_id):
        raise HarnessError("RUN_ID_INVALID", f"Invalid run id: {run_id}")
    return root / "artifacts" / "harness-runs" / run_id / "run.json"


def _write_run_receipt(path: Path, receipt: dict[str, Any]) -> None:
    temp = path.with_name(f".run.{uuid.uuid4().hex}.tmp")
    try:
        with temp.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(receipt, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temp, path)
    finally:
        try:
            temp.unlink()
        except FileNotFoundError:
            pass


def load_lanista_session_manifest(path: Path | str) -> tuple[dict[str, Any], Path]:
    manifest_path = Path(path).expanduser().resolve()
    try:
        raw = manifest_path.read_bytes()
        value = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Cannot read Lanista session manifest: {manifest_path}",
        ) from exc
    if not isinstance(value, dict) or value.get("schema") != "colosseum.session.v1":
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Not a Colosseum Lanista v1 session manifest: {manifest_path}",
        )

    required_strings = (
        "sessionId", "tag", "pipe", "exe", "exeSha256", "appDataRoot", "cacheRoot",
    )
    for key in required_strings:
        if not isinstance(value.get(key), str) or not value[key].strip():
            raise HarnessError(
                "LANISTA_SESSION_INVALID",
                f"Lanista session manifest has no valid {key}: {manifest_path}",
            )
    pid = value.get("pid")
    valid_pid = (
        isinstance(pid, int) and not isinstance(pid, bool) and pid > 0
    ) or (
        isinstance(pid, float) and pid.is_integer() and pid > 0
    )
    if not valid_pid:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session manifest has no valid pid: {manifest_path}",
        )
    if not re.fullmatch(r"[0-9a-fA-F]{64}", value["exeSha256"]):
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session manifest has no valid exeSha256: {manifest_path}",
        )
    if value["pipe"] != f"ColosseumLanista-{value['sessionId']}":
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session pipe does not match its session id: {manifest_path}",
        )
    marker = f"Colosseum-dltest-{value['tag']}"
    if marker not in value["appDataRoot"] or marker not in value["cacheRoot"]:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session isolation roots do not match its tag: {manifest_path}",
        )
    return value, manifest_path


def bind_run_session(root: Path, run_id: str, session_path: Path | str) -> tuple[dict[str, Any], Path]:
    receipt_path = _run_receipt_path(root, run_id)
    receipt = load_run_receipt(receipt_path)
    if receipt.get("runId") != run_id or receipt.get("repo", {}).get("root") != str(root.resolve()):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt identity mismatch: {receipt_path}")
    if receipt.get("runtime") is not None:
        raise HarnessError(
            "RUN_RUNTIME_ALREADY_BOUND",
            f"Run already has runtime identity: {run_id}",
        )

    manifest, resolved_session_path = load_lanista_session_manifest(session_path)
    receipt["runtime"] = {
        "source": "lanista-session-manifest",
        "manifestPath": str(resolved_session_path),
        "sessionId": manifest["sessionId"],
        "tag": manifest["tag"],
        "exe": manifest["exe"],
        "exeSha256": manifest["exeSha256"].lower(),
        "pid": int(manifest["pid"]),
        "pipe": manifest["pipe"],
        "appDataRoot": manifest["appDataRoot"],
        "cacheRoot": manifest["cacheRoot"],
    }
    receipt["completionReady"] = False
    _write_run_receipt(receipt_path, receipt)
    return receipt, receipt_path


def envelope(
    command: str, root: Path, *, ok: bool = True,
    data: Any = None, evidence: list[Any] | None = None,
    warnings: list[str] | None = None, error: dict[str, Any] | None = None,
) -> dict[str, Any]:
    out = {
        "ok": ok,
        "command": command,
        "repo": repo_snapshot(root),
        "data": {} if data is None else data,
        "evidence": evidence or [],
        "warnings": warnings or [],
    }
    if error is not None:
        out["error"] = error
    return out


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
    doc: dict[str, Any], target: str, current_head: str | None = None,
) -> dict[str, Any]:
    selector = target.strip()
    query_alias = selector.casefold()
    query_path = norm(selector)
    entries = list(map_entries(doc))
    basis_head, basis_branch = map_basis(doc)
    stale = None if current_head is None or basis_head is None else current_head != basis_head

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


def load_scenario(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError("SCENARIO_INVALID", f"Cannot read scenario: {path}", str(exc)) from exc
    if not isinstance(doc, dict) or not isinstance(doc.get("steps"), list):
        raise HarnessError("SCENARIO_INVALID", f"Scenario lacks a steps array: {path}")
    return doc


def discover_journeys(root: Path) -> list[dict[str, Any]]:
    scenario_dir = root / "tests" / "lanista_scenarios"
    if not scenario_dir.is_dir():
        return []

    out = []
    for path in sorted(scenario_dir.glob("*.json")):
        rel = path.relative_to(root).as_posix()
        try:
            doc = load_scenario(path)
        except HarnessError as exc:
            out.append({
                "name": path.stem,
                "file": path.name,
                "path": rel,
                "valid": False,
                "usesDriveCommands": None,
                "comment": "",
                "error": {"code": exc.code, "message": exc.message, "details": exc.details},
            })
            continue
        commands = {
            step.get("cmd") for step in doc["steps"]
            if isinstance(step, dict) and isinstance(step.get("cmd"), str)
        }
        out.append({
            "name": doc.get("name") or path.stem,
            "file": path.name,
            "path": rel,
            "valid": True,
            "usesDriveCommands": bool(commands & DRIVE_COMMANDS),
            "comment": doc.get("comment", ""),
        })
    return out


def resolve_journey(root: Path, selector: str) -> dict[str, Any]:
    query = selector.strip().lower()
    matches = [
        item for item in discover_journeys(root)
        if query in {
            item["name"].lower(),
            item["file"].lower(),
            Path(item["file"]).stem.lower(),
        }
    ]
    if not matches:
        raise HarnessError("JOURNEY_NOT_FOUND", f"No exact Lanista scenario match for: {selector}")
    if len(matches) != 1:
        raise HarnessError("JOURNEY_AMBIGUOUS", f"Multiple scenarios match: {selector}", matches)
    match = matches[0]
    if not match.get("valid", True):
        error = match.get("error", {})
        raise HarnessError(
            error.get("code", "SCENARIO_INVALID"),
            error.get("message", f"Scenario is invalid: {match['path']}"),
            error.get("details"),
        )
    return match


def journey_argv(
    root: Path, journey: dict[str, Any], *, mode: str,
    drive: bool, seed: str | None, ready_ms: int | None,
    pipe: str | None,
) -> list[str]:
    exe = root / "native" / "build-msvc" / ("lanista.exe" if os.name == "nt" else "lanista")
    scenario = journey["path"]
    if mode == "session":
        argv = [str(exe), "session", "run", scenario]
        if drive:
            argv.append("--drive")
        if seed:
            seed_path = Path(seed)
            if not seed_path.is_absolute():
                seed_path = root / seed_path
            argv.extend(["--seed", str(seed_path)])
        if ready_ms is not None:
            argv.extend(["--ready-ms", str(ready_ms)])
        return argv
    argv = [str(exe)]
    if pipe:
        argv.extend(["--pipe", pipe])
    argv.extend(["run", scenario])
    return argv


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


def dirty_paths(snapshot: dict[str, Any]) -> list[str]:
    out = []
    for record in snapshot.get("dirty", []):
        text = record[3:] if len(record) >= 4 else record
        if " -> " in text:
            text = text.split(" -> ", 1)[1]
        out.append(text.replace("\\", "/"))
    return out


def normalize_repo_paths(values: list[str], label: str = "path") -> list[str]:
    out: list[str] = []
    for value in values:
        if not isinstance(value, str) or not value.strip():
            raise HarnessError("INVALID_REPO_PATH", f"{label} must be a non-empty repo-relative path.")
        candidate = Path(value)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise HarnessError("INVALID_REPO_PATH", f"{label} must stay inside the repository: {value}")
        normalized = value.replace("\\", "/").strip("/")
        if normalized and normalized not in out:
            out.append(normalized)
    return out


def semantic_worktree_fingerprint(root: Path, watch_scopes: list[str]) -> str:
    scopes = sorted(
        normalize_repo_paths(watch_scopes, "watch scope"),
        key=str.casefold,
    )
    if not scopes:
        raise HarnessError("MAP_INVALID", "semantic_worktree.watch_scopes must not be empty.")

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
        "algorithm": "sha256-git-semantic-v1",
        "head": git(root, "rev-parse", "HEAD"),
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

    semantic = basis.get("semantic_worktree")
    if not isinstance(semantic, dict):
        return {
            "state": "UNKNOWN",
            "authoritative": False,
            "basisHead": basis_head,
            "currentHead": snapshot["head"],
            "reason": "Map has no semantic working-tree fingerprint.",
        }
    algorithm = semantic.get("algorithm")
    scopes = semantic.get("watch_scopes")
    expected = semantic.get("fingerprint")
    if (
        algorithm != "sha256-git-semantic-v1"
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

    actual = semantic_worktree_fingerprint(root, scopes)
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
    }


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
            freshness = map_freshness(root, doc) if explicit_scope else None
            if explicit_scope and freshness is not None and freshness.get("state") != "FRESH":
                warnings.append(
                    "Map ignored for scoped verify: freshness is "
                    f"{freshness.get('state')} rather than FRESH."
                )
            elif basis_head is not None and basis_head != snapshot["head"]:
                warnings.append(
                    f"Map ignored for verify: stale basis {basis_head} != live HEAD {snapshot['head']}."
                )
            else:
                for path in paths:
                    try:
                        resolved = inspect_map(doc, path, current_head=snapshot["head"])
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
        resolved = inspect_map(doc, domain, current_head=repo_snapshot(root)["head"])
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
            resolved = inspect_map(doc, path, current_head=repo_snapshot(root)["head"])
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
    journey_p.add_argument("selector")
    journey_p.add_argument("--mode", choices=("session", "attached"), default="session")
    journey_p.add_argument("--drive", action="store_true")
    journey_p.add_argument("--seed")
    journey_p.add_argument("--ready-ms", type=int)
    journey_p.add_argument("--pipe")
    add_execution_mode(journey_p)

    verify_p = sub.add_parser("verify", parents=[common], add_help=True)
    verify_p.add_argument("--path", dest="paths", action="append")
    add_execution_mode(verify_p)
    return parser


def dispatch(ns: argparse.Namespace) -> dict[str, Any]:
    root = resolve_root(getattr(ns, "root", None))
    command = ns.command
    if command == "status":
        snapshot = repo_snapshot(root)
        tracked = [row for row in snapshot["dirty"] if not row.startswith("??")]
        untracked = [row for row in snapshot["dirty"] if row.startswith("??")]
        freshness = None
        warnings: list[str] = []
        evidence: list[dict[str, Any]] = [{"kind": "git", "source": "live checkout"}]
        if getattr(ns, "map", None) or os.environ.get("COLOSSEUM_MAP"):
            map_path, doc = load_map(getattr(ns, "map", None))
            freshness = map_freshness(root, doc)
            evidence.append({
                "kind": "map",
                "path": str(map_path),
                "freshness": freshness,
            })
            if freshness.get("state") != "FRESH":
                warnings.append(
                    "Intelligence map is non-authoritative: "
                    f"freshness={freshness.get('state')}."
                )
        return envelope(
            command, root,
            data={
                "state": "observed",
                "dirtyCount": len(snapshot["dirty"]),
                "trackedChangeCount": len(tracked),
                "untrackedChangeCount": len(untracked),
                "mapFreshness": freshness,
            },
            warnings=warnings,
            evidence=evidence,
        )
    if command == "inspect":
        map_path, doc = load_map(getattr(ns, "map", None))
        current_head = repo_snapshot(root)["head"]
        freshness = map_freshness(root, doc)
        match = inspect_map(doc, ns.target, current_head=current_head)
        warnings = []
        if freshness.get("state") != "FRESH":
            warnings.append(
                "Intelligence map is non-authoritative: "
                f"freshness={freshness.get('state')}; returned mappings are historical context."
            )
        return envelope(
            command, root,
            data={
                "target": ns.target,
                "map": str(map_path),
                "freshness": freshness,
                **match,
            },
            warnings=warnings,
            evidence=[{
                "kind": "map",
                "path": str(map_path),
                "basisHead": match.get("basis_head"),
                "currentHead": current_head,
                "freshness": freshness,
            }],
        )
    if command == "context-for-task":
        data = context_for_task(
            root,
            getattr(ns, "map", None),
            ns.task,
            paths=getattr(ns, "paths", None),
            domain=getattr(ns, "domain", None),
            preflight_root=getattr(ns, "preflight_root", None),
        )
        if getattr(ns, "record_run", False):
            requested_paths = getattr(ns, "paths", None)
            if not requested_paths:
                raise HarnessError(
                    "RUN_SCOPE_REQUIRED",
                    "--record-run requires at least one explicit --path.",
                )
            plan, run_warnings = verify_plan(
                root,
                getattr(ns, "map", None),
                paths=requested_paths,
            )
            selected_checks = [
                {
                    "selector": item.get("selector"),
                    "kind": item.get("kind"),
                    "name": item.get("name"),
                    "selectedTests": item.get("selectedTests", []),
                }
                for item in plan
            ]
            receipt, receipt_path = create_run_receipt(
                root, ns.task, requested_paths, data["map"], selected_checks, run_warnings
            )
            data["runId"] = receipt["runId"]
            data["receiptPath"] = str(receipt_path)
        return envelope(
            command,
            root,
            data=data,
            evidence=[{
                "kind": "map-context",
                "path": data.get("map"),
                "freshness": data.get("freshness"),
                "domains": [item.get("id") for item in data.get("domains", [])],
            }],
        )
    if command == "bind-session":
        receipt, receipt_path = bind_run_session(root, ns.run_id, ns.session)
        return envelope(
            command,
            root,
            data={
                "runId": receipt["runId"],
                "receiptPath": str(receipt_path),
                "runtime": receipt["runtime"],
                "completionReady": receipt["completionReady"],
            },
            evidence=[{
                "kind": "lanista-session-manifest",
                "path": receipt["runtime"]["manifestPath"],
            }],
        )
    if command == "test":
        item = resolve_test(root, ns.selector)
        data = execute(root, item, ns.dry_run)
        ok = data.get("exitCode", 0) == 0
        return envelope(
            command, root, ok=ok, data=data,
            evidence=[{
                "kind": "test-resolution",
                "path": item["path"],
                "registration": item.get("registration"),
                "selectedTests": item.get("selectedTests", []),
            }],
        )
    if command == "journeys":
        journeys = discover_journeys(root)
        invalid = [item for item in journeys if not item.get("valid", True)]
        warnings = []
        if invalid:
            warnings.append(
                f"{len(invalid)} scenario file(s) are invalid; valid scenarios remain discoverable."
            )
        return envelope(
            command, root,
            data={
                "count": len(journeys),
                "validCount": len(journeys) - len(invalid),
                "invalidCount": len(invalid),
                "journeys": journeys,
            },
            warnings=warnings,
            evidence=[{
                "kind": "lanista-scenarios",
                "path": "tests/lanista_scenarios",
            }],
        )
    if command == "journey":
        journey = resolve_journey(root, ns.selector)
        argv = journey_argv(
            root, journey, mode=ns.mode, drive=ns.drive,
            seed=ns.seed, ready_ms=ns.ready_ms, pipe=ns.pipe,
        )
        item = {
            "kind": "journey",
            **journey,
            "mode": ns.mode,
            "argv": argv,
        }
        data = execute(root, item, ns.dry_run)
        warnings = []
        if journey["usesDriveCommands"] and ns.mode == "session" and not ns.drive:
            warnings.append(
                "Scenario contains Drive-gated commands; --drive may be "
                "required or deliberately omitted by the scenario."
            )
        ok = data.get("exitCode", 0) == 0
        return envelope(
            command, root, ok=ok, data=data, warnings=warnings,
            evidence=[{"kind": "lanista-scenario", "path": journey["path"]}],
        )
    if command == "verify":
        requested_paths = getattr(ns, "paths", None)
        plan, warnings = verify_plan(
            root,
            getattr(ns, "map", None),
            paths=requested_paths,
        )
        changed_paths = (
            normalize_repo_paths(requested_paths, "verification path")
            if requested_paths is not None
            else dirty_paths(repo_snapshot(root))
        )
        map_freshness_data = None
        if getattr(ns, "map", None) or os.environ.get("COLOSSEUM_MAP"):
            try:
                _map_path, verify_doc = load_map(getattr(ns, "map", None))
                map_freshness_data = map_freshness(root, verify_doc)
            except HarnessError:
                map_freshness_data = None
        scope = {
            "kind": "explicit-paths" if requested_paths is not None else "dirty-tree",
            "paths": changed_paths,
        }
        if not plan:
            return envelope(
                command,
                root,
                ok=False,
                data={
                    "dryRun": ns.dry_run,
                    "scope": scope,
                    "mapFreshness": map_freshness_data,
                    "completionReady": False,
                    "selectedChecks": [],
                    "checks": [],
                },
                warnings=warnings,
                evidence=[{"kind": "verification-scope", **scope}],
                error={
                    "code": "NO_VERIFICATION_SURFACE",
                    "message": "No executable verification surface could be inferred safely.",
                    "details": {
                        "changedPaths": changed_paths,
                        "nextActions": [
                            "Inspect the changed area and choose an existing test explicitly.",
                            "Add a focused verification surface before claiming verification.",
                            "Refresh the intelligence map if the changed owner is missing.",
                        ],
                    },
                },
            )
        selected = [
            {
                "selector": item.get("selector"),
                "kind": item.get("kind"),
                "name": item.get("name"),
                "selectedTests": item.get("selectedTests", []),
                "selectionReasons": item.get("selectionReasons", []),
                "argv": item.get("argv", []),
            }
            for item in plan
        ]
        results = []
        ok = True
        for item in plan:
            data = execute(root, item, ns.dry_run)
            results.append(data)
            ok = ok and data.get("exitCode", 0) == 0

        warning_blocks_completion = any(
            warning.startswith(
                (
                    "No mapped verification",
                    "Map ignored",
                    "Unresolved selector",
                )
            )
            or ("no checks were guessed" in warning)
            for warning in warnings
        )
        completion_ready = (
            requested_paths is not None
            and map_freshness_data is not None
            and map_freshness_data.get("state") == "FRESH"
            and not ns.dry_run
            and ok
            and bool(plan)
            and not warning_blocks_completion
        )
        return envelope(
            command, root, ok=ok,
            data={
                "dryRun": ns.dry_run,
                "scope": scope,
                "mapFreshness": map_freshness_data,
                "completionReady": completion_ready,
                "selectedChecks": selected,
                "checks": results,
            },
            warnings=warnings,
            evidence=[{"kind": "verification-scope", **scope}],
        )
    raise HarnessError("UNKNOWN_COMMAND", f"Unknown command: {command}")


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


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    json_requested = "--json" in argv
    root_hint = option_value(argv, "--root")
    commands = {"status", "inspect", "context-for-task", "test", "journeys", "journey", "verify"}
    command = next((arg for arg in argv if arg in commands), "unknown")
    try:
        ns = build_parser().parse_args(argv)
        payload = dispatch(ns)
    except HarnessError as exc:
        try:
            root = resolve_root(root_hint)
            repo = repo_snapshot(root)
        except Exception:
            repo = {"root": root_hint, "head": None, "branch": None, "dirty": []}
        payload = {
            "ok": False,
            "command": command,
            "repo": repo,
            "data": {},
            "evidence": [],
            "warnings": [],
            "error": {
                "code": exc.code,
                "message": exc.message,
                "details": exc.details,
            },
        }
    emit(payload, json_requested)
    return 0 if payload["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
