#!/usr/bin/env python3
"""Validates docs/visibility/lanista-structural-gap.md's machine-readable appendix
(Slice L1-Discovery, visibility Phase 2). This is a discovery-decision contract test,
not a runtime test: it proves the DOCUMENT is internally consistent and that the
forbidden GammaRay shipping modes are all declared false. It does not touch the
lanista bridge, the harness, or any production source.

The document path is overridable via LANISTA_STRUCTURAL_GAP_DOC so the mandatory
negative control can point at a temporary mutated copy without editing the
committed document (mirrors the house style used by F0's forensic-owner contract:
mutate a COPY, never the artifact itself).
"""
from __future__ import annotations

import json
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DOC = ROOT / "docs" / "visibility" / "lanista-structural-gap.md"


def _doc_path() -> Path:
    override = os.environ.get("LANISTA_STRUCTURAL_GAP_DOC")
    return Path(override) if override else DEFAULT_DOC


def _extract_appendix(doc_path: Path) -> dict:
    """Pull the JSON object out of the '## Machine-readable appendix' fenced block."""
    if not doc_path.is_file():
        raise AssertionError(f"structural gap document not found: {doc_path}")
    text = doc_path.read_text(encoding="utf-8")
    heading = "## Machine-readable appendix"
    if heading not in text:
        raise AssertionError(f"document is missing the '{heading}' heading")
    tail = text.split(heading, 1)[1]
    match = re.search(r"```json\s*\n(.*?)\n```", tail, re.DOTALL)
    if not match:
        raise AssertionError("no fenced ```json block found after the appendix heading")
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        raise AssertionError(f"appendix JSON does not parse: {exc}") from exc


class LanistaStructuralGapAppendixTests(unittest.TestCase):
    """Structural/shape checks on the appendix -- independent of the GammaRay field,
    so a negative control that flips only gammaRayProbeShipped cannot turn these red."""

    @classmethod
    def setUpClass(cls):
        cls.doc_path = _doc_path()
        cls.appendix = _extract_appendix(cls.doc_path)

    def test_schema_id_is_pinned(self):
        self.assertEqual(
            self.appendix.get("schema"),
            "colosseum.visibility.l1-discovery.v1",
        )

    def test_baseline_counts_are_present_and_sane(self):
        baseline = self.appendix.get("baseline")
        self.assertIsInstance(baseline, dict, "appendix must carry a 'baseline' object")
        for key in ("namedItems", "estimatedUnnamedItems", "estimatedTotalItems"):
            self.assertIn(key, baseline, f"baseline missing '{key}'")
            self.assertIsInstance(baseline[key], int)
            self.assertGreater(baseline[key], 0, f"baseline.{key} must be a positive count")
        self.assertEqual(
            baseline["namedItems"] + baseline["estimatedUnnamedItems"],
            baseline["estimatedTotalItems"],
            "named + unnamed must reconcile to the stated total",
        )
        self.assertIn("harnessScene", self.appendix)
        self.assertTrue(
            self.appendix["harnessScene"].endswith("lanista_harness_scene.qml"),
            "baseline must be captured against the real harness scene, not invented",
        )

    def test_required_now_list_is_populated_and_shaped(self):
        required = self.appendix.get("requiredNow")
        self.assertIsInstance(required, list)
        self.assertGreater(len(required), 0, "requiredNow must not be empty")
        for row in required:
            self.assertIsInstance(row, dict)
            self.assertIn("field", row)
            self.assertIn("reason", row)
            self.assertTrue(row["reason"], "every required-now row needs a non-empty reason")
        # The structural fields the plan's default design names must all be accounted for.
        fields = {row["field"] for row in required}
        expected = {
            "everyQQuickItemIncluded",
            "nullableObjectName",
            "parentHandleOrName",
            "childCount",
            "localRectAndSceneRect",
            "z",
            "effectiveVisibilityOpacityEnabled",
            "rootWindowBounds",
            "clippingAncestorChain",
            "requestBounding_root_maxDepth_maxItems",
            "replyByteCeilingWithTruncation",
        }
        missing = expected - fields
        self.assertFalse(missing, f"requiredNow is missing plan-named fields: {sorted(missing)}")

    def test_deferred_list_names_binding_and_model_with_no_forcing_checkpoint(self):
        deferred = self.appendix.get("deferred")
        self.assertIsInstance(deferred, list)
        by_field = {row["field"]: row for row in deferred}
        for field in ("arbitraryBindingGraph", "modelEnumeration"):
            self.assertIn(field, by_field, f"deferred list must name '{field}'")
            row = by_field[field]
            self.assertIn("reason", row)
            self.assertTrue(row["reason"])
            # A missing/None checkpoint means "no named Phase 2 checkpoint forces this yet."
            # A populated checkpoint would mean this capability should NOT be deferred.
            self.assertIsNone(
                row.get("forcingPhase2Checkpoint"),
                f"'{field}' names a forcing checkpoint but is still marked deferred",
            )

    def test_verdict_recommends_bridge_extension_not_gammaray(self):
        verdict = self.appendix.get("verdict")
        self.assertIsInstance(verdict, dict)
        self.assertFalse(verdict.get("gammaRayAdopted"))
        self.assertTrue(verdict.get("bridgeExtensionRecommended"))
        self.assertEqual(verdict.get("nextSlice"), "L1-Bridge")


class LanistaStructuralGapForbiddenShippingModeTests(unittest.TestCase):
    """The one mandatory-negative-control case. Scoped to gammaRayProbeShipped ONLY --
    flipping that single field in a temporary copy of the document must turn exactly
    this test red and no other test in this file."""

    @classmethod
    def setUpClass(cls):
        cls.doc_path = _doc_path()
        cls.appendix = _extract_appendix(cls.doc_path)

    def test_rejects_shipping_gammaray_probe(self):
        modes = self.appendix.get("forbiddenShippingModes")
        self.assertIsInstance(modes, dict, "appendix must carry 'forbiddenShippingModes'")
        self.assertIn("gammaRayProbeShipped", modes)
        self.assertFalse(
            modes["gammaRayProbeShipped"],
            "GammaRay's probe must never ship -- GPL-2.0-or-later/dual-license and "
            "DLL-injection rule it out (plan ruling #4); L1 extends Lanista instead.",
        )

    def test_other_forbidden_modes_also_stay_false(self):
        # A separate case so this file's OWN mutation discipline is provable: flipping
        # gammaRayProbeShipped alone must not make THIS case red, and vice versa.
        modes = self.appendix.get("forbiddenShippingModes")
        self.assertIsInstance(modes, dict)
        for key in ("gammaRayLinked", "gammaRayVendoredInRepo"):
            self.assertIn(key, modes)
            self.assertFalse(modes[key], f"'{key}' must stay false -- nothing GammaRay-related ships")


if __name__ == "__main__":
    unittest.main()
