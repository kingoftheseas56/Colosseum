#!/usr/bin/env python3
"""
Colosseum Guardian Loop - Promotion: branch, draft PR, dossier; the human gate (Slice G8).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G8 ("Promotion -
branch, draft PR, dossier; the human gate"). Purpose: hand Hemanth a decision, not a diff -
everything he needs to judge the repair in one page, and nothing lands without him.

Decision D9 (the PR body is the spec's 12-item dossier, verbatim): Problem, Root cause,
Reproduction, Files changed, Why this fix, Negative control, Focused tests, Journey
verification, Full regression, Warnings, Before/after screenshots, Risk assessment -
written in Hemanth-language, no color, no emoji, no taglines (Colosseum product-rule
memory, restated here because this is the one module that renders end-user-facing text).

Program ruling 6 (autonomy ceiling is B): success ends at a draft PR on branch
`autorepair/AR-<id>` plus dossier - no merge, no push to master. `policy.autonomyLevel`
(policy.py's own AUTONOMY_LEVELS = {"patch-only", "draft-pr"}) carries the ladder: A
(`patch-only`, no PR, shakedown mode) is available today; C (autonomous merge) is a future
policy change, not built here at all - this module never merges anything, in any mode.

Program ruling 10 (Rule 28 carve-out, ratified by the spec itself): "master-only cannot
remain absolute for autonomous repair" - the branch/apply/commit work happens via a TEMP
WORKTREE of the MAIN repo, scoped strictly to `autorepair/*` branches, so the dirty main
working tree (other lanes' WIP) is never checked out over. This module documents that
carve-out's exact shape in default_invoke()'s own docstring; it never performs the git
work itself (see the DEFERRED section below).

Binding amendment A6 (HEAD drift handling) - implemented verbatim: "Promotion attempts a
rebase of the patch onto current master; on conflict it keeps the base-SHA branch and the
PR body opens with a prominent NEEDS-REBASE flag - the human gate decides, never silent
staleness." rebase_flag() below is the pure judgment of an already-collected rebase-attempt
outcome; the live rebase attempt itself is default_invoke()'s job (DEFERRED).

This module splits into the same two-layer shape as policy.py (G1)/sandbox.py (G2)/
triage.py (G4)/diagnosis.py (G5)/repair_contract.py (G6)/verify.py (G7):

  1. FIVE pure, hermetic pieces - zero I/O, zero subprocess, zero git, zero gh:
       assemble_pr_body(dossier)                          - D9's 12-item completeness gate
                                                              + Hemanth-language Markdown
                                                              rendering.
       validate_promotion_guards(verdict, target_branch,
                                  autonomy_level)            - the promotion guard triad:
                                                              approved-only, autorepair/*
                                                              only, a recognized autonomy
                                                              level.
       rebase_flag(rebase_result)                          - A6's pure judgment of an
                                                              already-collected rebase
                                                              outcome.
       promotion_ready_content(incident, patch, dossier,
                                base_sha)                    - the "patch-only" autonomy
                                                              mode's PROMOTION-READY.md
                                                              content, a pure string
                                                              builder (the dry-run seam:
                                                              no git/gh call anywhere in
                                                              this function or its callers
                                                              when autonomyLevel is
                                                              "patch-only").
       branch_name_for(incident_id)                        - "autorepair/AR-<id>", the one
                                                              sanctioned branch-name shape
                                                              (ruling 6).
     Every deterministic test in tests/test_autorepair_promotion.py exercises these five
     directly on canned data - no git, no gh, no worktree, no network call anywhere.

  2. promote(incident, patch, verdict, dossier, base_sha, ...) - the orchestration entry:
     guards -> assemble body -> (patch-only: return promotion_ready_content(), invoke()
     NEVER called) or (draft-pr: the LIVE git/gh work) - ALL of the live half sits behind
     the single injectable `invoke` seam (mirrors run_verify()'s/run_repair()'s own seam
     exactly). `invoke`'s DEFAULT (default_invoke) raises NotImplementedError - see its own
     docstring for the exact real sequence it stands in for: git fetch, a temp worktree of
     the MAIN repo (Rule 28 carve-out), branch `autorepair/AR-<id>` from base_sha, apply
     the patch, a single commit (dossier trailer + Co-Authored-By), push, `gh pr create
     --draft` with the assembled (and A6-flagged) body - a `gh` failure falls back to
     "push branch + write the body to a file, report the PR step Bridge blocked honestly"
     rather than silently claiming success; re-running promotion on an already-promoted
     incident refuses "already promoted" (idempotence). This is the golden incident's real
     draft PR on GitHub - explicitly DEFERRED to the Guardian Loop's batched runtime pass
     (named here, not silent - owed by this slice per its own instructions).

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,triage,diagnosis,
repair_contract,verify}.py). No pip dependencies. This module performs NO I/O of its own
anywhere - not even a mechanical subprocess call (unlike repair_contract.py's
count_patch_lines() or sandbox.py's git plumbing): every real action Promotion needs (git
fetch/worktree/branch/apply/commit/push, `gh pr create`) is entirely behind the injected
`invoke` seam.

Public API:

    PromotionError                                          # the one named refusal type
    DOSSIER_SECTIONS                                          # D9's 12 (key, heading) pairs
    BRANCH_PREFIX                                             # "autorepair/"
    branch_name_for(incident_id) -> str                        # pure
    assemble_pr_body(dossier) -> str                            # pure, hermetic
    validate_promotion_guards(verdict, target_branch,
                               autonomy_level) -> None            # pure, hermetic
    rebase_flag(rebase_result) -> str | None                     # pure, hermetic (A6)
    promotion_ready_content(incident, patch, dossier,
                             base_sha) -> str                     # pure, dry-run seam
    promote(incident, patch, verdict, dossier, base_sha, *,
            policy_obj=None, invoke=default_invoke) -> dict         # orchestration
    default_invoke(incident, patch, verdict, dossier, base_sha,
                    *, target_branch, pr_body) -> dict               # DEFERRED, raises

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by
this slice per its own instructions):

  - The live `git fetch` + temp-worktree branch/apply/commit/push sequence in the MAIN
    repo, scoped to `autorepair/*` only (Rule 28 carve-out, ruling 10) - see
    default_invoke()'s own docstring for the exact real sequence it stands in for.
  - The live rebase-onto-current-master attempt (A6) that produces the `rebaseResult` dict
    rebase_flag() judges - this module's rebase_flag() is pure and fully tested on canned
    outcomes; only the live `git rebase` subprocess call itself is deferred.
  - The live `gh pr create --draft` call on the golden incident (G8's own completion
    criterion: "a real draft PR exists for the golden incident, URL captured") + the live
    `gh` authentication/failure handling.
  - The live idempotence check (does `autorepair/AR-<id>` already exist locally or on
    origin, or does an open PR already reference it) - this module's promote() correctly
    turns an already-computed `{"alreadyPromoted": True}` invoke() result into a refusal
    (deterministically tested); only the live git/gh lookup that PRODUCES that boolean is
    deferred.

Usage (manual sanity check on canned data only - no git, no gh, no live invoke; mirrors
policy.py's/triage.py's/diagnosis.py's/repair_contract.py's/verify.py's own
`python scripts/autorepair/<module>.py` pattern):

    python scripts/autorepair/promotion.py
"""

