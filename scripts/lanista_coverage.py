#!/usr/bin/env python3
"""
Colosseum Lanista coverage ledger - accepted-vs-current checker.

Slice 1 of docs/superpowers/plans/2026-08-13-colosseum-lanista-coverage-ledger-plan.md
("Colosseum Lanista Coverage Ledger + Drift Block"). Adopted from a Preflight-Architect
reference implementation (agents/reference/lanista_coverage.reference.py, pinned against
b55faa16a7052b0c7f88082becf53d9594e1d88f) and verified by focused/negative tests in
tests/test_lanista_coverage.py before this file was trusted. This slice ships the schema
and the accepted/current acceptance engine only - Slice 2 wires it into
scripts/precommit-encyclopedia-check.sh, and Slice 3 seeds the first real family
(Vault Browse). Until Slice 3 lands, docs/lanista-coverage/ledger.json is deliberately
empty and every family is unledgered - absence never means `covered`.

Ports the two ideas from scripts/code_encyclopedia.py's ACCEPTED-vs-CURRENT design
(read that file first - this one is modeled directly on it):

  1. ACCEPTED vs CURRENT, tracked by Git blob hash, with an integrity-protected
     generated state file that fails closed on hand-editing.
  2. Gaps are reported, not hidden - an unaccepted or drifted family blocks rather
     than silently passing.

This port does NOT copy the encyclopedia's file-comment harvesting. A coverage record
classifies whether a UI surface is drivable by Lanista automation (D2's five closed
states: covered / structurally unreachable / intentionally visual-only /
requires OS bridge / blocked); it does not synthesize prose from source files.

What differs from the encyclopedia's acceptance model (see inline `# PORT DECISION:`
comments below for the reasoning behind each):
  - acceptance carries who/when/HEAD provenance (the encyclopedia has none);
  - acceptance is per-FAMILY (a group of ledger surfaces), not per-file, because a
    family's ledger digest - including its own provenance stamp - is itself part of
    what gets accepted (see stamp_family_provenance / build_current below: acceptance
    rebuilds `current` AFTER writing the stamped ledger, specifically so this
    self-reference converges instead of oscillating);
  - `<family>.paths` manifests are auto-discovered from docs/lanista-coverage/*.paths
    rather than passed with one --paths argument, so a future staged-path-intersection
    hook (Slice 2) can dispatch across every accepted family in one pass;
  - there is no implicit first-run acceptance: only --accept / --accept-all-drifted
    may create or update accepted state, so merely running the tool can never turn an
    unreviewed fact into an accepted one;
  - unknown ledger/state fields are rejected outright (closed schema) rather than
    silently ignored, so a typo'd field (e.g. missingCapabilty) cannot pass as valid;
  - accepted-state.json is locked to its own canonical serialized text, not just its
    parsed value: `load_state` re-serializes what it parsed and rejects the file if the
    bytes differ at all, so a semantically-equal hand reformat (reordered keys,
    different indentation) is rejected even though it would still satisfy the integrity
    digest. This is a genuine textual check, not an artifact of dict key ordering -
    verified in tests/test_lanista_coverage.py's canonical-form-exactness test.

Usage:
  python scripts/lanista_coverage.py --check
  python scripts/lanista_coverage.py --accept <family> --accepted-by "<name>"
  python scripts/lanista_coverage.py --accept-all-drifted --accepted-by "<name>"

(--ledger / --coverage-dir / --state default to the canonical docs/lanista-coverage/
locations and normally do not need overriding; tests point them at hermetic temp repos.)
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = 1
LEDGER_SCHEMA = 1

DEFAULT_COVERAGE_DIR = Path("docs/lanista-coverage")
DEFAULT_LEDGER = DEFAULT_COVERAGE_DIR / "ledger.json"
DEFAULT_STATE = DEFAULT_COVERAGE_DIR / "accepted-state.json"

COVERAGE_STATES = {
    "covered",
    "structurally unreachable",
    "intentionally visual-only",
    "requires OS bridge",
    "blocked",
}
TARGET_KINDS = {"objectName", "pattern", "none"}

FAMILY_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
HEX40_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")

SURFACE_KEYS = {
    "id",
    "family",
    "state",
    "target",
    "capabilities",
    "evidence",
    "missingCapability",
    "rationale",
    "provenance",
}
TARGET_KEYS = {"kind", "value"}
CAPABILITY_KEYS = {"actions", "observations"}
PROVENANCE_KEYS = {"acceptedBy", "acceptedAt", "acceptedAgainstCommit"}


class CoverageError(RuntimeError):
    """Operational coverage-checker failure."""


class CoverageSchemaError(CoverageError):
    """Ledger or generated-state schema failure."""


@dataclass(frozen=True)
class AcceptedFamily:
    blobs: dict[str, str]
    ledger_digest: str
    provenance: dict[str, str]


@dataclass(frozen=True)
class CurrentFamily:
    family: str
    blobs: dict[str, str]
    ledger_digest: str


@dataclass(frozen=True)
class Drift:
    family: str
    reasons: tuple[str, ...]
    removed: bool = False


def repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise CoverageError(
            "run inside a Git worktree with Git available"
        ) from exc
    return Path(result.stdout.strip()).resolve()


def git_head_sha(root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise CoverageError("cannot resolve current HEAD") from exc
    value = result.stdout.strip().lower()
    if not HEX40_RE.fullmatch(value):
        raise CoverageError(f"expected 40-char HEAD SHA, got: {value!r}")
    return value


def git_config_user_name(root: Path) -> str | None:
    try:
        result = subprocess.run(
            ["git", "config", "--get", "user.name"],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    return value or None


def resolve_accepted_by(root: Path, explicit: str | None) -> str:
    """
    Resolve acceptance attribution.

    # PORT DECISION:
    # `code_encyclopedia.py` has no provenance actor because its accepted
    # state stores only blob/comment. This port needs acceptedBy.
    #
    # Choice: explicit --accepted-by wins, then COLOSSEUM_ACCEPTED_BY, then
    # Git user.name. Acceptance fails if all three are absent.
    #
    # Alternative: require --accepted-by on every acceptance invocation.
    # The chosen fallback keeps the printed acceptance command executable in
    # normal developer worktrees while still allowing agent-specific identity.
    """
    candidates = (
        explicit,
        os.environ.get("COLOSSEUM_ACCEPTED_BY"),
        git_config_user_name(root),
    )
    for value in candidates:
        if value is not None and value.strip():
            return value.strip()
    raise CoverageError(
        "acceptance requires an actor: pass --accepted-by, set "
        "COLOSSEUM_ACCEPTED_BY, or configure git user.name"
    )


def utc_now_iso() -> str:
    return (
        datetime.now(timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z")
    )


def canonical_digest(value: Any) -> str:
    raw = json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    )
    return "sha256:" + hashlib.sha256(raw.encode("utf-8")).hexdigest()


def atomic_write(path: Path, content: str) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        newline="",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as handle:
        handle.write(content)
        temp_name = handle.name
    os.replace(temp_name, path)
    return True


def resolve_cli_path(root: Path, raw: Path) -> Path:
    return raw if raw.is_absolute() else root / raw


def display_path(root: Path, path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(root).as_posix()
    except ValueError:
        return str(resolved)


def normalize_repo_relpath(raw: str) -> str:
    text = raw.strip().replace("\\", "/")
    path = PurePosixPath(text)
    if not text or path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise CoverageSchemaError(f"invalid repository-relative path: {raw!r}")
    normalized = path.as_posix()
    if normalized in {"", "."}:
        raise CoverageSchemaError(f"invalid repository-relative path: {raw!r}")
    return normalized


def ensure_repo_file(root: Path, rel: str) -> Path:
    normalized = normalize_repo_relpath(rel)
    source = (root / normalized).resolve()
    try:
        source.relative_to(root)
    except ValueError as exc:
        raise CoverageSchemaError(
            f"path escapes repository: {normalized}"
        ) from exc
    if not source.is_file():
        raise CoverageSchemaError(f"missing watched source: {normalized}")
    return source


def git_blob(root: Path, rel: str) -> str:
    """
    Hash current worktree bytes using the repository's Git clean-filter rules.

    This deliberately mirrors the encyclopedia checker rather than using a
    plain SHA-256 of filesystem bytes.
    """
    source = ensure_repo_file(root, rel)
    data = source.read_bytes()
    try:
        result = subprocess.run(
            ["git", "hash-object", f"--path={rel}", "--stdin"],
            cwd=root,
            input=data,
            check=True,
            capture_output=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise CoverageError(f"cannot hash watched source: {rel}") from exc
    value = result.stdout.decode("ascii").strip().lower()
    if not HEX40_RE.fullmatch(value):
        raise CoverageError(f"unexpected Git blob hash for {rel}: {value!r}")
    return value


def read_json(path: Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise CoverageSchemaError(f"missing {label}: {path}") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise CoverageSchemaError(f"cannot read {label}: {path}") from exc


def require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CoverageSchemaError(f"{context} must be an object")
    return value


def require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CoverageSchemaError(f"{context} must be a non-empty string")
    return value.strip()


def require_nullable_string(value: Any, context: str) -> str | None:
    if value is None:
        return None
    return require_string(value, context)


def require_string_list(
    value: Any,
    context: str,
    *,
    allow_empty: bool = True,
) -> list[str]:
    if not isinstance(value, list):
        raise CoverageSchemaError(f"{context} must be an array")
    result: list[str] = []
    seen: set[str] = set()
    for index, item in enumerate(value):
        text = require_string(item, f"{context}[{index}]")
        if text in seen:
            raise CoverageSchemaError(
                f"{context} contains duplicate value: {text!r}"
            )
        seen.add(text)
        result.append(text)
    if not allow_empty and not result:
        raise CoverageSchemaError(f"{context} must not be empty")
    return result


def reject_unknown_keys(
    obj: dict[str, Any],
    allowed: set[str],
    context: str,
) -> None:
    """
    Reject misspelled/unknown fields instead of silently ignoring them.

    # PORT DECISION:
    # The encyclopedia state loader does not enforce a closed field set.
    # This ledger is a new machine-readable contract and field-name typos
    # (for example `missingCapabilty`) could otherwise look valid while
    # defeating state-dependent requirements.
    #
    # Alternative: tolerate unknown keys for forward-compatible extensions.
    # Chosen rule: schema increments should authorize new fields explicitly.
    """
    unknown = set(obj).difference(allowed)
    if unknown:
        raise CoverageSchemaError(
            f"{context} contains unknown field(s): "
            + ", ".join(sorted(unknown))
        )


def validate_family_name(value: Any, context: str) -> str:
    family = require_string(value, context)
    if not FAMILY_RE.fullmatch(family):
        raise CoverageSchemaError(
            f"{context} must match {FAMILY_RE.pattern}: {family!r}"
        )
    return family


def validate_provenance(
    value: Any,
    context: str,
) -> dict[str, str]:
    obj = require_object(value, context)
    reject_unknown_keys(obj, PROVENANCE_KEYS, context)
    if set(obj) != PROVENANCE_KEYS:
        missing = PROVENANCE_KEYS.difference(obj)
        raise CoverageSchemaError(
            f"{context} missing field(s): " + ", ".join(sorted(missing))
        )

    accepted_by = require_string(obj["acceptedBy"], f"{context}.acceptedBy")
    accepted_at = require_string(obj["acceptedAt"], f"{context}.acceptedAt")
    commit = require_string(
        obj["acceptedAgainstCommit"],
        f"{context}.acceptedAgainstCommit",
    ).lower()

    if not accepted_at.endswith("Z"):
        raise CoverageSchemaError(
            f"{context}.acceptedAt must be UTC ISO-8601 ending in Z"
        )
    try:
        parsed = datetime.fromisoformat(
            accepted_at[:-1] + "+00:00"
        )
    except ValueError as exc:
        raise CoverageSchemaError(
            f"{context}.acceptedAt is not valid ISO-8601: {accepted_at!r}"
        ) from exc
    if parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise CoverageSchemaError(
            f"{context}.acceptedAt must be UTC"
        )
    if not HEX40_RE.fullmatch(commit):
        raise CoverageSchemaError(
            f"{context}.acceptedAgainstCommit must be a 40-char SHA"
        )

    return {
        "acceptedBy": accepted_by,
        "acceptedAt": accepted_at,
        "acceptedAgainstCommit": commit,
    }


def validate_target(value: Any, context: str) -> dict[str, Any]:
    obj = require_object(value, context)
    reject_unknown_keys(obj, TARGET_KEYS, context)
    kind = require_string(obj.get("kind"), f"{context}.kind")
    if kind not in TARGET_KINDS:
        raise CoverageSchemaError(
            f"{context}.kind must be one of "
            + ", ".join(sorted(TARGET_KINDS))
        )

    raw_value = obj.get("value")
    if kind in {"objectName", "pattern"}:
        target_value = require_string(raw_value, f"{context}.value")
        return {"kind": kind, "value": target_value}

    if raw_value is not None:
        raise CoverageSchemaError(
            f"{context}.value must be null/absent when kind is 'none'"
        )
    return {"kind": "none", "value": None}


def validate_capabilities(value: Any, context: str) -> dict[str, list[str]]:
    obj = require_object(value, context)
    reject_unknown_keys(obj, CAPABILITY_KEYS, context)
    actions = require_string_list(
        obj.get("actions", []),
        f"{context}.actions",
    )
    observations = require_string_list(
        obj.get("observations", []),
        f"{context}.observations",
    )
    return {
        "actions": actions,
        "observations": observations,
    }


def validate_surface(
    raw: Any,
    index: int,
) -> dict[str, Any]:
    context = f"ledger.surfaces[{index}]"
    obj = require_object(raw, context)
    reject_unknown_keys(obj, SURFACE_KEYS, context)

    surface_id = require_string(obj.get("id"), f"{context}.id")
    family = validate_family_name(obj.get("family"), f"{context}.family")
    state = require_string(obj.get("state"), f"{context}.state")
    if state not in COVERAGE_STATES:
        raise CoverageSchemaError(
            f"family {family}, surface {surface_id}: unknown state {state!r}; "
            f"expected one of {', '.join(sorted(COVERAGE_STATES))}"
        )

    target = validate_target(
        obj.get("target"),
        f"family {family}, surface {surface_id}.target",
    )
    capabilities = validate_capabilities(
        obj.get("capabilities", {}),
        f"family {family}, surface {surface_id}.capabilities",
    )
    evidence = require_string_list(
        obj.get("evidence", []),
        f"family {family}, surface {surface_id}.evidence",
    )
    missing_capability = require_nullable_string(
        obj.get("missingCapability"),
        f"family {family}, surface {surface_id}.missingCapability",
    )
    rationale = require_nullable_string(
        obj.get("rationale"),
        f"family {family}, surface {surface_id}.rationale",
    )

    provenance: dict[str, str] | None = None
    if obj.get("provenance") is not None:
        provenance = validate_provenance(
            obj["provenance"],
            f"family {family}, surface {surface_id}.provenance",
        )

    if state == "covered":
        if not evidence:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: "
                "covered requires non-empty evidence"
            )
        if not capabilities["actions"] and not capabilities["observations"]:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: "
                "covered requires at least one action or observation capability"
            )
        if missing_capability is not None:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: "
                "covered cannot declare missingCapability"
            )

    if state in {"blocked", "requires OS bridge"}:
        if missing_capability is None:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: {state} requires "
                "missingCapability"
            )
        if not evidence:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: {state} requires "
                "non-empty evidence"
            )

    if state == "intentionally visual-only" and rationale is None:
        raise CoverageSchemaError(
            f"family {family}, surface {surface_id}: "
            "intentionally visual-only requires rationale"
        )

    if state == "structurally unreachable":
        if rationale is None:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: "
                "structurally unreachable requires rationale"
            )
        if not evidence:
            raise CoverageSchemaError(
                f"family {family}, surface {surface_id}: "
                "structurally unreachable requires non-empty evidence"
            )

    result: dict[str, Any] = {
        "id": surface_id,
        "family": family,
        "state": state,
        "target": target,
        "capabilities": capabilities,
        "evidence": evidence,
        "missingCapability": missing_capability,
        "rationale": rationale,
    }
    if provenance is not None:
        result["provenance"] = provenance
    return result


def validate_ledger_object(raw: Any) -> dict[str, Any]:
    """
    Canonical ledger top-level shape.

    # PORT DECISION:
    # There is no pre-existing coverage-ledger schema to copy.
    #
    # Choice:
    #   {"schema": 1, "surfaces": [ ... ]}
    #
    # Alternative:
    #   family-keyed top-level objects.
    #
    # A flat surface list makes global ID uniqueness explicit while the
    # `family` field remains the authority for acceptance/drift grouping.
    """
    obj = require_object(raw, "ledger")
    reject_unknown_keys(obj, {"schema", "surfaces"}, "ledger")
    if obj.get("schema") != LEDGER_SCHEMA:
        raise CoverageSchemaError(
            f"ledger.schema must equal {LEDGER_SCHEMA}"
        )
    surfaces_raw = obj.get("surfaces")
    if not isinstance(surfaces_raw, list):
        raise CoverageSchemaError("ledger.surfaces must be an array")

    surfaces: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    for index, raw_surface in enumerate(surfaces_raw):
        surface = validate_surface(raw_surface, index)
        if surface["id"] in seen_ids:
            raise CoverageSchemaError(
                f"duplicate surface id: {surface['id']}"
            )
        seen_ids.add(surface["id"])
        surfaces.append(surface)

    return {
        "schema": LEDGER_SCHEMA,
        "surfaces": surfaces,
    }


def load_ledger(path: Path) -> dict[str, Any]:
    return validate_ledger_object(read_json(path, "coverage ledger"))


def encode_ledger(ledger: dict[str, Any]) -> str:
    """
    Generated-on-accept representation.

    This preserves logical surface order while normalizing formatting.
    """
    return (
        json.dumps(
            ledger,
            indent=2,
            ensure_ascii=False,
        )
        + "\n"
    )


def group_surfaces(
    ledger: dict[str, Any],
) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {}
    for surface in ledger["surfaces"]:
        result.setdefault(surface["family"], []).append(surface)
    for family in result:
        result[family].sort(key=lambda item: item["id"])
    return result


def read_family_manifest(
    path: Path,
    root: Path,
    family: str,
) -> list[str]:
    if not path.is_file():
        raise CoverageSchemaError(
            f"family {family}: missing manifest {path}"
        )

    seen: set[str] = set()
    items: list[str] = []
    for number, raw in enumerate(
        path.read_text(encoding="utf-8").splitlines(),
        1,
    ):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        normalized = normalize_repo_relpath(stripped)
        if normalized in seen:
            raise CoverageSchemaError(
                f"family {family}: duplicate manifest path at line "
                f"{number}: {normalized}"
            )
        ensure_repo_file(root, normalized)
        seen.add(normalized)
        items.append(normalized)

    if not items:
        raise CoverageSchemaError(
            f"family {family}: manifest contains no watched files"
        )
    return sorted(items)


def discover_manifests(
    coverage_dir: Path,
    root: Path,
) -> dict[str, list[str]]:
    """
    Discover one `<family>.paths` manifest per ledger family.

    # PORT DECISION:
    # The encyclopedia checker receives one `--paths` manifest.
    # Coverage drift is family-scoped, so this port discovers all `*.paths`
    # manifests beneath the canonical coverage directory.
    #
    # Alternative: require one --paths argument per invocation.
    # Discovery better matches Slice 2's eventual staged-family dispatch and
    # keeps accept-all-drifted able to reconcile multiple families.
    """
    if not coverage_dir.exists():
        return {}
    if not coverage_dir.is_dir():
        raise CoverageSchemaError(
            f"coverage directory is not a directory: {coverage_dir}"
        )

    result: dict[str, list[str]] = {}
    for path in sorted(coverage_dir.glob("*.paths")):
        family = path.name[:-len(".paths")]
        validate_family_name(family, f"manifest family {family!r}")
        if family in result:
            raise CoverageSchemaError(
                f"duplicate family manifest: {family}"
            )
        result[family] = read_family_manifest(path, root, family)
    return result


def validate_family_alignment(
    grouped: dict[str, list[dict[str, Any]]],
    manifests: dict[str, list[str]],
) -> None:
    ledger_families = set(grouped)
    manifest_families = set(manifests)

    missing = ledger_families - manifest_families
    orphan = manifest_families - ledger_families

    if missing:
        raise CoverageSchemaError(
            "ledger family missing .paths manifest: "
            + ", ".join(sorted(missing))
        )
    if orphan:
        raise CoverageSchemaError(
            ".paths manifest has no ledger surfaces: "
            + ", ".join(sorted(orphan))
        )


def family_ledger_digest(
    records: list[dict[str, Any]],
) -> str:
    canonical_records = sorted(
        (copy.deepcopy(item) for item in records),
        key=lambda item: item["id"],
    )
    return canonical_digest(canonical_records)


def build_current(
    root: Path,
    ledger: dict[str, Any],
    manifests: dict[str, list[str]],
) -> dict[str, CurrentFamily]:
    grouped = group_surfaces(ledger)
    validate_family_alignment(grouped, manifests)

    result: dict[str, CurrentFamily] = {}
    for family in sorted(grouped):
        blobs = {
            rel: git_blob(root, rel)
            for rel in manifests[family]
        }
        result[family] = CurrentFamily(
            family=family,
            blobs=blobs,
            ledger_digest=family_ledger_digest(grouped[family]),
        )
    return result


def state_payload(
    families: dict[str, AcceptedFamily],
) -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "families": {
            family: {
                "acceptedBlobs": dict(sorted(item.blobs.items())),
                "acceptedLedgerDigest": item.ledger_digest,
                "provenance": item.provenance,
            }
            for family, item in sorted(families.items())
        },
    }


def encode_state(
    families: dict[str, AcceptedFamily],
) -> str:
    payload = state_payload(families)
    full = {
        **payload,
        "integrity": canonical_digest(payload),
    }
    return (
        json.dumps(
            full,
            indent=2,
            sort_keys=True,
            ensure_ascii=False,
        )
        + "\n"
    )


def load_state(
    path: Path,
) -> tuple[dict[str, AcceptedFamily], bool]:
    if not path.exists():
        return {}, False

    try:
        text = path.read_text(encoding="utf-8")
        raw = json.loads(text)
    except (OSError, json.JSONDecodeError) as exc:
        raise CoverageSchemaError(
            f"cannot read acceptance state: {path}"
        ) from exc

    obj = require_object(raw, "acceptance state")
    integrity = obj.pop("integrity", None)
    if not isinstance(integrity, str):
        raise CoverageSchemaError(
            "acceptance state missing string integrity"
        )
    if integrity != canonical_digest(obj):
        raise CoverageSchemaError(
            "acceptance state integrity mismatch; "
            "do not hand-edit generated state"
        )
    if obj.get("schema") != SCHEMA or not isinstance(
        obj.get("families"), dict
    ):
        raise CoverageSchemaError(
            "unsupported or malformed acceptance state"
        )

    # `encode_state()` is the only canonical writer. This mirrors the
    # encyclopedia checker's generated-state exactness: semantically equal
    # hand-reformatting is still not an accepted generated artifact.
    parsed: dict[str, AcceptedFamily] = {}
    for family_raw, value_raw in obj["families"].items():
        family = validate_family_name(
            family_raw,
            "acceptance state family",
        )
        value = require_object(
            value_raw,
            f"acceptance state family {family}",
        )
        reject_unknown_keys(
            value,
            {"acceptedBlobs", "acceptedLedgerDigest", "provenance"},
            f"acceptance state family {family}",
        )

        blobs_raw = require_object(
            value.get("acceptedBlobs"),
            f"acceptance state family {family}.acceptedBlobs",
        )
        blobs: dict[str, str] = {}
        for rel_raw, blob_raw in blobs_raw.items():
            if not isinstance(rel_raw, str):
                raise CoverageSchemaError(
                    f"acceptance state family {family}: blob path must be string"
                )
            rel = normalize_repo_relpath(rel_raw)
            if rel != rel_raw:
                raise CoverageSchemaError(
                    f"acceptance state family {family}: non-canonical blob path "
                    f"{rel_raw!r}"
                )
            blob = require_string(
                blob_raw,
                f"acceptance state family {family}.acceptedBlobs[{rel}]",
            ).lower()
            if not HEX40_RE.fullmatch(blob):
                raise CoverageSchemaError(
                    f"acceptance state family {family}: malformed Git blob "
                    f"for {rel}"
                )
            blobs[rel] = blob

        ledger_digest = require_string(
            value.get("acceptedLedgerDigest"),
            f"acceptance state family {family}.acceptedLedgerDigest",
        )
        if not SHA256_RE.fullmatch(ledger_digest):
            raise CoverageSchemaError(
                f"acceptance state family {family}: malformed ledger digest"
            )

        provenance = validate_provenance(
            value.get("provenance"),
            f"acceptance state family {family}.provenance",
        )
        parsed[family] = AcceptedFamily(
            blobs=blobs,
            ledger_digest=ledger_digest,
            provenance=provenance,
        )

    # PORT DECISION (undeclared in the original reference draft; called out
    # here explicitly after Slice 1 verification):
    # `code_encyclopedia.py`'s integrity digest is computed from the PARSED
    # value (`json.dumps(raw_without_integrity, sort_keys=True, ...)`), so a
    # hand-reformatted but semantically identical state file still passes it
    # unchanged there - the digest cannot see formatting, only content. This
    # port adds a stricter textual check on top: reserialize the parsed value
    # with `encode_state` and require it to equal the file's raw text
    # byte-for-byte. Confirmed by test: a reordered-key/rewhitespaced copy of
    # a valid, integrity-passing state file is still rejected here.
    #
    # Alternative: keep only the integrity digest, matching the encyclopedia
    # exactly. Rejected because accepted-state.json is documented (D3) as
    # fully generated; a human should never be able to make it green by
    # reformatting rather than by running --accept.
    canonical = encode_state(parsed)
    if text != canonical:
        raise CoverageSchemaError(
            "acceptance state is not in canonical generated form; "
            "do not hand-edit generated state"
        )
    return parsed, True


def compare_families(
    current: dict[str, CurrentFamily],
    accepted: dict[str, AcceptedFamily],
) -> tuple[list[str], list[Drift]]:
    current_names: list[str] = []
    drifted: list[Drift] = []

    for family in sorted(current):
        now = current[family]
        old = accepted.get(family)
        if old is None:
            drifted.append(
                Drift(
                    family,
                    ("no accepted state for current family",),
                )
            )
            continue

        reasons: list[str] = []
        if old.ledger_digest != now.ledger_digest:
            reasons.append("family ledger digest changed")

        old_paths = set(old.blobs)
        new_paths = set(now.blobs)
        added = sorted(new_paths - old_paths)
        removed = sorted(old_paths - new_paths)
        changed = sorted(
            rel
            for rel in old_paths & new_paths
            if old.blobs[rel] != now.blobs[rel]
        )
        if added:
            reasons.append(
                "watched path(s) added: " + ", ".join(added)
            )
        if removed:
            reasons.append(
                "watched path(s) removed: " + ", ".join(removed)
            )
        if changed:
            reasons.append(
                "watched blob(s) changed: " + ", ".join(changed)
            )

        if reasons:
            drifted.append(Drift(family, tuple(reasons)))
        else:
            current_names.append(family)

    for family in sorted(set(accepted) - set(current)):
        drifted.append(
            Drift(
                family,
                ("accepted family is absent from current ledger/manifests",),
                removed=True,
            )
        )

    return current_names, drifted


def new_provenance(
    root: Path,
    accepted_by: str,
) -> dict[str, str]:
    return {
        "acceptedBy": accepted_by,
        "acceptedAt": utc_now_iso(),
        "acceptedAgainstCommit": git_head_sha(root),
    }


def stamp_family_provenance(
    ledger: dict[str, Any],
    families: set[str],
    provenance: dict[str, str],
) -> None:
    """
    Stamp accepted provenance into every record of an accepted family.

    # PORT DECISION:
    # The approved plan/commission says entries carry provenance and that
    # provenance is written on acceptance through one path. There is no
    # equivalent field in `code_encyclopedia.py`.
    #
    # Choice: the acceptance action stamps the same family-level acceptance
    # event into each record AND stores it in accepted-state.json.
    # The per-family ledger digest therefore protects provenance edits too.
    #
    # Alternative: keep provenance only in generated acceptance state.
    # That would avoid rewriting ledger.json during acceptance, but entries
    # would no longer visibly carry the provenance required by the plan.
    #
    # This is the source of the "provenance circularity": accepting mutates
    # the very ledger digest it is about to accept. See build_current() calls
    # in main() below - `current` is deliberately rebuilt AFTER this stamp is
    # written to disk, so the accepted digest always matches the post-stamp
    # ledger rather than the pre-stamp one. Verified idempotent under repeated
    # accept -> check -> accept in tests/test_lanista_coverage.py.
    """
    for surface in ledger["surfaces"]:
        if surface["family"] in families:
            surface["provenance"] = dict(provenance)


def replace_accepted_family(
    accepted: dict[str, AcceptedFamily],
    current: dict[str, CurrentFamily],
    family: str,
    provenance: dict[str, str],
) -> None:
    now = current[family]
    accepted[family] = AcceptedFamily(
        blobs=dict(now.blobs),
        ledger_digest=now.ledger_digest,
        provenance=dict(provenance),
    )


def acceptance_command(
    root: Path,
    ledger_path: Path,
    coverage_dir: Path,
    state_path: Path,
    family: str | None,
) -> str:
    script = display_path(root, Path(__file__).resolve())
    ledger = display_path(root, ledger_path)
    coverage = display_path(root, coverage_dir)
    state = display_path(root, state_path)

    pieces = [
        "python",
        script,
        "--ledger",
        ledger,
        "--coverage-dir",
        coverage,
        "--state",
        state,
    ]
    if family is None:
        pieces.append("--accept-all-drifted")
    else:
        pieces.extend(["--accept", family])

    # Paths in the canonical repo locations contain no spaces today. This
    # human-facing action is intentionally plain rather than shell-specific.
    return " ".join(pieces)


def print_status(
    root: Path,
    ledger_path: Path,
    coverage_dir: Path,
    state_path: Path,
    current_names: list[str],
    drifted: list[Drift],
) -> None:
    for family in current_names:
        print(f"CURRENT {family}")

    for item in drifted:
        print(f"DRIFTED {item.family}: " + "; ".join(item.reasons))
        if item.removed:
            action = acceptance_command(
                root,
                ledger_path,
                coverage_dir,
                state_path,
                None,
            )
        else:
            action = acceptance_command(
                root,
                ledger_path,
                coverage_dir,
                state_path,
                item.family,
            )
        print(f"ACCEPT {item.family}: {action}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description=(
            "Validate and acceptance-track Colosseum Lanista coverage facts."
        )
    )
    result.add_argument(
        "--ledger",
        type=Path,
        default=DEFAULT_LEDGER,
        help=f"coverage ledger (default: {DEFAULT_LEDGER.as_posix()})",
    )
    result.add_argument(
        "--coverage-dir",
        type=Path,
        default=DEFAULT_COVERAGE_DIR,
        help=(
            "directory containing <family>.paths manifests "
            f"(default: {DEFAULT_COVERAGE_DIR.as_posix()})"
        ),
    )
    result.add_argument(
        "--state",
        type=Path,
        default=DEFAULT_STATE,
        help=f"generated acceptance state (default: {DEFAULT_STATE.as_posix()})",
    )
    mode = result.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--check",
        action="store_true",
        help="fail non-zero when schema/state is invalid or any family drifts",
    )
    mode.add_argument(
        "--accept",
        metavar="FAMILY",
        help="accept/re-accept exactly one current family",
    )
    mode.add_argument(
        "--accept-all-drifted",
        action="store_true",
        help=(
            "accept all currently drifted families and ratify removal of "
            "accepted families no longer present"
        ),
    )
    result.add_argument(
        "--accepted-by",
        metavar="NAME",
        help=(
            "acceptance actor; fallback: COLOSSEUM_ACCEPTED_BY, then git user.name"
        ),
    )
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)

    try:
        root = repo_root()
        ledger_path = resolve_cli_path(root, args.ledger)
        coverage_dir = resolve_cli_path(root, args.coverage_dir)
        state_path = resolve_cli_path(root, args.state)

        if args.check and args.accepted_by:
            raise CoverageError(
                "--accepted-by is only valid with acceptance actions"
            )

        ledger = load_ledger(ledger_path)
        manifests = discover_manifests(coverage_dir, root)
        current = build_current(root, ledger, manifests)
        accepted, state_exists = load_state(state_path)

        if args.check:
            if not state_exists:
                raise CoverageSchemaError(
                    f"acceptance state is missing: {state_path}; "
                    "bootstrap deliberately with --accept-all-drifted"
                )
            current_names, drifted = compare_families(current, accepted)
            print_status(
                root,
                ledger_path,
                coverage_dir,
                state_path,
                current_names,
                drifted,
            )
            return 1 if drifted else 0

        # No implicit first-run acceptance.
        #
        # PORT DECISION:
        # `code_encyclopedia.py` seeds a missing accepted entry during a
        # non-check run. This port refuses to make "covered" facts true merely
        # because the tool was invoked: only --accept/--accept-all-drifted may
        # create or update accepted coverage state.
        #
        # Alternative: auto-bootstrap current families. Rejected because that
        # would turn first observation into acceptance without review.

        if args.accept is not None:
            family = validate_family_name(args.accept, "--accept FAMILY")
            if family not in current:
                raise CoverageError(
                    f"--accept family is not current: {family}"
                )
            actor = resolve_accepted_by(root, args.accepted_by)
            provenance = new_provenance(root, actor)

            stamp_family_provenance(ledger, {family}, provenance)
            # Validate the post-stamp ledger before writing.
            ledger = validate_ledger_object(ledger)
            atomic_write(ledger_path, encode_ledger(ledger))

            # Provenance participates in the ledger digest, so rebuild after
            # stamping and writing the canonical ledger.
            current = build_current(root, ledger, manifests)
            replace_accepted_family(
                accepted,
                current,
                family,
                provenance,
            )
            atomic_write(state_path, encode_state(accepted))

            current_names, drifted = compare_families(current, accepted)
            print(f"ACCEPTED {family}")
            print_status(
                root,
                ledger_path,
                coverage_dir,
                state_path,
                current_names,
                drifted,
            )
            return 0

        # --accept-all-drifted
        _, pre_drift = compare_families(current, accepted)
        current_drifted = {
            item.family for item in pre_drift if not item.removed
        }
        removed_accepted = {
            item.family for item in pre_drift if item.removed
        }

        if current_drifted:
            actor = resolve_accepted_by(root, args.accepted_by)
            provenance = new_provenance(root, actor)
            stamp_family_provenance(
                ledger,
                current_drifted,
                provenance,
            )
            ledger = validate_ledger_object(ledger)
            atomic_write(ledger_path, encode_ledger(ledger))
            current = build_current(root, ledger, manifests)
            for family in sorted(current_drifted):
                replace_accepted_family(
                    accepted,
                    current,
                    family,
                    provenance,
                )
                print(f"ACCEPTED {family}")

        if removed_accepted:
            # PORT DECISION:
            # A deleted family cannot be named by --accept because it is no
            # longer current. `--accept-all-drifted` is the explicit action
            # that ratifies removal by pruning stale accepted-state entries.
            #
            # Alternative: add a separate --accept-removed FAMILY command.
            # The commission requested accept-one + accept-all-drifted only,
            # so this keeps the CLI within that contract.
            for family in sorted(removed_accepted):
                accepted.pop(family, None)
                print(f"ACCEPTED REMOVAL {family}")

        # Bootstrap an empty canonical state too; Slice 1 may exist before any
        # family is seeded in Slice 3.
        atomic_write(state_path, encode_state(accepted))

        current_names, remaining = compare_families(current, accepted)
        print_status(
            root,
            ledger_path,
            coverage_dir,
            state_path,
            current_names,
            remaining,
        )
        return 0

    except CoverageSchemaError as exc:
        print(f"SCHEMA ERROR: {exc}", file=sys.stderr)
        return 2
    except CoverageError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
