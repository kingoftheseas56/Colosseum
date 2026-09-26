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

from harness.repo import (
    HarnessError, dirty_paths, envelope, git, looks_like_colosseum, norm,
    normalize_repo_paths, repo_snapshot, resolve_root, run,
)
from harness.evidence import (
    _completion_blocker, _persisted_execution_result, apply_run_completion_gate,
    load_lanista_session_manifest, run_completion_blockers,
)
from harness.receipts import (
    _run_receipt_path, _run_source_identity, _write_run_receipt, bind_run_session,
    create_run_receipt, load_run_receipt,
)
from harness.intelligence import (
    entry_aliases, entry_path_records, entry_paths, inspect_map, inspect_suggestions,
    load_map, map_basis, map_entries, map_freshness, rank_path_candidates,
    semantic_worktree_fingerprint,
)
from harness.journeys import (
    DRIVE_COMMANDS, discover_journeys, journey_argv, load_scenario, resolve_journey,
)
from harness.verification import (
    SCRIPT_SUFFIXES, discover_built_ctest_registry, discover_ctest_names,
    discover_scripts, execute, find_ctest_runner, resolve_test, script_runner, verify_plan,
)
from harness.context import (
    AUTHORITY_EXCERPT_MAX_BYTES, AUTHORITY_EXCERPT_MAX_LINES,
    _task_mentions_selector, bounded_preflight_evidence, context_for_task,
    resolve_preflight_root,
)
from harness.cli import (
    add_common, add_execution_mode, build_parser, command_text, emit, option_value,
    runner_summary_line,
)

def verify_run_receipt(
    root: Path,
    run_id: str,
    *,
    dry_run: bool,
) -> dict[str, Any]:
    receipt_path = _run_receipt_path(root, run_id)
    receipt = load_run_receipt(receipt_path)
    if receipt.get("runId") != run_id or receipt.get("repo", {}).get("root") != str(root.resolve()):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt identity mismatch: {receipt_path}")

    paths = receipt.get("paths")
    if not isinstance(paths, list):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt has no valid paths: {receipt_path}")
    scope = {
        "kind": "run-receipt",
        "paths": normalize_repo_paths(paths, "run path"),
    }

    verification = receipt.get("verification")
    if not isinstance(verification, dict):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt has no verification block: {receipt_path}")
    frozen = verification.get("selectedChecks")
    if not isinstance(frozen, list) or not frozen:
        raise HarnessError(
            "RUN_VERIFICATION_NOT_SELECTED",
            f"Run has no frozen verification checks: {run_id}",
        )
    warnings = verification.get("warnings", [])
    if not isinstance(warnings, list) or any(not isinstance(item, str) for item in warnings):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt warnings are invalid: {receipt_path}")

    plan: list[dict[str, Any]] = []
    selected: list[dict[str, Any]] = []
    for spec in frozen:
        if not isinstance(spec, dict) or not isinstance(spec.get("selector"), str):
            raise HarnessError("RUN_RECEIPT_INVALID", f"Run has an invalid frozen check: {receipt_path}")
        selector = spec["selector"]
        item = resolve_test(root, selector)
        expected_kind = spec.get("kind")
        expected_name = spec.get("name")
        expected_tests = spec.get("selectedTests")
        drift = (
            (expected_kind is not None and expected_kind != item.get("kind"))
            or (expected_name is not None and expected_name != item.get("name"))
            or (
                isinstance(expected_tests, list)
                and expected_tests != item.get("selectedTests", [])
            )
        )
        if drift:
            raise HarnessError(
                "RUN_VERIFY_CHECK_DRIFT",
                f"Frozen verification check no longer resolves to the same test: {selector}",
                {
                    "frozen": spec,
                    "current": {
                        "kind": item.get("kind"),
                        "name": item.get("name"),
                        "selectedTests": item.get("selectedTests", []),
                    },
                },
            )
        item["selector"] = selector
        plan.append(item)
        selected.append(dict(spec))

    results: list[dict[str, Any]] = []
    ok = True
    for item in plan:
        data = execute(root, item, dry_run)
        results.append(data)
        ok = ok and data.get("exitCode", 0) == 0

    verification_result = {
        "ok": ok,
        "scope": scope,
        "selectedChecks": selected,
        "checks": [_persisted_execution_result(item) for item in results],
        "warnings": list(warnings),
    }
    blockers = run_completion_blockers(receipt)
    completion_ready = False
    if not dry_run:
        result = receipt.get("result")
        if result is None:
            result = {}
        if not isinstance(result, dict):
            raise HarnessError("RUN_RECEIPT_INVALID", f"Run result slot is invalid: {receipt_path}")
        result["verification"] = verification_result
        receipt["result"] = result
        blockers = apply_run_completion_gate(receipt)
        completion_ready = receipt["completionReady"]
        _write_run_receipt(receipt_path, receipt)

    return envelope(
        "verify",
        root,
        ok=ok,
        data={
            "dryRun": dry_run,
            "scope": scope,
            "completionReady": completion_ready,
            "completionBlockers": blockers,
            "selectedChecks": selected,
            "checks": results,
        },
        warnings=list(warnings),
        evidence=[{"kind": "verification-scope", **scope}],
    )

