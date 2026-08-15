#!/usr/bin/env python3
"""
Colosseum Guardian Loop - policy loader (Slice G1).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G1 ("The laws -
policy, forbidden paths, risk classes, and a loader that fails closed"). Program ruling 1:
"The orchestrator owns the laws, not the model." All three law files
(docs/autorepair/{policy,forbidden-paths,risk-classes}.json) live in the MAIN repo and are
read by the orchestrator from OUTSIDE any sandbox; a repair agent's own sandboxed copies are
never consulted by this loader. This module is a CLOSED-SCHEMA reader for those three files:
unknown key anywhere -> refuse; missing required field -> refuse; enum violation -> refuse.

Stdlib only (house pattern - see scripts/lanista_coverage.py, scripts/soak-digest.py). No
pip dependencies. This module performs no git/subprocess work of its own; it only reads and
validates JSON already checked out in the working tree.

Self-protection (ruling 1, mechanized): after forbidden-paths.json validates structurally,
the loader independently asserts that its own MODIFY/DELETE glob set still covers all three
law files AND the Guardian Loop's own enforcement code (scripts/autorepair/** - C6
hardening, Guardian Loop audit). A corrupted or narrowed forbidden-paths.json that no
longer protects itself (or its own enforcement code) raises SelfProtectionError rather
than silently trusting the file's content - "a model can argue; it cannot re-legislate."

Public API (imported by later Guardian Loop slices - G2 sandbox, G6 repair contract, etc.):

    load_policy(base_dir) -> Policy
    Policy.is_forbidden(path, op) -> bool     # op in {"add", "modify", "delete"}

Usage (manual sanity check; not required by any later slice):
    python scripts/autorepair/policy.py
"""

from __future__ import annotations

import fnmatch
import json
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

SCHEMA = 1

# scripts/autorepair/policy.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LAW_DIR = REPO_ROOT / "docs" / "autorepair"

LAW_FILES = ("policy.json", "forbidden-paths.json", "risk-classes.json")

# The three law files this loader itself reads, expressed as repo-relative paths for the
# self-protection assertion (ruling 1). Keep in sync with LAW_FILES/DEFAULT_LAW_DIR above.
SELF_LAW_FILES = tuple(f"docs/autorepair/{name}" for name in LAW_FILES)

# C6 hardening (Guardian Loop audit, LOW): a synthetic probe path used ONLY to assert
# that scripts/autorepair/** (the Guardian Loop's own ENFORCEMENT CODE - this loader,
# the guard hook, the repair contract, verify, the orchestrator itself) stays covered by
# forbidden-paths.json's own MODIFY/DELETE glob set, not just the three JSON law files.
# is_forbidden() is pure glob matching (never a filesystem existence check), so this
# probe path never needs to name a real file - a future edit that drops or narrows the
# scripts/autorepair/** glob fails closed here even if every real file under it is
# untouched.
SELF_ENFORCEMENT_PROBE_PATH = "scripts/autorepair/_self_protection_probe.py"

AUTONOMY_LEVELS = {"patch-only", "draft-pr", "document-only"}
CONFIDENCE_LEVELS = {"low", "medium", "high"}
MODEL_TIERS = {"opus", "sonnet", "glm"}
REFUTATION_PROVIDERS = {"glm", "deepseek"}
THINKING_LEVELS = {"off", "low", "medium", "high", "max"}
STAGE_NAMES = {"build", "triage", "diagnosis", "repair", "verify", "promotion"}
FORBIDDEN_OPS = {"add", "modify", "delete"}

POLICY_KEYS = {
    "schema",
    "autonomyLevel",
    "maxRepairAttempts",
    "maxPatchLines",
    "triage",
    "minConfidenceToRepair",
    "nightWatchAutoRepair",
    "modelRouting",
    "verifierRefutation",
    "perStageTimeoutSec",
    "perIncidentTotalSec",
}
TRIAGE_KEYS = {"runs", "confirmThreshold"}
MODEL_ROUTING_KEYS = {"diagnosis", "repair", "verify"}
VERIFIER_REFUTATION_KEYS = {"provider", "thinking", "advisory"}