from __future__ import annotations

import fnmatch
import re
import sys
from pathlib import Path
from typing import Any, Callable

# scripts/autorepair/promotion.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling import (house pattern: flat scripts/autorepair/, no package __init__.py - see
# sandbox.py's/repair_contract.py's/verify.py's identical sys.path setup). Program ruling
# 1: policy.py owns AUTONOMY_LEVELS/policy.autonomyLevel; this module only reads it via
# load_policy(), it never hard-codes or re-derives its own copy of the enum.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import AUTONOMY_LEVELS, Policy, load_policy  # noqa: E402  (after sys.path setup)

__all__ = [
    "REPO_ROOT",
    "PromotionError",
    "BRANCH_PREFIX",
    "DOSSIER_SECTIONS",
    "branch_name_for",
    "assemble_pr_body",
    "validate_promotion_guards",
    "rebase_flag",
    "promotion_ready_content",
    "promote",
    "default_invoke",
]


class PromotionError(RuntimeError):
    """The one named G8 promotion refusal: an incomplete dossier (D9's own completeness
    gate), a failed guard (verdict not approved, a non-`autorepair/*` target branch, an
    unrecognized autonomy level), a malformed rebase outcome, or an already-promoted
    incident (idempotence). Every reject path in this module raises exactly this type -
    mirrors RepairContractError's/VerifyError's own "one named type per stage" shape."""