def journey_run_receipt(
    root: Path,
    run_id: str,
    *,
    dry_run: bool,
) -> dict[str, Any]:
    receipt_path = _run_receipt_path(root, run_id)
    receipt = load_run_receipt(receipt_path)
    if receipt.get("runId") != run_id or receipt.get("repo", {}).get("root") != str(root.resolve()):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt identity mismatch: {receipt_path}")

    verification = receipt.get("verification")
    if not isinstance(verification, dict):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt has no verification block: {receipt_path}")
    frozen = verification.get("selectedJourneys")
    if not isinstance(frozen, list) or not frozen:
        raise HarnessError(
            "RUN_JOURNEY_NOT_SELECTED",
            f"Run has no frozen Lanista journey: {run_id}",
        )
    if len(frozen) != 1:
        raise HarnessError(
            "RUN_JOURNEY_AMBIGUOUS",
            f"Run has {len(frozen)} frozen Lanista journeys; choose exactly one before execution.",
            frozen,
        )
    spec = frozen[0]
    if not isinstance(spec, dict) or not isinstance(spec.get("selector"), str):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run has an invalid frozen journey: {receipt_path}")

    selector = spec["selector"]
    journey = resolve_journey(root, selector)
    if (
        (spec.get("name") is not None and spec.get("name") != journey.get("name"))
        or (spec.get("path") is not None and spec.get("path") != journey.get("path"))
    ):
        raise HarnessError(
            "RUN_JOURNEY_DRIFT",
            f"Frozen Lanista journey no longer resolves to the same scenario: {selector}",
            {
                "frozen": spec,
                "current": {
                    "name": journey.get("name"),
                    "path": journey.get("path"),
                },
            },
        )

    runtime = receipt.get("runtime")
    if not isinstance(runtime, dict):
        raise HarnessError("RUN_RUNTIME_NOT_BOUND", f"Run has no bound Lanista runtime: {run_id}")
    session_id = runtime.get("sessionId")
    pipe = runtime.get("pipe")
    if (
        runtime.get("source") != "lanista-session-manifest"
        or not isinstance(session_id, str)
        or not session_id.strip()
        or not isinstance(pipe, str)
        or pipe != f"ColosseumLanista-{session_id}"
    ):
        raise HarnessError(
            "RUN_RUNTIME_NOT_BOUND",
            f"Run has no valid bound Lanista session/pipe: {run_id}",
        )

    argv = journey_argv(
        root,
        journey,
        mode="attached",
        drive=False,
        seed=None,
        ready_ms=None,
        pipe=pipe,
    )
    item = {
        "kind": "journey",
        **journey,
        "selector": selector,
        "mode": "attached",
        "argv": argv,
    }
    data = execute(root, item, dry_run)
    ok = data.get("exitCode", 0) == 0
    blockers = run_completion_blockers(receipt)
    completion_ready = False
    if not dry_run:
        result = receipt.get("result")
        if result is None:
            result = {}
        if not isinstance(result, dict):
            raise HarnessError("RUN_RECEIPT_INVALID", f"Run result slot is invalid: {receipt_path}")
        result["journey"] = {
            "ok": ok,
            "sessionId": session_id,
            "pipe": pipe,
            "selector": selector,
            "path": journey["path"],
            "execution": _persisted_execution_result(data),
        }
        receipt["result"] = result
        blockers = apply_run_completion_gate(receipt)
        completion_ready = receipt["completionReady"]
        _write_run_receipt(receipt_path, receipt)

    data = dict(data)
    data["runId"] = run_id
    data["sessionId"] = session_id
    data["pipe"] = pipe
    data["completionReady"] = completion_ready
    data["completionBlockers"] = blockers
    return envelope(
        "journey",
        root,
        ok=ok,
        data=data,
        evidence=[{"kind": "lanista-scenario", "path": journey["path"]}],
    )

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
        match = inspect_map(
            doc,
            ns.target,
            current_head=current_head,
            freshness_state=str(freshness.get("state")),
        )
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
            selected_journeys: list[dict[str, Any]] = []
            seen_journeys: set[str] = set()
            for verification_item in data.get("verification", []):
                if not isinstance(verification_item, dict) or verification_item.get("kind") != "journey":
                    continue
                selector = verification_item.get("selector")
                if not isinstance(selector, str) or selector.casefold() in seen_journeys:
                    continue
                resolved_journey = resolve_journey(root, selector)
                seen_journeys.add(selector.casefold())
                selected_journeys.append({
                    "selector": selector,
                    "name": resolved_journey["name"],
                    "path": resolved_journey["path"],
                })
            journey_choice = getattr(ns, "journey", None)
            if journey_choice:
                needle = journey_choice.strip().lower().replace("\\", "/")
                matches = [
                    item for item in selected_journeys
                    if needle in {
                        str(item.get("name", "")).lower(),
                        str(item.get("path", "")).lower(),
                    }
                ]
                if len(matches) != 1:
                    raise HarnessError(
                        "JOURNEY_SELECTION_INVALID",
                        "--journey must name one of the candidate journeys for this scope.",
                        {
                            "journey": journey_choice,
                            "candidates": [
                                {"name": item.get("name"), "path": item.get("path")}
                                for item in selected_journeys
                            ],
                            "nextActions": [
                                "Re-run --record-run with --journey <name> from the candidates.",
                            ],
                        },
                    )
                selected_journeys = [matches[0]]
            elif len(selected_journeys) > 1:
                raise HarnessError(
                    "JOURNEY_SELECTION_REQUIRED",
                    "--record-run found multiple candidate journeys; choose one explicitly.",
                    {
                        "candidates": [
                            {"name": item.get("name"), "path": item.get("path")}
                            for item in selected_journeys
                        ],
                        "nextActions": [
                            "Re-run --record-run with --journey <name> from the candidates.",
                            "Exactly one candidate is frozen per receipt by design.",
                        ],
                    },
                )
            receipt, receipt_path = create_run_receipt(
                root,
                ns.task,
                requested_paths,
                data["map"],
                selected_checks,
                run_warnings,
                selected_journeys=selected_journeys,
            )
            data["runId"] = receipt["runId"]
            data["receiptPath"] = str(receipt_path)
            data["frozenJourneys"] = selected_journeys
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
        run_id = getattr(ns, "run_id", None)
        if run_id is not None:
            if ns.selector is not None or ns.pipe is not None or ns.seed is not None or ns.ready_ms is not None or ns.drive:
                raise HarnessError(
                    "RUN_JOURNEY_SCOPE_FIXED",
                    "--run-id uses the frozen journey and bound pipe in run.json; do not pass selector/session overrides.",
                )
            return journey_run_receipt(root, run_id, dry_run=ns.dry_run)
        if ns.selector is None:
            raise HarnessError(
                "JOURNEY_SELECTOR_REQUIRED",
                "journey requires a selector unless --run-id supplies a frozen journey.",
            )
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
        run_id = getattr(ns, "run_id", None)
        requested_paths = getattr(ns, "paths", None)
        if run_id is not None:
            if requested_paths is not None:
                raise HarnessError(
                    "RUN_VERIFY_SCOPE_FIXED",
                    "--run-id uses the verification scope frozen in run.json; do not pass --path.",
                )
            return verify_run_receipt(root, run_id, dry_run=ns.dry_run)
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