FORBIDDEN_TOP_KEYS = {"schema", "forbidden", "notes"}
FORBIDDEN_REQUIRED_TOP_KEYS = {"schema", "forbidden"}
FORBIDDEN_INNER_KEYS = {"modifyDelete", "addExempt"}

RISK_TOP_KEYS = {"schema", "classes"}
RISK_CLASS_KEYS = {"id", "areaPattern", "verify"}
RISK_VERIFY_KEYS = {"unitTestLabel", "journeys", "warningGate"}


class PolicyError(RuntimeError):
    """Operational policy-loader failure (I/O, bad path, unusable at runtime)."""


class PolicySchemaError(PolicyError):
    """A law file failed closed-schema validation: unknown key, missing field, bad enum."""


class SelfProtectionError(PolicyError):
    """forbidden-paths.json no longer protects its own three law files (ruling 1)."""


# ── generic JSON/schema helpers (house pattern - mirrors scripts/lanista_coverage.py) ──


def read_json(path: Path, label: str) -> Any:
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise PolicySchemaError(f"missing {label}: {path}") from exc
    except OSError as exc:
        raise PolicySchemaError(f"cannot read {label}: {path}") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise PolicySchemaError(f"{label} is not valid JSON ({path}): {exc}") from exc


def require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise PolicySchemaError(f"{context} must be an object")
    return value


def require_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise PolicySchemaError(f"{context} must be an array")
    return value


def require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise PolicySchemaError(f"{context} must be a non-empty string")
    return value