# ══════════════════════════════════════════════════════════════════════════
# branch_name_for() - the one sanctioned branch-name shape (ruling 6)
# ══════════════════════════════════════════════════════════════════════════

BRANCH_PREFIX = "autorepair/"


def branch_name_for(incident_id: str) -> str:
    """Program ruling 6: "success ends at a draft PR on branch `autorepair/AR-<id>`."
    Pure string formatting - no I/O, no git. `incident_id` is expected to already be an
    `AR-YYYY-MM-DD-NNNN`-shaped id (G3's own incident.json 'id' field), but this function
    does not itself validate that shape - validate_promotion_guards() is the mechanical
    gate that judges the RESULT against the `autorepair/*` pattern; this function only
    builds the candidate."""
    if not isinstance(incident_id, str) or not incident_id.strip():
        raise PromotionError(f"incident_id must be a non-empty string; got {incident_id!r}")
    return f"{BRANCH_PREFIX}{incident_id.strip()}"


# ══════════════════════════════════════════════════════════════════════════
# 1. assemble_pr_body() - D9's 12-item completeness gate + Hemanth-language rendering
# ══════════════════════════════════════════════════════════════════════════

# D9, verbatim order. Interpretation call (the plan's own text names the 12 English
# section titles but does not pin JSON key names for a `dossier` dict - flagged in the
# execution report, not silent): this module chooses one camelCase key per section,
# mirroring diagnosis.json's/verdict.json's own key-naming convention elsewhere in this
# program. A caller building `dossier` (the orchestrator, once G9 exists) is free to carry
# additional metadata keys alongside these 12 (e.g. "incidentId", used below only to
# decorate the body's own header line) - this gate checks ONLY that these 12 required
# keys are present and non-empty; it does not reject an unknown extra key the way
# policy.py's/diagnosis.json's/verdict.json's closed schemas do, because a dossier is an
# ASSEMBLED artifact from multiple stages' own evidence, not a single model's output
# contract.
DOSSIER_SECTIONS: tuple[tuple[str, str], ...] = (
    ("problem", "Problem"),
    ("rootCause", "Root cause"),
    ("reproduction", "Reproduction"),
    ("filesChanged", "Files changed"),
    ("whyThisFix", "Why this fix"),
    ("negativeControl", "Negative control"),
    ("focusedTests", "Focused tests"),
    ("journeyVerification", "Journey verification"),
    ("fullRegression", "Full regression"),
    ("warnings", "Warnings"),
    ("beforeAfterScreenshots", "Before/after screenshots"),
    ("riskAssessment", "Risk assessment"),
)

# Colosseum product rule (memory: "No color, no emoji - gray, black, white, and SVG"),
# restated as a mechanical self-check on the FULLY ASSEMBLED body - the same
# "self-protection, mechanized" shape as policy.py's own _assert_self_protection() /
# verify.py's find_forbidden_verifier_exhibits(): rather than trusting every dossier
# section's free-form text to already be clean, scan the rendered output and refuse to
# return a body that fails the house rule. Common pictographic/emoji Unicode blocks -
# not an exhaustive Unicode emoji classifier (that would need an external table this
# stdlib-only module deliberately does not carry), but covers the overwhelmingly common
# case (faces, symbols, dingbats, transport/map symbols, regional-indicator flag pairs).
_EMOJI_RE = re.compile(
    "[\U0001F300-\U0001FAFF\U00002600-\U000027BF\U0001F1E6-\U0001F1FF✀-➿☀-⛿]"
)


def _section_missing_or_empty(dossier: dict[str, Any], key: str) -> bool:
    if key not in dossier:
        return True
    value = dossier[key]
    if value is None:
        return True
    if isinstance(value, str) and not value.strip():
        return True
    if isinstance(value, (list, tuple, dict)) and len(value) == 0:
        return True
    return False


