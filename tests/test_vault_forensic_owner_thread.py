#!/usr/bin/env python3
"""test_vault_forensic_owner_thread.py — validates tests/contracts/vault-forensic-owner-thread.json

Slice F0 (discovery only, no production source changed). This script is the "contract test"
named in docs/superpowers/plans/2026-08-13-colosseum-visibility-phase2-plan.md's F0 section: it
rejects a contract with missing/incomplete citations, and rejects any of the five forbidden
conditions being marked "allowed" (true).

Usage:
    python tests/test_vault_forensic_owner_thread.py

Negative control:
    Point VAULT_FORENSIC_CONTRACT_PATH at a TEMPORARY copy of the real contract with exactly
    forbiddenConditions.secondConnectionAllowed flipped false -> true. Only
    test_rejects_second_connection may go red; every other case must still pass. Never edit the
    committed contract to run this control — copy it, mutate the copy, point the env var at the
    copy, then unset the env var (or just don't set it) to rerun against the real, green contract.

        set VAULT_FORENSIC_CONTRACT_PATH=<path to mutated temp copy>   (cmd)
        $env:VAULT_FORENSIC_CONTRACT_PATH = "<path>"                   (PowerShell)
        VAULT_FORENSIC_CONTRACT_PATH=<path> python tests/test_vault_forensic_owner_thread.py  (sh)
"""

import json
import os
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONTRACT_PATH = REPO_ROOT / "tests" / "contracts" / "vault-forensic-owner-thread.json"

# The six required top-level narrative fields plus the five forbidden-condition booleans:
# every one of these MUST have at least one citation whose "claim" equals the key or starts
# with "<key>." (a citation may support a field with more than one pin).
REQUIRED_CLAIM_KEYS = [
    "dbOwner",
    "connectionFactory",
    "ownerThread",
    "publishThread",
    "safeProjectionOwner",
    "safeInvocation",
    "forbiddenConditions.secondConnectionAllowed",
    "forbiddenConditions.foreignThreadAccessAllowed",
    "forbiddenConditions.writerAllowed",
    "forbiddenConditions.mutationAllowed",
    "forbiddenConditions.identityCarryWideningAllowed",
]

REQUIRED_TOP_LEVEL_FIELDS = [
    "dbOwner",
    "connectionFactory",
    "ownerThread",
    "publishThread",
    "safeProjectionOwner",
    "safeInvocation",
    "forbiddenConditions",
]

REQUIRED_FORBIDDEN_KEYS = [
    "secondConnectionAllowed",
    "foreignThreadAccessAllowed",
    "writerAllowed",
    "mutationAllowed",
    "identityCarryWideningAllowed",
]


def _contract_path() -> Path:
    override = os.environ.get("VAULT_FORENSIC_CONTRACT_PATH")
    if override:
        return Path(override)
    return DEFAULT_CONTRACT_PATH


def _load_contract() -> dict:
    path = _contract_path()
    if not path.is_file():
        raise FileNotFoundError(f"contract not found: {path}")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


class VaultForensicOwnerThreadContractTest(unittest.TestCase):
    """Validates tests/contracts/vault-forensic-owner-thread.json (Slice F0)."""

    @classmethod
    def setUpClass(cls):
        cls.contract = _load_contract()

    # ── Shape ──────────────────────────────────────────────────────────────

    def test_schema_and_required_fields_present(self):
        for field in REQUIRED_TOP_LEVEL_FIELDS:
            self.assertIn(field, self.contract, f"contract missing required field '{field}'")

    def test_forbidden_conditions_block_has_exactly_five_booleans(self):
        fc = self.contract.get("forbiddenConditions", {})
        for key in REQUIRED_FORBIDDEN_KEYS:
            self.assertIn(key, fc, f"forbiddenConditions missing '{key}'")
            self.assertIsInstance(
                fc[key], bool, f"forbiddenConditions.{key} must be a JSON boolean, got {type(fc[key])}"
            )

    # ── Citations: reject missing citations ──────────────────────────────

    def test_citations_present(self):
        citations = self.contract.get("citations")
        self.assertTrue(citations, "contract carries no 'citations' array — every claim must be pinned")

    def test_citations_have_required_keys(self):
        for i, c in enumerate(self.contract.get("citations", [])):
            for key in ("claim", "file", "line"):
                self.assertIn(key, c, f"citations[{i}] missing '{key}': {c}")
            self.assertTrue(str(c["claim"]).strip(), f"citations[{i}] has an empty claim")
            self.assertTrue(str(c["file"]).strip(), f"citations[{i}] has an empty file")

    def test_citations_reference_existing_files(self):
        for i, c in enumerate(self.contract.get("citations", [])):
            file_field = str(c["file"])
            if file_field.startswith("COMMAND:"):
                continue  # a shell-command provenance note (e.g. git rev-parse), not a repo file
            target = REPO_ROOT / file_field
            self.assertTrue(
                target.is_file(),
                f"citations[{i}] cites a file that does not exist in the repo: {file_field}",
            )

    def test_every_required_claim_has_citation(self):
        claims = {str(c.get("claim", "")) for c in self.contract.get("citations", [])}
        for key in REQUIRED_CLAIM_KEYS:
            has_citation = any(claim == key or claim.startswith(key + ".") for claim in claims)
            self.assertTrue(
                has_citation,
                f"required claim '{key}' has no citation in the contract's 'citations' array "
                f"(missing citation is a reject, per F0's contract test requirement)",
            )

    # ── The five forbidden conditions: reject any marked allowed ─────────

    def test_rejects_second_connection(self):
        fc = self.contract.get("forbiddenConditions", {})
        self.assertFalse(
            fc.get("secondConnectionAllowed", True),
            "forbiddenConditions.secondConnectionAllowed is true — a second SQLite connection "
            "to the Vault's own database is forbidden by the F0 stop law",
        )

    def test_rejects_foreign_thread_access(self):
        fc = self.contract.get("forbiddenConditions", {})
        self.assertFalse(
            fc.get("foreignThreadAccessAllowed", True),
            "forbiddenConditions.foreignThreadAccessAllowed is true — store access from a "
            "foreign thread is forbidden by the F0 stop law",
        )

    def test_rejects_writer(self):
        fc = self.contract.get("forbiddenConditions", {})
        self.assertFalse(
            fc.get("writerAllowed", True),
            "forbiddenConditions.writerAllowed is true — a writer on the forensic read path is "
            "forbidden by the F0 stop law",
        )

    def test_rejects_mutation(self):
        fc = self.contract.get("forbiddenConditions", {})
        self.assertFalse(
            fc.get("mutationAllowed", True),
            "forbiddenConditions.mutationAllowed is true — a mutation on the forensic read path "
            "is forbidden by the F0 stop law",
        )

    def test_rejects_identity_carry_widening(self):
        fc = self.contract.get("forbiddenConditions", {})
        self.assertFalse(
            fc.get("identityCarryWideningAllowed", True),
            "forbiddenConditions.identityCarryWideningAllowed is true — widening "
            "VaultIndex::publish()'s identity-carry is forbidden by the F0 stop law",
        )


if __name__ == "__main__":
    # Print the resolved contract path so a reader can tell at a glance which file (real or a
    # negative-control temp copy) this run validated.
    print(f"[test_vault_forensic_owner_thread] contract: {_contract_path()}")
    unittest.main(verbosity=2)
