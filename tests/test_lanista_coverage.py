#!/usr/bin/env python3
"""test_lanista_coverage.py - tests for scripts/lanista_coverage.py (Coverage Slice 1).

docs/superpowers/plans/2026-08-13-colosseum-lanista-coverage-ledger-plan.md, Slice 1
("Ledger schema + acceptance engine"). Pure Python, stdlib unittest - matching this
repo's existing pattern (tests/test_soak_digest.py, tests/test_vault_forensic_owner_thread.py,
tests/test_lanista_structural_contract.py). Runnable directly:

    python tests/test_lanista_coverage.py

Three groups of cases:

  SchemaValidationTests        - pure ledger schema checks (validate_ledger_object), no
                                  filesystem/git required. Covers the plan's "unknown
                                  state fails", "blocked/requires-OS-bridge without
                                  missingCapability fails", "covered without
                                  evidence/capability fails" cases.

  AcceptedStateSchemaTests     - load_state()/encode_state() against a temp file, no git
                                  required. Covers "malformed / hand-edited state fails
                                  closed" AND the canonical-form-exactness claim: a
                                  reordered-key, differently-indented copy of a valid
                                  state file, with a CORRECTLY recomputed integrity
                                  digest (so the digest check alone would pass), must
                                  still be rejected. That isolates the rejection to the
                                  raw-text canonical-form check and proves it is not
                                  contingent on the integrity digest failing.

  CoverageLifecycleTests        - full hermetic temp-git-repo + real CLI subprocess
                                  invocations (mirrors how a developer/hook actually
                                  runs the tool). Covers "changed watched blob drifts",
                                  "changed family ledger digest drifts", "acceptance
                                  restores CURRENT", the provenance-circularity property
                                  (accept -> check -> accept -> check stays CURRENT and
                                  never oscillates), and NC1 (mutate a fixture state
                                  covered -> coveredd, confirm EXACTLY the schema check
                                  goes red - exit 2, not the drift exit 1 - restore,
                                  confirm green).

All git-backed fixtures use a throwaway family named "synthetic-widget" seeded inside a
temporary git repository created per test - never the real Colosseum repo, and never the
Vault Browse family (Slice 3 is deliberately held; this suite must not create or imply
any Vault coverage record).
"""
from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "lanista_coverage.py"