def _render_section_value(value: Any) -> str:
    """Best-effort, deliberately simple rendering (mirrors strip_diff_comments()'s own
    "not a real tokenizer, an honest heuristic" spirit): a string renders as its own
    paragraph; a list renders as a bullet list (dict items inside a list render as one
    bullet each, `key=value` joined); a bare dict renders as `key: value` lines."""
    if isinstance(value, str):
        return value.strip()
    if isinstance(value, dict):
        return "\n".join(f"- {k}: {v}" for k, v in value.items())
    if isinstance(value, (list, tuple)):
        lines = []
        for item in value:
            if isinstance(item, dict):
                inner = "; ".join(f"{k}={v}" for k, v in item.items())
                lines.append(f"- {inner}")
            else:
                lines.append(f"- {item}")
        return "\n".join(lines)
    return str(value)


def _assert_hemanth_language_clean(body: str) -> None:
    found = _EMOJI_RE.findall(body)
    if found:
        raise PromotionError(
            "REFUSED (Hemanth-language: no color, no emoji, no taglines) - the assembled "
            f"PR body contains emoji/pictographic character(s): {found!r}"
        )


def assemble_pr_body(dossier: dict[str, Any]) -> str:
    """
    D9: "the PR body is the spec's 12-item dossier ... written in Hemanth-language, no
    color, no emoji, no taglines." Pure: no I/O, no filesystem, no git.

    REFUSES (raises PromotionError) if ANY of the 12 DOSSIER_SECTIONS keys is missing from
    `dossier` or holds an empty value (empty/whitespace-only string, or an empty
    list/tuple/dict) - the mechanical completeness gate this slice's own instructions call
    for ("a dossier that isn't complete cannot be promoted"), naming every missing/empty
    section by its English heading so the caller knows exactly what to fill in.

    On success, renders a Markdown body: an H1 header (decorated with `dossier["incidentId"]`
    when present - never a subtitle/tagline line, just the incident id inline in the same
    heading), then one `## <Heading>` section per DOSSIER_SECTIONS entry in D9's own
    order, each followed by `_render_section_value()`'s rendering of that section's
    content. Self-checks the fully assembled body against `_assert_hemanth_language_clean()`
    before returning - the no-emoji house rule is enforced mechanically, not left to
    the caller's discipline alone (no mechanized "no tagline" check is attempted: unlike
    emoji, "tagline" is a judgment call this module does not try to pattern-match, so the
    discipline here is structural instead - the renderer itself never emits a subtitle
    line under the H1, only `## `-level section headings).
    """
    if not isinstance(dossier, dict):
        raise PromotionError(f"dossier must be a dict; got {dossier!r}")

    missing = [heading for key, heading in DOSSIER_SECTIONS if _section_missing_or_empty(dossier, key)]
    if missing:
        raise PromotionError(
            "REFUSED (D9 - the PR body is the spec's 12-item dossier; a dossier that "
            "isn't complete cannot be promoted): missing or empty section(s): "
            + ", ".join(missing)
        )

    incident_id = dossier.get("incidentId")
    header = "# Guardian Loop repair" + (f" - {incident_id}" if incident_id else "")

    lines: list[str] = [header, ""]
    for key, heading in DOSSIER_SECTIONS:
        lines.append(f"## {heading}")
        lines.append("")
        lines.append(_render_section_value(dossier[key]))
        lines.append("")

    body = "\n".join(lines).rstrip() + "\n"
    _assert_hemanth_language_clean(body)
    return body


# ══════════════════════════════════════════════════════════════════════════
# 2. validate_promotion_guards() - the promotion guard triad
# ══════════════════════════════════════════════════════════════════════════


