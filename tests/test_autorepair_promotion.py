#!/usr/bin/env python3
"""test_autorepair_promotion.py - tests for scripts/autorepair/promotion.py (Guardian Loop
Slice G8: "Promotion - branch, draft PR, dossier; the human gate").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G8, decision D9,
Program ruling 6, and binding amendment A6. Pure Python, stdlib unittest, house flat
convention (D8: tests/test_autorepair_*.py). Runnable directly:

    python tests/test_autorepair_promotion.py -v

HERMETIC end to end: no git command runs, no `gh` call is made, no worktree is created, no
network call happens anywhere in this file. Every gate/guard/rebase/patch-only/orchestration
function is exercised on hand-built canned dicts; the orchestration tests inject a canned
`invoke` callable (or a call-counting spy) - the one real deferred boundary
(default_invoke()) is asserted to raise NotImplementedError, never called for real.

Test groups:
  BranchNameTests                     - branch_name_for() builds "autorepair/AR-<id>";
                                         refuses an empty/non-string incident id.
  PromotionGuardTests                 - verdict.approve False -> refuse; target_branch
                                         "master" (and "main", and "autorepair" bare, and
                                         "autorepair-fake") -> refuse; a proper
                                         "autorepair/AR-2026-08-14-0001" branch with
                                         approve=True and autonomyLevel "draft-pr" ->
                                         accept; an unrecognized autonomy_level -> refuse.
  BodyAssemblyTests                   - a complete 12-section dossier -> body string
                                         CONTAINS all 12 D9 section headings, in order;
                                         Hemanth-language check (no emoji character in the
                                         body; no subtitle/tagline line between the H1 and
                                         the first "## " section heading); a non-dict
                                         dossier refuses.
  DossierCompletenessNegativeControlTests - THE MANDATORY NEGATIVE CONTROL, both
                                         directions: a dossier with "fullRegression"
                                         entirely MISSING -> assemble_pr_body() REFUSES,
                                         naming "Full regression"; restoring it (with
                                         "warnings" instead made an EMPTY string) ->
                                         REFUSES, naming "Warnings"; a fully complete
                                         dossier -> ACCEPTS. Exact assertion: the raised
                                         PromotionError's message names the missing
                                         section's own English heading.
  RebaseFlagTests                     - A6: a conflicting rebase outcome -> rebase_flag()
                                         returns a string containing "NEEDS-REBASE" and
                                         names the conflicting file(s); a clean rebase
                                         (conflict=False) -> None; a malformed
                                         rebase_result -> refuses.
  PromotionReadyContentTests          - patch-only mode's dry-run seam: the content string
                                         names the incident id, embeds the raw patch text,
                                         and embeds the assembled dossier body; refuses on
                                         an incomplete dossier same as assemble_pr_body().
  PromoteOrchestrationSeamTests       - promote()'s injectable invoke seam, canned end to
                                         end: patch-only autonomyLevel returns
                                         promotionReadyContent and the injected invoke spy
                                         sees ZERO calls; an unapproved verdict refuses
                                         BEFORE invoke is ever called (spy still sees zero
                                         calls); draft-pr + a clean canned invoke returns
                                         prStatus="created" with the real prUrl; draft-pr +
                                         ghFailed=True returns prStatus="Bridge blocked"
                                         with prUrl=None and the reported prBodyFilePath;
                                         draft-pr + alreadyPromoted=True raises
                                         PromotionError naming "already promoted";
                                         default_invoke() raises NotImplementedError, and
                                         promote()'s own default `invoke` parameter reaches
                                         it.
"""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROMOTION_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "promotion.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


promotion_mod = _load_module("autorepair_promotion", PROMOTION_SCRIPT_PATH)

PromotionError = promotion_mod.PromotionError
DOSSIER_SECTIONS = promotion_mod.DOSSIER_SECTIONS
BRANCH_PREFIX = promotion_mod.BRANCH_PREFIX
branch_name_for = promotion_mod.branch_name_for
assemble_pr_body = promotion_mod.assemble_pr_body
validate_promotion_guards = promotion_mod.validate_promotion_guards
rebase_flag = promotion_mod.rebase_flag
promotion_ready_content = promotion_mod.promotion_ready_content
promote = promotion_mod.promote
default_invoke = promotion_mod.default_invoke


# ══════════════════════════════════════════════════════════════════════════
# canned fixtures
# ══════════════════════════════════════════════════════════════════════════