def _load_lanista_coverage_module():
    spec = importlib.util.spec_from_file_location("lanista_coverage", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    # dataclasses (3.12) resolves ClassVar/InitVar via sys.modules[cls.__module__], so
    # the module must be registered there before exec_module() defines the dataclasses.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_lanista_coverage_module()


# ── shared ledger-record builders (used by both the pure schema tests and the
#    git-backed lifecycle fixtures) ───────────────────────────────────────────

def _surface(
    *,
    surface_id="syntheticWidgetPrimary",
    family="synthetic-widget",
    state="covered",
    target=None,
    capabilities=None,
    evidence=None,
    missing_capability=None,
    rationale=None,
):
    return {
        "id": surface_id,
        "family": family,
        "state": state,
        "target": target if target is not None else {"kind": "objectName", "value": surface_id},
        "capabilities": capabilities if capabilities is not None else {
            "actions": ["ui-click"],
            "observations": [],
        },
        "evidence": evidence if evidence is not None else ["synthetic/evidence.md"],
        "missingCapability": missing_capability,
        "rationale": rationale,
    }


def _ledger(*surfaces):
    return {"schema": 1, "surfaces": list(surfaces)}


# ══════════════════════════════════════════════════════════════════════════
# Part A - pure ledger schema validation (no filesystem, no git)
# ══════════════════════════════════════════════════════════════════════════

class SchemaValidationTests(unittest.TestCase):
    def test_unknown_state_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(_ledger(_surface(state="coveredd")))
        self.assertIn("unknown state", str(ctx.exception))

    def test_blocked_without_missing_capability_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(state="blocked", missing_capability=None))
            )
        self.assertIn("requires missingCapability", str(ctx.exception))

    def test_requires_os_bridge_without_missing_capability_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(state="requires OS bridge", missing_capability=None))
            )
        self.assertIn("requires missingCapability", str(ctx.exception))

    def test_blocked_with_capability_but_no_evidence_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(state="blocked", missing_capability="right-click", evidence=[]))
            )
        self.assertIn("requires non-empty evidence", str(ctx.exception))

    def test_covered_without_evidence_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(_ledger(_surface(state="covered", evidence=[])))
        self.assertIn("covered requires non-empty evidence", str(ctx.exception))

    def test_covered_without_capability_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(
                    state="covered",
                    capabilities={"actions": [], "observations": []},
                ))
            )
        self.assertIn(
            "covered requires at least one action or observation capability",
            str(ctx.exception),
        )

    def test_covered_with_missing_capability_set_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(state="covered", missing_capability="x"))
            )
        self.assertIn("covered cannot declare missingCapability", str(ctx.exception))

    def test_intentionally_visual_only_without_rationale_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(
                    state="intentionally visual-only",
                    evidence=[],
                    capabilities={"actions": [], "observations": []},
                ))
            )
        self.assertIn("intentionally visual-only requires rationale", str(ctx.exception))

    def test_structurally_unreachable_without_rationale_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(
                    state="structurally unreachable",
                    evidence=["e.md"],
                    capabilities={"actions": [], "observations": []},
                ))
            )
        self.assertIn("structurally unreachable requires rationale", str(ctx.exception))

    def test_structurally_unreachable_without_evidence_fails(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(
                    state="structurally unreachable",
                    evidence=[],
                    rationale="current tree hides it",
                    capabilities={"actions": [], "observations": []},
                ))
            )
        self.assertIn("structurally unreachable requires non-empty evidence", str(ctx.exception))

    def test_unknown_field_rejected(self):
        surface = _surface()
        surface["extraField"] = "oops"
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(_ledger(surface))
        self.assertIn("unknown field", str(ctx.exception))

    def test_duplicate_surface_id_rejected(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(
                _ledger(_surface(surface_id="dup"), _surface(surface_id="dup"))
            )
        self.assertIn("duplicate surface id", str(ctx.exception))

    def test_bad_family_name_rejected(self):
        with self.assertRaises(mod.CoverageSchemaError) as ctx:
            mod.validate_ledger_object(_ledger(_surface(family="Vault_Browse!")))
        self.assertIn("must match", str(ctx.exception))

    def test_valid_covered_surface_passes(self):
        ledger = mod.validate_ledger_object(_ledger(_surface()))
        self.assertEqual(len(ledger["surfaces"]), 1)
        self.assertEqual(ledger["surfaces"][0]["family"], "synthetic-widget")


# ══════════════════════════════════════════════════════════════════════════
# Part B - accepted-state.json schema / integrity / canonical-form exactness
# ══════════════════════════════════════════════════════════════════════════

class AcceptedStateSchemaTests(unittest.TestCase):
    def _valid_families(self):
        return {
            "synthetic-widget": mod.AcceptedFamily(
                blobs={"synthetic/widget.txt": "a" * 40},
                ledger_digest="sha256:" + "b" * 64,
                provenance={
                    "acceptedBy": "Test Harness",
                    "acceptedAt": "2026-08-13T00:00:00Z",
                    "acceptedAgainstCommit": "c" * 40,
                },
            )
        }

    def test_round_trip_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            families = self._valid_families()
            path.write_text(mod.encode_state(families), encoding="utf-8")
            loaded, exists = mod.load_state(path)
            self.assertTrue(exists)
            self.assertEqual(set(loaded), {"synthetic-widget"})
            self.assertEqual(
                loaded["synthetic-widget"].blobs, families["synthetic-widget"].blobs
            )

    def test_missing_state_file_returns_empty_not_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            loaded, exists = mod.load_state(path)
            self.assertEqual(loaded, {})
            self.assertFalse(exists)

    def test_malformed_state_bad_integrity_fails_closed(self):
        """The plan's 'malformed / hand-edited state fails closed' case: a blob hash is
        hand-edited without recomputing the integrity digest."""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            text = mod.encode_state(self._valid_families())
            obj = json.loads(text)
            obj["families"]["synthetic-widget"]["acceptedBlobs"]["synthetic/widget.txt"] = "f" * 40
            path.write_text(
                json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(mod.CoverageSchemaError) as ctx:
                mod.load_state(path)
            self.assertIn("integrity mismatch", str(ctx.exception))

    def test_reformatted_semantically_identical_state_is_rejected(self):
        """Canonical-form-exactness. Build a copy of a valid state file with different
        key order and indentation but a CORRECTLY recomputed integrity digest (proven
        below), then confirm load_state() still rejects it - on the canonical-form
        message specifically, not the integrity-mismatch message. This is the test that
        distinguishes a genuine textual check from an accident of json.dumps ordering:
        if the canonical-form check were not real, this reformatted-but-valid file
        would load successfully."""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            families = self._valid_families()
            payload = mod.state_payload(families)
            integrity = mod.canonical_digest(payload)
            # Sanity: this is exactly the integrity value the real encode_state() would
            # have produced for the same content.
            self.assertEqual(integrity, json.loads(mod.encode_state(families))["integrity"])

            reformatted = {
                "integrity": integrity,          # different key ORDER than encode_state
                "schema": payload["schema"],
                "families": payload["families"],
            }
            text = json.dumps(reformatted, indent=4, ensure_ascii=False) + "\n"  # different indent
            path.write_text(text, encoding="utf-8")

            # Prove the integrity digest alone still matches (isolates the failure to
            # the canonical-form check, not a coincidental integrity failure).
            reparsed = json.loads(text)
            popped = dict(reparsed)
            popped.pop("integrity")
            self.assertEqual(mod.canonical_digest(popped), integrity)

            with self.assertRaises(mod.CoverageSchemaError) as ctx:
                mod.load_state(path)
            self.assertIn("not in canonical generated form", str(ctx.exception))

    def test_malformed_blob_hash_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            obj = json.loads(mod.encode_state(self._valid_families()))
            obj["families"]["synthetic-widget"]["acceptedBlobs"]["synthetic/widget.txt"] = "not-a-hash"
            payload = {"schema": obj["schema"], "families": obj["families"]}
            text = json.dumps(
                {**payload, "integrity": mod.canonical_digest(payload)},
                indent=2, sort_keys=True, ensure_ascii=False,
            ) + "\n"
            path.write_text(text, encoding="utf-8")
            with self.assertRaises(mod.CoverageSchemaError) as ctx:
                mod.load_state(path)
            self.assertIn("malformed Git blob", str(ctx.exception))

    def test_non_object_root_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "accepted-state.json"
            path.write_text("[]\n", encoding="utf-8")
            with self.assertRaises(mod.CoverageSchemaError) as ctx:
                mod.load_state(path)
            self.assertIn("must be an object", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# Part C - full CLI lifecycle against a hermetic temp git repo
# ══════════════════════════════════════════════════════════════════════════

def _git(cwd: Path, *args: str) -> subprocess.CompletedProcess:
    result = subprocess.run(
        ["git", *args], cwd=str(cwd), capture_output=True, text=True
    )
    if result.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed: {result.stderr}")
    return result


def _run(cwd: Path, *args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    return subprocess.run(
        [sys.executable, str(SCRIPT_PATH), *args],
        cwd=str(cwd),
        capture_output=True,
        text=True,
        env=full_env,
    )


def _make_synthetic_repo(root: Path, git_user_name: str = "Fixture Author") -> None:
    """Seeds a throwaway git repo with exactly one synthetic family - never Vault, never
    the real Colosseum repo. Mirrors the shape of a real family: one ledger surface, one
    <family>.paths manifest, and the watched files it names."""
    _git(root, "init", "-q")
    _git(root, "config", "user.email", "test@example.com")
    _git(root, "config", "user.name", git_user_name)

    (root / "docs" / "lanista-coverage").mkdir(parents=True)
    (root / "synthetic").mkdir(parents=True)

    (root / "synthetic" / "widget.txt").write_text(
        "synthetic widget source v1\n", encoding="utf-8", newline="\n"
    )
    (root / "synthetic" / "evidence.md").write_text(
        "# Synthetic evidence\n\nFixture only, not real product source.\n",
        encoding="utf-8", newline="\n",
    )

    ledger = _ledger(_surface())
    (root / "docs" / "lanista-coverage" / "ledger.json").write_text(
        json.dumps(ledger, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (root / "docs" / "lanista-coverage" / "synthetic-widget.paths").write_text(
        "# synthetic fixture family for lanista_coverage.py tests\n"
        "synthetic/widget.txt\n"
        "synthetic/evidence.md\n",
        encoding="utf-8", newline="\n",
    )

    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "seed synthetic fixture family")


def _ledger_path(root: Path) -> Path:
    return root / "docs" / "lanista-coverage" / "ledger.json"


def _state_path(root: Path) -> Path:
    return root / "docs" / "lanista-coverage" / "accepted-state.json"


class CoverageLifecycleTests(unittest.TestCase):
    def test_check_before_any_acceptance_fails_closed(self):
        """No implicit first-run acceptance: --check must refuse to pass just because
        the tool was invoked before anything was ever accepted."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            result = _run(root, "--check")
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("acceptance state is missing", result.stderr)

    def test_bootstrap_then_check_is_current(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            accept = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(accept.returncode, 0, accept.stderr)
            self.assertIn("ACCEPTED synthetic-widget", accept.stdout)

            check = _run(root, "--check")
            self.assertEqual(check.returncode, 0, check.stderr)
            self.assertIn("CURRENT synthetic-widget", check.stdout)
            self.assertNotIn("DRIFTED", check.stdout)

    def test_provenance_circularity_accept_check_accept_check_stays_current(self):
        """The hardest case named in the executor brief: stamp_family_provenance()
        writes provenance INTO ledger.json during acceptance, and provenance
        participates in the per-family ledger digest that acceptance is supposed to
        capture. Prove that accepting does not chase its own tail: accept -> check ->
        accept again with nothing else changed -> check must stay CURRENT at every
        step, never DRIFTED."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)

            r1 = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(r1.returncode, 0, r1.stderr)

            r2 = _run(root, "--check")
            self.assertEqual(r2.returncode, 0, r2.stderr)
            self.assertIn("CURRENT synthetic-widget", r2.stdout)
            self.assertNotIn("DRIFTED", r2.stdout)

            blobs_after_first_accept = json.loads(
                _state_path(root).read_text(encoding="utf-8")
            )["families"]["synthetic-widget"]["acceptedBlobs"]

            # Re-accept with NOTHING else changed.
            r3 = _run(root, "--accept", "synthetic-widget", "--accepted-by", "Test Harness")
            self.assertEqual(r3.returncode, 0, r3.stderr)
            self.assertIn("ACCEPTED synthetic-widget", r3.stdout)
            # print_status() runs as part of the same --accept invocation and must
            # already show CURRENT, not DRIFTED, for the family just accepted.
            self.assertIn("CURRENT synthetic-widget", r3.stdout)
            self.assertNotIn("DRIFTED", r3.stdout)

            r4 = _run(root, "--check")
            self.assertEqual(r4.returncode, 0, r4.stderr)
            self.assertIn("CURRENT synthetic-widget", r4.stdout)
            self.assertNotIn("DRIFTED", r4.stdout)

            blobs_after_second_accept = json.loads(
                _state_path(root).read_text(encoding="utf-8")
            )["families"]["synthetic-widget"]["acceptedBlobs"]

            # Watched file content never changed, so the accepted blob hashes must be
            # byte-identical across both acceptances even though the ledger digest they
            # sit alongside is free to change (a fresh provenance timestamp is exactly
            # what a re-accept is supposed to record).
            self.assertEqual(blobs_after_first_accept, blobs_after_second_accept)

            # A third check with no action in between must still be CURRENT - proves
            # there is no slow oscillation across repeated checks either.
            r5 = _run(root, "--check")
            self.assertEqual(r5.returncode, 0, r5.stderr)
            self.assertIn("CURRENT synthetic-widget", r5.stdout)

    def test_completion_signal_accept_mutate_drift_reaccept_restores_current(self):
        """The plan's own Slice-1 completion signal: a synthetic family can be accepted
        -> CURRENT, mutated -> DRIFTED, re-accepted -> CURRENT."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)

            accept1 = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(accept1.returncode, 0, accept1.stderr)
            current1 = _run(root, "--check")
            self.assertEqual(current1.returncode, 0, current1.stderr)
            self.assertIn("CURRENT synthetic-widget", current1.stdout)

            (root / "synthetic" / "widget.txt").write_text(
                "synthetic widget source v2 - CHANGED\n", encoding="utf-8", newline="\n"
            )

            drifted = _run(root, "--check")
            self.assertEqual(drifted.returncode, 1, drifted.stderr)
            self.assertIn("DRIFTED synthetic-widget", drifted.stdout)
            self.assertIn("watched blob(s) changed: synthetic/widget.txt", drifted.stdout)

            accept2 = _run(root, "--accept", "synthetic-widget", "--accepted-by", "Test Harness")
            self.assertEqual(accept2.returncode, 0, accept2.stderr)

            current2 = _run(root, "--check")
            self.assertEqual(current2.returncode, 0, current2.stderr)
            self.assertIn("CURRENT synthetic-widget", current2.stdout)
            self.assertNotIn("DRIFTED", current2.stdout)

    def test_family_ledger_digest_drift_without_touching_watched_files(self):
        """A ledger-only edit (changing what a record CLAIMS) must drift the family even
        though no watched source/evidence file changed - the ledger digest, not just the
        blob set, is part of what acceptance protects."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            accept1 = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(accept1.returncode, 0, accept1.stderr)

            ledger_path = _ledger_path(root)
            ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
            ledger["surfaces"][0]["rationale"] = "Updated rationale text for drift test."
            ledger_path.write_text(
                json.dumps(ledger, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
            )

            drifted = _run(root, "--check")
            self.assertEqual(drifted.returncode, 1, drifted.stderr)
            self.assertIn("DRIFTED synthetic-widget", drifted.stdout)
            self.assertIn("family ledger digest changed", drifted.stdout)
            self.assertNotIn("watched blob(s) changed", drifted.stdout)
            self.assertNotIn("watched path(s)", drifted.stdout)

            accept2 = _run(root, "--accept", "synthetic-widget", "--accepted-by", "Test Harness")
            self.assertEqual(accept2.returncode, 0, accept2.stderr)
            current = _run(root, "--check")
            self.assertEqual(current.returncode, 0, current.stderr)
            self.assertIn("CURRENT synthetic-widget", current.stdout)

    def test_nc1_invalid_state_mutation_turns_exactly_schema_check_red(self):
        """NC1 (mandatory negative control): mutate a fixture state covered ->
        coveredd; confirm EXACTLY the schema check turns red - distinguished from the
        DRIFTED case by exit code (2 = SCHEMA ERROR, never 1 = DRIFTED) and by message
        - restore, and prove green again."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            accept = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(accept.returncode, 0, accept.stderr)

            baseline = _run(root, "--check")
            self.assertEqual(baseline.returncode, 0, baseline.stderr)

            ledger_path = _ledger_path(root)
            original_text = ledger_path.read_text(encoding="utf-8")
            mutated_text = original_text.replace('"state": "covered"', '"state": "coveredd"')
            self.assertNotEqual(
                mutated_text, original_text,
                "fixture ledger.json did not contain the expected literal to mutate",
            )
            ledger_path.write_text(mutated_text, encoding="utf-8")

            red = _run(root, "--check")
            self.assertEqual(
                red.returncode, 2, f"expected SCHEMA ERROR (2), got {red.returncode}: {red.stderr}"
            )
            self.assertIn("SCHEMA ERROR", red.stderr)
            self.assertIn("unknown state", red.stderr)
            self.assertIn("coveredd", red.stderr)
            # The failure must be the schema check, never the drift check.
            self.assertNotIn("DRIFTED", red.stdout)
            self.assertNotIn("DRIFTED", red.stderr)

            ledger_path.write_text(original_text, encoding="utf-8")
            green = _run(root, "--check")
            self.assertEqual(green.returncode, 0, green.stderr)
            self.assertIn("CURRENT synthetic-widget", green.stdout)

    def test_accept_unknown_family_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            result = _run(root, "--accept", "nonexistent-family", "--accepted-by", "Test Harness")
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertIn("--accept family is not current", result.stderr)

    def test_accepted_by_falls_back_to_git_user_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root, git_user_name="Configured Git Identity")
            accept = _run(root, "--accept-all-drifted")  # no --accepted-by, no env var
            self.assertEqual(accept.returncode, 0, accept.stderr)
            state = json.loads(_state_path(root).read_text(encoding="utf-8"))
            self.assertEqual(
                state["families"]["synthetic-widget"]["provenance"]["acceptedBy"],
                "Configured Git Identity",
            )

    def test_accepted_by_env_var_overrides_git_user_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root, git_user_name="Configured Git Identity")
            accept = _run(
                root, "--accept-all-drifted",
                env={"COLOSSEUM_ACCEPTED_BY": "Env Var Actor"},
            )
            self.assertEqual(accept.returncode, 0, accept.stderr)
            state = json.loads(_state_path(root).read_text(encoding="utf-8"))
            self.assertEqual(
                state["families"]["synthetic-widget"]["provenance"]["acceptedBy"],
                "Env Var Actor",
            )

    def test_explicit_accepted_by_wins_over_env_and_git(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root, git_user_name="Configured Git Identity")
            accept = _run(
                root, "--accept-all-drifted", "--accepted-by", "Explicit Flag Actor",
                env={"COLOSSEUM_ACCEPTED_BY": "Env Var Actor"},
            )
            self.assertEqual(accept.returncode, 0, accept.stderr)
            state = json.loads(_state_path(root).read_text(encoding="utf-8"))
            self.assertEqual(
                state["families"]["synthetic-widget"]["provenance"]["acceptedBy"],
                "Explicit Flag Actor",
            )

    def test_accepted_by_with_check_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            result = _run(root, "--check", "--accepted-by", "Should Not Be Allowed")
            self.assertEqual(result.returncode, 2)
            self.assertIn("--accepted-by is only valid with acceptance actions", result.stderr)

    def test_ledger_family_without_manifest_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _git(root, "init", "-q")
            _git(root, "config", "user.email", "test@example.com")
            _git(root, "config", "user.name", "Fixture Author")
            (root / "docs" / "lanista-coverage").mkdir(parents=True)
            (root / "docs" / "lanista-coverage" / "ledger.json").write_text(
                json.dumps(_ledger(_surface()), indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            # Deliberately no synthetic-widget.paths manifest.
            _git(root, "add", "-A")
            _git(root, "commit", "-q", "-m", "ledger without manifest")

            result = _run(root, "--check")
            self.assertEqual(result.returncode, 2)
            self.assertIn("missing .paths manifest", result.stderr)

    def test_manifest_without_ledger_family_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _git(root, "init", "-q")
            _git(root, "config", "user.email", "test@example.com")
            _git(root, "config", "user.name", "Fixture Author")
            (root / "docs" / "lanista-coverage").mkdir(parents=True)
            (root / "synthetic").mkdir(parents=True)
            (root / "synthetic" / "widget.txt").write_text("x\n", encoding="utf-8", newline="\n")
            (root / "docs" / "lanista-coverage" / "ledger.json").write_text(
                json.dumps(_ledger(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
            )
            (root / "docs" / "lanista-coverage" / "synthetic-widget.paths").write_text(
                "synthetic/widget.txt\n", encoding="utf-8", newline="\n"
            )
            _git(root, "add", "-A")
            _git(root, "commit", "-q", "-m", "orphan manifest")

            result = _run(root, "--check")
            self.assertEqual(result.returncode, 2)
            self.assertIn("has no ledger surfaces", result.stderr)

    def test_deleted_watched_file_fails_closed(self):
        """A watched file disappearing must not be silently treated as fine - it fails
        closed (a schema error, distinct from an ordinary drift) rather than crashing
        uncontrolled or passing."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _make_synthetic_repo(root)
            accept = _run(root, "--accept-all-drifted", "--accepted-by", "Test Harness")
            self.assertEqual(accept.returncode, 0, accept.stderr)

            (root / "synthetic" / "widget.txt").unlink()

            result = _run(root, "--check")
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertIn("missing watched source", result.stderr)


if __name__ == "__main__":
    print(f"[test_lanista_coverage] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)