def validate_promotion_guards(verdict: dict[str, Any], target_branch: str, autonomy_level: str) -> None:
    """
    Pure, hermetic. The three mechanical guards this slice's own instructions name,
    checked in this order:

      1. `verdict["approve"]` must be `True` - Ruling 4/G7's own ordering: "only an
         approved verdict promotes." A missing/non-bool/False `approve` refuses.
      2. `target_branch` must match `autorepair/*` (BRANCH_PREFIX) - Program ruling 6:
         "never master, never any other branch." Neither a bare `autorepair/` (nothing
         after the prefix) nor any branch outside the prefix is ever accepted.
      3. `autonomy_level` must be one of policy.py's own `AUTONOMY_LEVELS` (imported, never
         re-derived - Program ruling 1: "the orchestrator owns the laws, not the model").

    Raises PromotionError naming exactly which guard failed; returns None (no exception)
    when all three clear.
    """
    if not isinstance(verdict, dict) or "approve" not in verdict:
        raise PromotionError(f"verdict must be a dict with an 'approve' key; got {verdict!r}")
    approve = verdict["approve"]
    if not isinstance(approve, bool):
        raise PromotionError(f"verdict['approve'] must be a boolean; got {approve!r}")
    if not approve:
        raise PromotionError(
            "REFUSED (only an APPROVED verdict promotes - Ruling 4/G7's own ordering): "
            f"verdict.approve is False; reasons={verdict.get('reasons')!r}"
        )

    if not isinstance(target_branch, str) or not target_branch:
        raise PromotionError(f"target_branch must be a non-empty string; got {target_branch!r}")
    if target_branch == BRANCH_PREFIX or not fnmatch.fnmatchcase(target_branch, f"{BRANCH_PREFIX}*"):
        raise PromotionError(
            "REFUSED (Program ruling 6 - autonomy ceiling B; never master, never any "
            f"other branch): target_branch must match '{BRANCH_PREFIX}*'; got {target_branch!r}"
        )

    if autonomy_level not in AUTONOMY_LEVELS:
        raise PromotionError(
            f"autonomy_level must be one of {sorted(AUTONOMY_LEVELS)!r}; got {autonomy_level!r}"
        )


# ══════════════════════════════════════════════════════════════════════════
# 3. rebase_flag() - A6, HEAD drift handling (BINDING)
# ══════════════════════════════════════════════════════════════════════════


def rebase_flag(rebase_result: dict[str, Any]) -> str | None:
    """
    Binding amendment A6, verbatim: "Promotion attempts a rebase of the patch onto current
    master; on conflict it keeps the base-SHA branch and the PR body opens with a
    prominent NEEDS-REBASE flag - the human gate decides, never silent staleness."

    Pure: no I/O, no subprocess, no git - this function only judges an ALREADY-COLLECTED
    outcome of a real rebase-attempt subprocess (default_invoke()'s own job, DEFERRED).
    `rebase_result` is expected to carry at least `{"conflict": bool}`, and optionally
    `"conflictFiles": [...]` and `"currentMasterSha": str` for a richer banner.

    Returns a prominent Markdown banner string (containing the literal "NEEDS-REBASE"
    marker) when `rebase_result["conflict"]` is True. Returns None when the rebase was
    clean (`conflict` is False) - including the case where base_sha already IS current
    master, i.e. there was nothing to rebase. The base-SHA branch itself is kept either
    way (A6's own text: "it keeps the base-SHA branch") - this function never signals a
    branch change, only whether to prepend the banner.
    """
    if not isinstance(rebase_result, dict) or "conflict" not in rebase_result:
        raise PromotionError(
            f"rebase_result must be a dict with a 'conflict' key; got {rebase_result!r}"
        )
    conflict = rebase_result["conflict"]
    if not isinstance(conflict, bool):
        raise PromotionError(f"rebase_result['conflict'] must be a boolean; got {conflict!r}")

    if not conflict:
        return None

    conflict_files = rebase_result.get("conflictFiles") or []
    current_master = rebase_result.get("currentMasterSha", "<unknown>")
    files_text = ", ".join(sorted(conflict_files)) if conflict_files else "unknown"
    return (
        "**NEEDS-REBASE**\n\n"
        "This branch is built on a base commit that has drifted from master (current "
        f"master: `{current_master}`). Rebasing the patch onto current master hit a "
        "conflict and was NOT applied automatically - the base-SHA branch is kept as-is, "
        "nothing was silently rebased or force-pushed. Review and rebase by hand before "
        "merging; this is the human gate's call, never a silent staleness (binding "
        "amendment A6).\n\n"
        f"Conflicting file(s): {files_text}"
    )


# ══════════════════════════════════════════════════════════════════════════
# 4. promotion_ready_content() - the "patch-only" autonomy mode's dry-run seam
# ══════════════════════════════════════════════════════════════════════════