def _complete_dossier(**overrides) -> dict:
    payload = {
        "incidentId": "AR-2026-08-14-0001",
        "problem": "journey_open_manga's readerReady wait times out on reopen.",
        "rootCause": "readerReady is bound to reader visibility instead of the "
        "page-render signal in ComicReaderShell.qml.",
        "reproduction": ["lanista session run tests/lanista_scenarios/journey_open_manga.json"],
        "filesChanged": ["qml/reader/ComicReaderShell.qml", "tests/tst_reader_ready_signal.cpp"],
        "whyThisFix": "Binds readerReady to the actual page-render completion signal.",
        "negativeControl": "Bug test fails 2/2 without the fix, passes 2/2 with it.",
        "focusedTests": ["ctest -R tst_reader_ready_signal"],
        "journeyVerification": ["journey_open_manga: PASS"],
        "fullRegression": "ctest -L unit: 44/44 pass.",
        "warnings": "Warning gate clean (WARNING_GATE_OK).",
        "beforeAfterScreenshots": ["before: timeout at step 25", "after: step 25 passes"],
        "riskAssessment": "Low - the change is scoped to one QML binding.",
    }
    payload.update(overrides)
    return payload


def _incident() -> dict:
    return {"id": "AR-2026-08-14-0001", "baseSha": "deadbeef", "scenario": "journey_open_manga"}


def _approved_verdict() -> dict:
    return {"approve": True, "reasons": ["all gates green"], "riskAssessment": "low"}


def _rejected_verdict() -> dict:
    return {"approve": False, "reasons": ["unit test regressed"], "riskAssessment": "high"}


class _CannedPolicy:
    """A minimal stand-in for scripts/autorepair/policy.py's Policy dataclass - only the
    one field promote() actually reads (policy['autonomyLevel']). Mirrors
    test_autorepair_verify.py's/test_autorepair_diagnosis.py's own _CannedPolicy
    pattern."""

    def __init__(self, autonomy_level: str = "draft-pr"):
        self.policy = {"autonomyLevel": autonomy_level}