def require_bool(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise PolicySchemaError(f"{context} must be a boolean")
    return value


def require_positive_int(value: Any, context: str) -> int:
    # bool is a subclass of int in Python; reject it explicitly so `true`/`false`
    # can never silently satisfy an integer field.
    if isinstance(value, bool) or not isinstance(value, int):
        raise PolicySchemaError(f"{context} must be an integer")
    if value <= 0:
        raise PolicySchemaError(f"{context} must be a positive integer, got {value}")
    return value


def require_enum(value: Any, allowed: set[str], context: str) -> str:
    text = require_string(value, context)
    if text not in allowed:
        raise PolicySchemaError(
            f"{context} must be one of {', '.join(sorted(allowed))}; got {text!r}"
        )
    return text


def reject_unknown_keys(obj: dict[str, Any], allowed: set[str], context: str) -> None:
    unknown = set(obj) - allowed
    if unknown:
        raise PolicySchemaError(
            f"{context} contains unknown field(s): " + ", ".join(sorted(unknown))
        )


def require_exact_keys(obj: dict[str, Any], allowed: set[str], context: str) -> None:
    """Closed-schema field set: no unknown keys, no missing required keys."""
    reject_unknown_keys(obj, allowed, context)
    missing = allowed - set(obj)
    if missing:
        raise PolicySchemaError(
            f"{context} missing required field(s): " + ", ".join(sorted(missing))
        )


def require_string_list(value: Any, context: str, *, allow_empty: bool = True) -> list[str]:
    items = require_list(value, context)
    result: list[str] = []
    for index, item in enumerate(items):
        result.append(require_string(item, f"{context}[{index}]"))
    if not allow_empty and not result:
        raise PolicySchemaError(f"{context} must not be empty")
    return result


# ── policy.json ─────────────────────────────────────────────────────────────


def validate_policy_object(raw: Any) -> dict[str, Any]:
    obj = require_object(raw, "policy")
    require_exact_keys(obj, POLICY_KEYS, "policy")

    if obj["schema"] != SCHEMA:
        raise PolicySchemaError(f"policy.schema must equal {SCHEMA}")

    autonomy_level = require_enum(
        obj["autonomyLevel"], AUTONOMY_LEVELS, "policy.autonomyLevel"
    )
    max_repair_attempts = require_positive_int(
        obj["maxRepairAttempts"], "policy.maxRepairAttempts"
    )
    max_patch_lines = require_positive_int(obj["maxPatchLines"], "policy.maxPatchLines")

    triage_raw = require_object(obj["triage"], "policy.triage")
    require_exact_keys(triage_raw, TRIAGE_KEYS, "policy.triage")
    runs = require_positive_int(triage_raw["runs"], "policy.triage.runs")
    confirm_threshold = require_positive_int(
        triage_raw["confirmThreshold"], "policy.triage.confirmThreshold"
    )
    if confirm_threshold > runs:
        raise PolicySchemaError(
            "policy.triage.confirmThreshold cannot exceed policy.triage.runs"
        )

    min_confidence = require_enum(
        obj["minConfidenceToRepair"], CONFIDENCE_LEVELS, "policy.minConfidenceToRepair"
    )
    night_watch_auto_repair = require_bool(
        obj["nightWatchAutoRepair"], "policy.nightWatchAutoRepair"
    )

    routing_raw = require_object(obj["modelRouting"], "policy.modelRouting")
    require_exact_keys(routing_raw, MODEL_ROUTING_KEYS, "policy.modelRouting")
    model_routing = {
        key: require_enum(routing_raw[key], MODEL_TIERS, f"policy.modelRouting.{key}")
        for key in MODEL_ROUTING_KEYS
    }

    refutation_raw = require_object(obj["verifierRefutation"], "policy.verifierRefutation")
    require_exact_keys(refutation_raw, VERIFIER_REFUTATION_KEYS, "policy.verifierRefutation")
    verifier_refutation = {
        "provider": require_enum(
            refutation_raw["provider"],
            REFUTATION_PROVIDERS,
            "policy.verifierRefutation.provider",
        ),
        "thinking": require_enum(
            refutation_raw["thinking"],
            THINKING_LEVELS,
            "policy.verifierRefutation.thinking",
        ),
        "advisory": require_bool(
            refutation_raw["advisory"], "policy.verifierRefutation.advisory"
        ),
    }

    timeout_raw = require_object(obj["perStageTimeoutSec"], "policy.perStageTimeoutSec")
    require_exact_keys(timeout_raw, STAGE_NAMES, "policy.perStageTimeoutSec")
    per_stage_timeout_sec = {
        stage: require_positive_int(
            timeout_raw[stage], f"policy.perStageTimeoutSec.{stage}"
        )
        for stage in STAGE_NAMES
    }

    per_incident_total_sec = require_positive_int(
        obj["perIncidentTotalSec"], "policy.perIncidentTotalSec"
    )
    tightest_stage_cap = max(per_stage_timeout_sec.values())
    if per_incident_total_sec < tightest_stage_cap:
        raise PolicySchemaError(
            "policy.perIncidentTotalSec must be >= the largest "
            "policy.perStageTimeoutSec value "
            f"({per_incident_total_sec} < {tightest_stage_cap})"
        )

    return {
        "schema": SCHEMA,
        "autonomyLevel": autonomy_level,
        "maxRepairAttempts": max_repair_attempts,
        "maxPatchLines": max_patch_lines,
        "triage": {"runs": runs, "confirmThreshold": confirm_threshold},
        "minConfidenceToRepair": min_confidence,
        "nightWatchAutoRepair": night_watch_auto_repair,
        "modelRouting": model_routing,
        "verifierRefutation": verifier_refutation,
        "perStageTimeoutSec": per_stage_timeout_sec,
        "perIncidentTotalSec": per_incident_total_sec,
    }


# ── forbidden-paths.json ────────────────────────────────────────────────────


def normalize_repo_relpath(raw: str, context: str) -> str:
    text = raw.strip().replace("\\", "/")
    if not text:
        raise PolicySchemaError(f"{context}: empty path")
    if text.startswith("/") or (len(text) > 1 and text[1] == ":"):
        raise PolicySchemaError(f"{context}: must be repo-relative, got {raw!r}")
    path = PurePosixPath(text)
    if ".." in path.parts:
        raise PolicySchemaError(f"{context}: path escapes repository root: {raw!r}")
    return path.as_posix()


def validate_forbidden_object(raw: Any) -> dict[str, Any]:
    obj = require_object(raw, "forbidden-paths")
    reject_unknown_keys(obj, FORBIDDEN_TOP_KEYS, "forbidden-paths")
    missing = FORBIDDEN_REQUIRED_TOP_KEYS - set(obj)
    if missing:
        raise PolicySchemaError(
            "forbidden-paths missing required field(s): " + ", ".join(sorted(missing))
        )

    if obj["schema"] != SCHEMA:
        raise PolicySchemaError(f"forbidden-paths.schema must equal {SCHEMA}")

    forbidden_raw = require_object(obj["forbidden"], "forbidden-paths.forbidden")
    require_exact_keys(forbidden_raw, FORBIDDEN_INNER_KEYS, "forbidden-paths.forbidden")

    modify_delete_raw = require_string_list(
        forbidden_raw["modifyDelete"],
        "forbidden-paths.forbidden.modifyDelete",
        allow_empty=False,
    )
    modify_delete: list[str] = []
    seen: set[str] = set()
    for index, pattern in enumerate(modify_delete_raw):
        context = f"forbidden-paths.forbidden.modifyDelete[{index}]"
        normalized = _normalize_glob_pattern(pattern, context)
        if normalized in seen:
            raise PolicySchemaError(f"{context}: duplicate pattern {normalized!r}")
        seen.add(normalized)
        modify_delete.append(normalized)

    add_exempt_raw = require_string_list(
        forbidden_raw["addExempt"], "forbidden-paths.forbidden.addExempt"
    )
    add_exempt: list[str] = []
    seen = set()
    for index, pattern in enumerate(add_exempt_raw):
        context = f"forbidden-paths.forbidden.addExempt[{index}]"
        normalized = _normalize_glob_pattern(pattern, context)
        if normalized in seen:
            raise PolicySchemaError(f"{context}: duplicate pattern {normalized!r}")
        seen.add(normalized)
        add_exempt.append(normalized)

    if "notes" in obj:
        notes_raw = require_object(obj["notes"], "forbidden-paths.notes")
        for key, value in notes_raw.items():
            if not isinstance(key, str) or not isinstance(value, str):
                raise PolicySchemaError(
                    "forbidden-paths.notes must map string keys to string values"
                )

    return {
        "schema": SCHEMA,
        "forbidden": {"modifyDelete": modify_delete, "addExempt": add_exempt},
    }


def _normalize_glob_pattern(raw: str, context: str) -> str:
    text = raw.strip().replace("\\", "/")
    if not text:
        raise PolicySchemaError(f"{context}: empty glob pattern")
    if text.startswith("/"):
        raise PolicySchemaError(f"{context}: glob pattern must be repo-relative, not rooted")
    return text


# ── risk-classes.json ───────────────────────────────────────────────────────


def validate_risk_classes_object(raw: Any, repo_root: Path) -> dict[str, Any]:
    obj = require_object(raw, "risk-classes")
    require_exact_keys(obj, RISK_TOP_KEYS, "risk-classes")

    if obj["schema"] != SCHEMA:
        raise PolicySchemaError(f"risk-classes.schema must equal {SCHEMA}")

    classes_raw = require_list(obj["classes"], "risk-classes.classes")
    if not classes_raw:
        raise PolicySchemaError("risk-classes.classes must not be empty")

    classes: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    for index, class_raw in enumerate(classes_raw):
        context = f"risk-classes.classes[{index}]"
        class_obj = require_object(class_raw, context)
        require_exact_keys(class_obj, RISK_CLASS_KEYS, context)

        class_id = require_string(class_obj["id"], f"{context}.id")
        if class_id in seen_ids:
            raise PolicySchemaError(f"{context}.id: duplicate risk class id {class_id!r}")
        seen_ids.add(class_id)

        area_pattern = require_string(class_obj["areaPattern"], f"{context}.areaPattern")

        verify_raw = require_object(class_obj["verify"], f"{context}.verify")
        require_exact_keys(verify_raw, RISK_VERIFY_KEYS, f"{context}.verify")

        unit_test_label = require_string(
            verify_raw["unitTestLabel"], f"{context}.verify.unitTestLabel"
        )

        journeys = require_string_list(
            verify_raw["journeys"], f"{context}.verify.journeys", allow_empty=False
        )
        journeys = [
            _ensure_repo_file_exists(repo_root, path, f"{context}.verify.journeys[{i}]")
            for i, path in enumerate(journeys)
        ]

        warning_gate = require_string(
            verify_raw["warningGate"], f"{context}.verify.warningGate"
        )
        warning_gate = _ensure_repo_file_exists(
            repo_root, warning_gate, f"{context}.verify.warningGate"
        )

        classes.append(
            {
                "id": class_id,
                "areaPattern": area_pattern,
                "verify": {
                    "unitTestLabel": unit_test_label,
                    "journeys": journeys,
                    "warningGate": warning_gate,
                },
            }
        )

    return {"schema": SCHEMA, "classes": classes}


def _ensure_repo_file_exists(repo_root: Path, rel: str, context: str) -> str:
    normalized = normalize_repo_relpath(rel, context)
    candidate = (repo_root / normalized).resolve()
    try:
        candidate.relative_to(repo_root.resolve())
    except ValueError as exc:
        raise PolicySchemaError(f"{context}: path escapes repository: {rel!r}") from exc
    if not candidate.is_file():
        raise PolicySchemaError(f"{context}: referenced file does not exist: {normalized}")
    return normalized


# ── glob matching (supports ** across path segments; case-folded for NTFS) ─


def _glob_match(pattern: str, path: str) -> bool:
    pattern_segs = [seg for seg in pattern.split("/") if seg != ""]
    path_segs = [seg for seg in path.split("/") if seg != ""]
    return _match_segments(pattern_segs, path_segs)


def _match_segments(pattern_segs: list[str], path_segs: list[str]) -> bool:
    if not pattern_segs:
        return not path_segs

    head, rest = pattern_segs[0], pattern_segs[1:]

    if head == "**":
        # ** matches zero or more whole path segments.
        if _match_segments(rest, path_segs):
            return True
        if path_segs and _match_segments(pattern_segs, path_segs[1:]):
            return True
        return False

    if not path_segs:
        return False

    # Case-fold for NTFS case-insensitivity (mirrors the G4 guard-hook's planned
    # canonicalization rule, amendment A2 - applied here too since this matcher is
    # what the orchestrator uses to classify a real patch's touched paths).
    if not fnmatch.fnmatchcase(path_segs[0].lower(), head.lower()):
        return False
    return _match_segments(rest, path_segs[1:])


# ── Policy: the loaded, validated law set ───────────────────────────────────


@dataclass(frozen=True)
class Policy:
    policy: dict[str, Any]
    forbidden_modify_delete: tuple[str, ...]
    forbidden_add_exempt: tuple[str, ...]
    risk_classes: tuple[dict[str, Any], ...]

    def is_forbidden(self, path: str, op: str) -> bool:
        """
        True if `op` on repo-relative `path` is forbidden by law.

        op must be one of "add", "modify", "delete" (ruling 2's MODIFY/DELETE guard,
        plus the ADD-under-tests/ exemption - D6/G1).

        Semantics: a path matching any `forbidden.modifyDelete` glob is off-limits for
        MODIFY and DELETE unconditionally. For ADD, that same match is forgiven only if
        the path ALSO matches a `forbidden.addExempt` glob (today, only tests/** - "the
        bug-test door"). A path matching no forbidden glob at all is never forbidden,
        regardless of op.
        """
        if op not in FORBIDDEN_OPS:
            raise PolicyError(f"unknown operation: {op!r}; expected one of {FORBIDDEN_OPS}")

        normalized = normalize_repo_relpath(path, "is_forbidden(path=...)")
        matched = any(
            _glob_match(pattern, normalized) for pattern in self.forbidden_modify_delete
        )
        if not matched:
            return False
        if op == "add":
            exempt = any(
                _glob_match(pattern, normalized) for pattern in self.forbidden_add_exempt
            )
            return not exempt
        return True


def _assert_self_protection(policy: Policy) -> None:
    unprotected = [
        law_file
        for law_file in SELF_LAW_FILES
        if not (policy.is_forbidden(law_file, "modify") and policy.is_forbidden(law_file, "delete"))
    ]
    # C6 hardening: also assert the ENFORCEMENT CODE itself (scripts/autorepair/**) stays
    # covered for modify+delete, not just the three JSON law files - a future edit that
    # drops that glob (or narrows it to no longer cover the loop's own modules) must fail
    # closed here, exactly like a narrowed law-file glob already does above.
    if not (
        policy.is_forbidden(SELF_ENFORCEMENT_PROBE_PATH, "modify")
        and policy.is_forbidden(SELF_ENFORCEMENT_PROBE_PATH, "delete")
    ):
        unprotected.append(SELF_ENFORCEMENT_PROBE_PATH)
    if unprotected:
        raise SelfProtectionError(
            "forbidden-paths.json no longer protects its own law file(s)/enforcement "
            "code - the model cannot re-legislate (Program ruling 1): "
            + ", ".join(unprotected)
        )


def load_policy(base_dir: Path | str = DEFAULT_LAW_DIR, *, repo_root: Path | str = REPO_ROOT) -> Policy:
    """
    Load and validate the three Guardian Loop law files from `base_dir`.

    `base_dir` defaults to the real docs/autorepair/ in this repo; tests point it at a
    temp copy so a single file can be corrupted without touching the shipped originals.
    `repo_root` (separate from base_dir on purpose) is only used to verify that
    risk-classes.json's referenced journeys/warning-gate files actually exist on disk -
    it defaults to this repo's real root regardless of where base_dir's copy lives, so
    corrupting one law file in a temp copy never breaks that existence check for the
    other two, still-valid files.
    """
    base = Path(base_dir)
    root = Path(repo_root)

    policy_obj = validate_policy_object(read_json(base / "policy.json", "policy.json"))
    forbidden_obj = validate_forbidden_object(
        read_json(base / "forbidden-paths.json", "forbidden-paths.json")
    )
    risk_obj = validate_risk_classes_object(
        read_json(base / "risk-classes.json", "risk-classes.json"), root
    )

    result = Policy(
        policy=policy_obj,
        forbidden_modify_delete=tuple(forbidden_obj["forbidden"]["modifyDelete"]),
        forbidden_add_exempt=tuple(forbidden_obj["forbidden"]["addExempt"]),
        risk_classes=tuple(risk_obj["classes"]),
    )

    _assert_self_protection(result)
    return result


def main(argv: list[str] | None = None) -> int:
    del argv
    try:
        policy = load_policy()
    except PolicyError as exc:
        kind = "SELF-PROTECTION ERROR" if isinstance(exc, SelfProtectionError) else "SCHEMA ERROR"
        print(f"{kind}: {exc}", file=sys.stderr)
        return 2

    print(f"POLICY OK: {DEFAULT_LAW_DIR}")
    print(f"  autonomyLevel        = {policy.policy['autonomyLevel']}")
    print(f"  maxRepairAttempts    = {policy.policy['maxRepairAttempts']}")
    print(f"  maxPatchLines        = {policy.policy['maxPatchLines']}")
    print(f"  minConfidenceToRepair= {policy.policy['minConfidenceToRepair']}")
    print(f"  forbidden globs      = {len(policy.forbidden_modify_delete)}")
    print(f"  add-exempt globs     = {len(policy.forbidden_add_exempt)}")
    print(f"  risk classes         = {len(policy.risk_classes)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