def promotion_ready_content(
    incident: dict[str, Any],
    patch: str,
    dossier: dict[str, Any],
    base_sha: str,
) -> str:
    """
    The `autonomyLevel: "patch-only"` path (policy.py's AUTONOMY_LEVELS' shakedown rung):
    "skip the PR entirely and write a PROMOTION-READY.md file (return its content) instead
    of any git/gh call." Pure content-producer - this function performs NO file write and
    NO git/gh call of its own; it only builds the Markdown string. promote() below writes
    this content nowhere itself either (that write, if wanted, is the caller's job) - the
    load-bearing property this slice's own instructions ask for is that NEITHER this
    function NOR promote()'s patch-only branch ever calls `invoke` (asserted directly in
    tests/test_autorepair_promotion.py via a spy `invoke` that must see zero calls).

    Reuses assemble_pr_body(dossier) for the embedded dossier section (the SAME D9
    completeness gate applies in patch-only mode - a patch is never "promotion ready"
    with an incomplete dossier either, even without a PR).
    """
    body = assemble_pr_body(dossier)
    incident_id = incident.get("id", "<unknown incident>")
    return (
        f"# PROMOTION READY - {incident_id}\n\n"
        "policy.autonomyLevel is 'patch-only' (the shakedown rung of Program ruling 6's "
        "autonomy ladder) - no branch was created, no draft PR was opened, and no git/gh "
        "call was made anywhere in this run. The repair is verified and ready to promote; "
        "switch policy.autonomyLevel to 'draft-pr' and re-run promotion for a real branch "
        "+ draft PR, or apply the patch below by hand.\n\n"
        f"Base commit: `{base_sha}`\n\n"
        "## Patch\n\n"
        "```diff\n"
        f"{patch.rstrip()}\n"
        "```\n\n"
        "## Dossier\n\n"
        f"{body}"
    )


# ══════════════════════════════════════════════════════════════════════════
# promote(): the orchestration entry (live half DEFERRED)
# ══════════════════════════════════════════════════════════════════════════