class _InvokeSpy:
    """A call-counting canned invoke callable - lets a test assert 'invoke was NEVER
    called' (the patch-only dry-run seam's own load-bearing property) as well as return a
    fixed canned payload when it IS called."""

    def __init__(self, result: dict | None = None):
        self.calls: list[tuple] = []
        self._result = result or {}

    def __call__(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        return self._result


# ══════════════════════════════════════════════════════════════════════════
# branch_name_for()
# ══════════════════════════════════════════════════════════════════════════


class BranchNameTests(unittest.TestCase):
    def test_builds_autorepair_branch(self):
        self.assertEqual(branch_name_for("AR-2026-08-14-0001"), "autorepair/AR-2026-08-14-0001")

    def test_empty_incident_id_refuses(self):
        with self.assertRaises(PromotionError):
            branch_name_for("")

    def test_non_string_incident_id_refuses(self):
        with self.assertRaises(PromotionError):
            branch_name_for(None)  # type: ignore[arg-type]


# ══════════════════════════════════════════════════════════════════════════
# validate_promotion_guards()
# ══════════════════════════════════════════════════════════════════════════


class PromotionGuardTests(unittest.TestCase):
    def test_unapproved_verdict_refuses(self):
        with self.assertRaises(PromotionError) as ctx:
            validate_promotion_guards(
                _rejected_verdict(), "autorepair/AR-2026-08-14-0001", "draft-pr"
            )
        self.assertIn("APPROVED", str(ctx.exception))

    def test_master_branch_refuses(self):
        with self.assertRaises(PromotionError) as ctx:
            validate_promotion_guards(_approved_verdict(), "master", "draft-pr")
        self.assertIn("autorepair/*", str(ctx.exception))

    def test_main_branch_refuses(self):
        with self.assertRaises(PromotionError):
            validate_promotion_guards(_approved_verdict(), "main", "draft-pr")

    def test_bare_prefix_refuses(self):
        with self.assertRaises(PromotionError):
            validate_promotion_guards(_approved_verdict(), "autorepair/", "draft-pr")

    def test_lookalike_prefix_refuses(self):
        # "autorepair-fake" is not "autorepair/*" - must not be fooled by a shared prefix
        # substring.
        with self.assertRaises(PromotionError):
            validate_promotion_guards(_approved_verdict(), "autorepair-fake/AR-1", "draft-pr")

    def test_proper_autorepair_branch_accepts(self):
        # Returns None; no exception is the pass condition.
        validate_promotion_guards(
            _approved_verdict(), "autorepair/AR-2026-08-14-0001", "draft-pr"
        )

    def test_patch_only_autonomy_level_accepts(self):
        validate_promotion_guards(
            _approved_verdict(), "autorepair/AR-2026-08-14-0001", "patch-only"
        )

    def test_unrecognized_autonomy_level_refuses(self):
        with self.assertRaises(PromotionError) as ctx:
            validate_promotion_guards(
                _approved_verdict(), "autorepair/AR-2026-08-14-0001", "merge"
            )
        self.assertIn("merge", str(ctx.exception))

    def test_missing_approve_key_refuses(self):
        with self.assertRaises(PromotionError):
            validate_promotion_guards({}, "autorepair/AR-1", "draft-pr")

    def test_non_bool_approve_refuses(self):
        with self.assertRaises(PromotionError):
            validate_promotion_guards({"approve": "true"}, "autorepair/AR-1", "draft-pr")


# ══════════════════════════════════════════════════════════════════════════
# assemble_pr_body() - D9's 12-item completeness + Hemanth-language rendering
# ══════════════════════════════════════════════════════════════════════════


class BodyAssemblyTests(unittest.TestCase):
    def test_contains_all_12_section_headings_in_order(self):
        body = assemble_pr_body(_complete_dossier())
        last_index = -1
        for _key, heading in DOSSIER_SECTIONS:
            marker = f"## {heading}"
            self.assertIn(marker, body)
            index = body.index(marker)
            self.assertGreater(index, last_index, f"{heading!r} out of D9 order")
            last_index = index

    def test_header_carries_incident_id(self):
        body = assemble_pr_body(_complete_dossier())
        self.assertIn("AR-2026-08-14-0001", body.splitlines()[0])

    def test_no_emoji_in_body(self):
        body = assemble_pr_body(_complete_dossier())
        emoji_markers = ["\U0001F600", "\U0001F389", "\U00002705", "\U0001F680"]
        for marker in emoji_markers:
            self.assertNotIn(marker, body)

    def test_no_tagline_line_between_title_and_first_section(self):
        # Structural Hemanth-language check: the line right after the H1 title must be
        # blank, and the next non-blank line must already be a "## " section heading -
        # never a subtitle/tagline line sitting between them.
        body = assemble_pr_body(_complete_dossier())
        lines = body.splitlines()
        self.assertTrue(lines[0].startswith("# "))
        self.assertEqual(lines[1], "")
        self.assertTrue(lines[2].startswith("## "), f"expected a section heading, got {lines[2]!r}")

    def test_non_dict_dossier_refuses(self):
        with self.assertRaises(PromotionError):
            assemble_pr_body(["not", "a", "dict"])  # type: ignore[arg-type]

    def test_deliberately_poisoned_dossier_with_emoji_refuses(self):
        poisoned = _complete_dossier(problem="Reader crashes \U0001F600 on reopen.")
        with self.assertRaises(PromotionError) as ctx:
            assemble_pr_body(poisoned)
        self.assertIn("emoji", str(ctx.exception).lower())


# ══════════════════════════════════════════════════════════════════════════
# THE MANDATORY NEGATIVE CONTROL (both directions) - dossier completeness
# ══════════════════════════════════════════════════════════════════════════


class DossierCompletenessNegativeControlTests(unittest.TestCase):
    def test_missing_full_regression_section_refuses_naming_it(self):
        dossier = _complete_dossier()
        del dossier["fullRegression"]
        with self.assertRaises(PromotionError) as ctx:
            assemble_pr_body(dossier)
        self.assertIn("Full regression", str(ctx.exception))

    def test_empty_warnings_section_refuses_naming_it(self):
        dossier = _complete_dossier(warnings="")
        with self.assertRaises(PromotionError) as ctx:
            assemble_pr_body(dossier)
        self.assertIn("Warnings", str(ctx.exception))

    def test_empty_list_section_refuses_naming_it(self):
        dossier = _complete_dossier(journeyVerification=[])
        with self.assertRaises(PromotionError) as ctx:
            assemble_pr_body(dossier)
        self.assertIn("Journey verification", str(ctx.exception))

    def test_restoring_the_missing_section_accepts(self):
        # Restore direction: the SAME dossier, with fullRegression and warnings both put
        # back, assembles cleanly - the completeness gate is not a permanent lockout, it
        # tracks the dossier's actual current shape.
        dossier = _complete_dossier()
        del dossier["fullRegression"]
        with self.assertRaises(PromotionError):
            assemble_pr_body(dossier)

        dossier["fullRegression"] = "ctest -L unit: 44/44 pass."
        body = assemble_pr_body(dossier)
        self.assertIn("## Full regression", body)


# ══════════════════════════════════════════════════════════════════════════
# rebase_flag() - A6
# ══════════════════════════════════════════════════════════════════════════


class RebaseFlagTests(unittest.TestCase):
    def test_conflict_returns_needs_rebase_banner(self):
        banner = rebase_flag(
            {"conflict": True, "conflictFiles": ["qml/reader/ComicReaderShell.qml"], "currentMasterSha": "abc123"}
        )
        self.assertIsNotNone(banner)
        self.assertIn("NEEDS-REBASE", banner)
        self.assertIn("qml/reader/ComicReaderShell.qml", banner)
        self.assertIn("abc123", banner)

    def test_clean_rebase_returns_none(self):
        self.assertIsNone(rebase_flag({"conflict": False}))

    def test_missing_conflict_key_refuses(self):
        with self.assertRaises(PromotionError):
            rebase_flag({})

    def test_non_bool_conflict_refuses(self):
        with self.assertRaises(PromotionError):
            rebase_flag({"conflict": "yes"})

    def test_conflict_without_files_still_returns_banner(self):
        banner = rebase_flag({"conflict": True})
        self.assertIn("NEEDS-REBASE", banner)
        self.assertIn("unknown", banner)


# ══════════════════════════════════════════════════════════════════════════
# promotion_ready_content() - patch-only dry-run seam
# ══════════════════════════════════════════════════════════════════════════


class PromotionReadyContentTests(unittest.TestCase):
    def test_content_names_incident_and_embeds_patch_and_dossier(self):
        content = promotion_ready_content(
            _incident(), "diff --git a/foo b/foo\n+fix\n", _complete_dossier(), "deadbeef"
        )
        self.assertIn("AR-2026-08-14-0001", content)
        self.assertIn("diff --git a/foo b/foo", content)
        self.assertIn("## Problem", content)
        self.assertIn("deadbeef", content)
        self.assertIn("PROMOTION READY", content)

    def test_incomplete_dossier_refuses(self):
        dossier = _complete_dossier()
        del dossier["riskAssessment"]
        with self.assertRaises(PromotionError):
            promotion_ready_content(_incident(), "diff --git a/foo b/foo\n", dossier, "deadbeef")


# ══════════════════════════════════════════════════════════════════════════
# promote() - the injectable invoke orchestration seam
# ══════════════════════════════════════════════════════════════════════════


class PromoteOrchestrationSeamTests(unittest.TestCase):
    def test_patch_only_mode_returns_content_and_never_calls_invoke(self):
        spy = _InvokeSpy()
        result = promote(
            _incident(),
            "diff --git a/foo b/foo\n",
            _approved_verdict(),
            _complete_dossier(),
            "deadbeef",
            policy_obj=_CannedPolicy(autonomy_level="patch-only"),
            invoke=spy,
        )
        self.assertEqual(result["mode"], "patch-only")
        self.assertEqual(result["targetBranch"], "autorepair/AR-2026-08-14-0001")
        self.assertIn("PROMOTION READY", result["promotionReadyContent"])
        self.assertIsNone(result["prUrl"])
        self.assertEqual(spy.calls, [], "patch-only mode must make NO invoke/git call")

    def test_unapproved_verdict_refuses_before_invoke_is_ever_called(self):
        spy = _InvokeSpy()
        with self.assertRaises(PromotionError):
            promote(
                _incident(),
                "diff --git a/foo b/foo\n",
                _rejected_verdict(),
                _complete_dossier(),
                "deadbeef",
                policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
                invoke=spy,
            )
        self.assertEqual(spy.calls, [], "guards must refuse before invoke is ever called")

    def test_draft_pr_mode_clean_invoke_returns_created_with_pr_url(self):
        spy = _InvokeSpy(
            {
                "alreadyPromoted": False,
                "rebaseResult": {"conflict": False},
                "rebaseFlag": None,
                "branchPushed": True,
                "ghFailed": False,
                "prUrl": "https://github.com/kingoftheseas56/Colosseum/pull/999",
                "prBodyFilePath": None,
            }
        )
        result = promote(
            _incident(),
            "diff --git a/foo b/foo\n",
            _approved_verdict(),
            _complete_dossier(),
            "deadbeef",
            policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
            invoke=spy,
        )
        self.assertEqual(result["mode"], "draft-pr")
        self.assertEqual(result["prStatus"], "created")
        self.assertEqual(result["prUrl"], "https://github.com/kingoftheseas56/Colosseum/pull/999")
        self.assertTrue(result["branchPushed"])
        self.assertEqual(len(spy.calls), 1)
        # promote() must have handed invoke() the assembled, complete PR body.
        _, kwargs = spy.calls[0]
        self.assertIn("## Problem", kwargs["pr_body"])
        self.assertEqual(kwargs["target_branch"], "autorepair/AR-2026-08-14-0001")

    def test_draft_pr_mode_gh_failure_reports_bridge_blocked_honestly(self):
        spy = _InvokeSpy(
            {
                "alreadyPromoted": False,
                "rebaseResult": {"conflict": False},
                "rebaseFlag": None,
                "branchPushed": True,
                "ghFailed": True,
                "prUrl": None,
                "prBodyFilePath": "artifacts/autorepair/AR-2026-08-14-0001/pr-body.md",
            }
        )
        result = promote(
            _incident(),
            "diff --git a/foo b/foo\n",
            _approved_verdict(),
            _complete_dossier(),
            "deadbeef",
            policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
            invoke=spy,
        )
        self.assertEqual(result["prStatus"], "Bridge blocked")
        self.assertIsNone(result["prUrl"])
        self.assertTrue(result["branchPushed"])
        self.assertEqual(
            result["prBodyFilePath"], "artifacts/autorepair/AR-2026-08-14-0001/pr-body.md"
        )

    def test_draft_pr_mode_already_promoted_refuses(self):
        spy = _InvokeSpy({"alreadyPromoted": True})
        with self.assertRaises(PromotionError) as ctx:
            promote(
                _incident(),
                "diff --git a/foo b/foo\n",
                _approved_verdict(),
                _complete_dossier(),
                "deadbeef",
                policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
                invoke=spy,
            )
        self.assertIn("already been promoted", str(ctx.exception))

    def test_draft_pr_mode_with_needs_rebase_flag_is_passed_through(self):
        banner = rebase_flag({"conflict": True, "conflictFiles": ["a.qml"], "currentMasterSha": "abc"})
        spy = _InvokeSpy(
            {
                "alreadyPromoted": False,
                "rebaseResult": {"conflict": True},
                "rebaseFlag": banner,
                "branchPushed": True,
                "ghFailed": False,
                "prUrl": "https://github.com/kingoftheseas56/Colosseum/pull/1000",
                "prBodyFilePath": None,
            }
        )
        result = promote(
            _incident(),
            "diff --git a/foo b/foo\n",
            _approved_verdict(),
            _complete_dossier(),
            "deadbeef",
            policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
            invoke=spy,
        )
        self.assertIn("NEEDS-REBASE", result["rebaseFlag"])

    def test_incomplete_dossier_refuses_before_invoke_is_ever_called(self):
        dossier = _complete_dossier()
        del dossier["negativeControl"]
        spy = _InvokeSpy()
        with self.assertRaises(PromotionError):
            promote(
                _incident(),
                "diff --git a/foo b/foo\n",
                _approved_verdict(),
                dossier,
                "deadbeef",
                policy_obj=_CannedPolicy(autonomy_level="draft-pr"),
                invoke=spy,
            )
        self.assertEqual(spy.calls, [])

    def test_default_invoke_raises_not_implemented(self):
        with self.assertRaises(NotImplementedError):
            default_invoke(
                _incident(),
                "diff --git a/foo b/foo\n",
                _approved_verdict(),
                _complete_dossier(),
                "deadbeef",
                target_branch="autorepair/AR-2026-08-14-0001",
                pr_body="body text",
            )

    def test_promote_default_invoke_parameter_is_default_invoke(self):
        import inspect

        sig = inspect.signature(promote)
        self.assertIs(sig.parameters["invoke"].default, default_invoke)

    def test_real_docs_autorepair_policy_loads_and_is_draft_pr_by_default(self):
        # Sanity check against the REAL shipped docs/autorepair/policy.json (Slice G1) -
        # not a canned policy - proving promote()'s policy_obj=None default path reads the
        # actual committed law file without raising.
        from policy import load_policy

        real_policy = load_policy()
        self.assertEqual(real_policy.policy["autonomyLevel"], "draft-pr")


if __name__ == "__main__":
    unittest.main(verbosity=2)