def default_invoke(
    incident: dict[str, Any],
    patch: str,
    verdict: dict[str, Any],
    dossier: dict[str, Any],
    base_sha: str,
    *,
    target_branch: str,
    pr_body: str,
) -> dict[str, Any]:
    """
    DEFERRED (Guardian Loop batched-runtime pass, explicitly named by this slice's own
    instructions - the live branch/apply/push + real `gh pr create --draft` this slice
    owes and does not perform): the real live draft-PR promotion sequence. Its eventual
    implementation would, entirely in the MAIN repo (never a sandbox - Promotion is the
    one Guardian Loop stage whose live work targets the real repo, guarded by
    validate_promotion_guards() having already run in promote() before invoke() is ever
    called):

      1. Idempotence check (FIRST, before any mutation): does `target_branch` already
         exist locally or on origin, or does an open PR already reference this incident id?
         If so, return `{"alreadyPromoted": True, ...}` immediately - promote() below turns
         that into a clean PromotionError("already promoted") refusal; no branch/commit/
         push/PR call happens past this point for an already-promoted incident.
      2. `git fetch origin` in the MAIN repo (read-only network op, the one network call
         this whole program's v0 scope permits - Promotion, not any sandboxed agent stage).
      3. A rebase-onto-current-master ATTEMPT (A6): `git rebase origin/master` against the
         patch's own commit, collecting `{"conflict": bool, "conflictFiles": [...],
         "currentMasterSha": str}` as `rebaseResult`. Call `rebase_flag(rebaseResult)`
         (this module's own pure A6 function, reused not reimplemented) and, if it returns
         a banner, prepend it to `pr_body` before anything is ever pushed or shown to `gh`
         - "the PR body opens with a prominent NEEDS-REBASE flag." Either way, per A6's own
         text, "it keeps the base-SHA branch" - a conflicting rebase attempt is abandoned
         (`git rebase --abort`), never force-applied.
      4. A TEMP WORKTREE of the MAIN repo (`git worktree add <temp-dir> -b <target_branch>
         <base_sha>`) - Rule 28's carve-out (ruling 10), scoped STRICTLY to
         `target_branch` matching `autorepair/*` (already asserted by
         validate_promotion_guards() before this function is ever called): this is what
         lets the dirty main working tree (other lanes' WIP, per Rule 28's own "no new
         worktree... without Hemanth's explicit yes" general rule) stay completely
         untouched - the worktree is a SEPARATE checkout, never `git checkout` inside the
         primary working tree.
      5. `git apply <patch>` inside that worktree.
      6. A SINGLE commit: `git commit` whose message carries a dossier trailer (e.g.
         `Dossier: artifacts/autorepair/<id>/report.md`) plus a
         `Co-Authored-By: Colosseum Guardian Loop <noreply@colosseum.local>` trailer - "one
         commit," never a stack of the repair's own attempt-ledger commits (G6's sandbox
         history is squashed here into exactly one).
      7. `git push origin <target_branch>` from the worktree.
      8. `gh pr create --draft --base master --head <target_branch> --title <incident
         summary> --body <pr_body>` (the A6-flagged body from step 3). On SUCCESS: capture
         the PR URL, remove the temp worktree, return `{"alreadyPromoted": False,
         "rebaseResult": ..., "rebaseFlag": ..., "branchPushed": True, "ghFailed": False,
         "prUrl": <url>, "prBodyFilePath": None}`.
      9. On a `gh` FAILURE (auth, rate limit, network): the branch is ALREADY pushed (step
         7 already succeeded) - write `pr_body` to
         `artifacts/autorepair/<id>/pr-body.md` and return `{"alreadyPromoted": False,
         "rebaseResult": ..., "rebaseFlag": ..., "branchPushed": True, "ghFailed": True,
         "prUrl": None, "prBodyFilePath": <path>}` - promote() below turns this into an
         honestly-reported `prStatus: "Bridge blocked"` result, NEVER a silently-claimed
         success (this slice's own instructions, verbatim: "report the PR step Bridge
         blocked honestly").

    Never merges anything, in any branch of this sequence - Program ruling 6's autonomy
    ceiling is enforced by this function simply never containing a `git merge`/`gh pr
    merge` call anywhere, not by a runtime guard (there is nothing to guard against
    a call this code never makes).

    NOT implemented and NOT called by anything in this module's deterministic tests -
    calling this raises loudly rather than silently fabricating a branch, a commit, a
    push, or a PR URL, so the deferred boundary can never be crossed by accident.
    promote()'s own default `invoke` parameter points here; every deterministic test
    supplies its OWN canned invoke callable instead (mirrors triage.py's/diagnosis.py's/
    repair_contract.py's/verify.py's own default_invoke()/run_once() seam exactly).
    """
    raise NotImplementedError(
        "default_invoke() is the DEFERRED live Promotion sequence (Guardian Loop batched "
        "runtime pass) - pass an injected invoke callable (a canned "
        "alreadyPromoted/rebaseResult/rebaseFlag/branchPushed/ghFailed/prUrl/"
        "prBodyFilePath payload) for deterministic use; "
        f"incident={incident.get('id')!r}, base_sha={base_sha!r}, "
        f"target_branch={target_branch!r}, verdict_approve={verdict.get('approve')!r}, "
        f"pr_body_len={len(pr_body) if isinstance(pr_body, str) else None!r}"
    )


def promote(
    incident: dict[str, Any],
    patch: str,
    verdict: dict[str, Any],
    dossier: dict[str, Any],
    base_sha: str,
    *,
    policy_obj: Policy | None = None,
    invoke: Callable[..., dict[str, Any]] = default_invoke,
) -> dict[str, Any]:
    """
    Slice G8's orchestration entry, in the exact order this slice's own instructions
    specify: "guards -> assemble body -> then the LIVE git/gh work."

      1. `validate_promotion_guards(verdict, target_branch, autonomy_level)` - refuses
         BEFORE any body is assembled or any git/gh work is even considered, on an
         unapproved verdict, a non-`autorepair/*` branch, or an unrecognized autonomy
         level. `target_branch` is derived here via `branch_name_for(incident['id'])` -
         the caller never supplies it directly, so there is exactly one sanctioned branch
         name per incident id.
      2. `assemble_pr_body(dossier)` - D9's completeness gate; refuses on an incomplete
         dossier regardless of autonomy level (patch-only mode still needs a complete
         dossier - see promotion_ready_content()'s own docstring).
      3a. `autonomyLevel == "patch-only"`: returns `promotion_ready_content(...)` directly.
          `invoke` is NEVER called in this branch - the dry-run seam this slice's own
          instructions call for ("make the patch-only path a pure content-producer
          testable with NO git call").
      3b. `autonomyLevel == "draft-pr"`: calls `invoke(...)` ONCE (the injectable, DEFERRED
          live seam - see default_invoke()'s own docstring for the exact real sequence it
          stands in for), then applies purely mechanical post-processing to its result:
            - `live["alreadyPromoted"]` True -> raises PromotionError naming "already
              promoted" (idempotence, Slice G8's own regression path).
            - `live["ghFailed"]` True -> returns a result with `prStatus: "Bridge blocked"`,
              `prUrl: None`, and the `prBodyFilePath` invoke() reported - never silently
              claims a PR exists when `gh` failed.
            - otherwise -> returns a result with `prStatus: "created"` and the real
              `prUrl` invoke() reported.

    `policy_obj` defaults to the REAL committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - exactly diagnose()'s/run_repair()'s/run_verify()'s own
    `policy_obj` parameter shape; this only reads JSON plus whatever `invoke` itself does,
    so calling this with a canned `invoke` (or in patch-only mode, with no invoke call at
    all) never touches git or the network.

    Returns one of two shapes:
      mode="patch-only" - `{"mode", "incidentId", "targetBranch",
                            "promotionReadyContent", "prUrl": None}`.
      mode="draft-pr"    - `{"mode", "incidentId", "targetBranch", "branchPushed", "prUrl",
                            "prStatus" ("created"|"Bridge blocked"), "prBodyFilePath",
                            "rebaseFlag"}`.
    """
    if policy_obj is None:
        policy_obj = load_policy()

    incident_id = incident.get("id")
    if not incident_id:
        raise PromotionError(f"incident['id'] is required; got incident={incident!r}")

    target_branch = branch_name_for(incident_id)
    autonomy_level = policy_obj.policy["autonomyLevel"]

    validate_promotion_guards(verdict, target_branch, autonomy_level)
    body = assemble_pr_body(dossier)

    if autonomy_level == "patch-only":
        content = promotion_ready_content(incident, patch, dossier, base_sha)
        return {
            "mode": "patch-only",
            "incidentId": incident_id,
            "targetBranch": target_branch,
            "promotionReadyContent": content,
            "prUrl": None,
        }

    live = invoke(
        incident,
        patch,
        verdict,
        dossier,
        base_sha,
        target_branch=target_branch,
        pr_body=body,
    )

    if live.get("alreadyPromoted"):
        raise PromotionError(
            f"REFUSED (idempotence): incident {incident_id!r} has already been promoted "
            f"to {target_branch!r} - re-running promotion on an already-promoted incident "
            "is refused"
        )

    if live.get("ghFailed"):
        return {
            "mode": "draft-pr",
            "incidentId": incident_id,
            "targetBranch": target_branch,
            "branchPushed": bool(live.get("branchPushed", False)),
            "prUrl": None,
            "prStatus": "Bridge blocked",
            "prBodyFilePath": live.get("prBodyFilePath"),
            "rebaseFlag": live.get("rebaseFlag"),
        }

    return {
        "mode": "draft-pr",
        "incidentId": incident_id,
        "targetBranch": target_branch,
        "branchPushed": bool(live.get("branchPushed", True)),
        "prUrl": live.get("prUrl"),
        "prStatus": "created",
        "prBodyFilePath": None,
        "rebaseFlag": live.get("rebaseFlag"),
    }


# ── CLI (manual sanity check on canned data only) ───────────────────────────


def main(argv: list[str] | None = None) -> int:
    del argv

    canned_dossier = {
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
    body = assemble_pr_body(canned_dossier)
    print("PR BODY (canned sanity check):")
    print(f"  length = {len(body)} chars")
    print(f"  sections present = {sum(1 for _, h in DOSSIER_SECTIONS if h in body)}/12")

    validate_promotion_guards(
        {"approve": True, "reasons": ["clean"], "riskAssessment": "low"},
        branch_name_for("AR-2026-08-14-0001"),
        "draft-pr",
    )
    print("GUARDS (canned sanity check, approved + autorepair/AR-... + draft-pr): ACCEPTED")

    banner = rebase_flag({"conflict": True, "conflictFiles": ["a.qml"], "currentMasterSha": "abc123"})
    print(f"REBASE FLAG (canned conflict): {'NEEDS-REBASE' in (banner or '')}")
    print(f"REBASE FLAG (canned clean): {rebase_flag({'conflict': False})!r}")

    print("promote()'s live draft-pr half is DEFERRED - see default_invoke()'s own docstring.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
