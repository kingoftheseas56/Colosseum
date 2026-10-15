#!/bin/sh
# precommit-encyclopedia-check.sh — BLOCKS a commit when it touches a file covered by a
# docs/encyclopedia/ guide whose description has drifted (changed since it was accepted).
#
# WHY: the encyclopedia's value collapses to zero the moment a guide describes a subsystem
# that no longer matches the code. Silent drift already happened once (shell.paths files
# drifted from unrelated work, unnoticed until a manual --check days later) — a warning that
# never blocked was the original design, but a warning that ships the drift anyway doesn't
# actually protect the map. This hook now fails the commit when drift is real.
#
# HOW TO CLEAR THE BLOCK — two legitimate paths:
#   1. The change DID affect the subsystem: update the guide (docs/encyclopedia/<name>.md)
#      in THIS commit, then re-snapshot the drifted file:
#        python scripts/code_encyclopedia.py --paths docs/encyclopedia/<name>.paths \
#          --output docs/encyclopedia/<name>-index.md \
#          --state docs/encyclopedia/<name>-state.json --accept <drifted-file>
#      then re-commit. This is the path that keeps the map honest.
#   2. The change did NOT affect anything the guide claims (a stale hash from work that
#      left the identifying description untouched — common, and safe): re-accept as above
#      WITHOUT a guide edit, then re-commit. Verify by reading the file's top-of-file
#      comment against the accepted_comment in its *-state.json; if they match, it's safe.
#   Genuine emergency only: `git commit --no-verify` bypasses the gate — use sparingly and
#   follow up with a --accept so the next commit isn't blocked on the same drift.
#
# Reuses the real drift-detection tool (scripts/code_encyclopedia.py --check) rather
# than re-deriving "did the guide change" as a shell heuristic — that tool's state
# tracking correctly handles "guide prose updated but --accept never re-run", which a
# naive "was the .md file touched" check would miss.
#
# docs/encyclopedia/ is now tracked in git, so it is present on every clone. The hook
# itself (.git/hooks/pre-commit) is NOT tracked — git never tracks hooks — so it must be
# re-installed on each fresh clone. See Colosseum/AGENTS.md for the one-line install.

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$REPO_ROOT" || exit 0
command -v python >/dev/null 2>&1 || exit 0

# --no-renames: without it, git's default rename detection collapses a renamed watched
# file into just its NEW name, which is never in any manifest — so a plain --name-only
# diff would silently miss that an old, still-watched path just disappeared. Forcing the
# old-path/new-path pair keeps a rename triggering the same as a straight deletion (the
# old path still intersects the manifest, and the checker then fails closed on it being
# gone from disk) for both the encyclopedia loop and the Lanista coverage dispatch below.
STAGED=$(git diff --cached --name-only --no-renames)
[ -z "$STAGED" ] && exit 0

BLOCKED=0

# docs/encyclopedia is wrapped in its own guard (rather than an early `exit 0` for the
# whole script) so that a clone missing it still runs the independent Lanista coverage
# check below — the two subsystems' no-op conditions must not be coupled to each other.
if [ -d docs/encyclopedia ]; then
    ENCY_BLOCKED=0
    for pathsfile in docs/encyclopedia/*.paths; do
        [ -f "$pathsfile" ] || continue
        name=$(basename "$pathsfile" .paths)

        # Only bother checking manifests this commit actually touches a file from.
        overlap=$(comm -12 \
            <(grep -v '^#' "$pathsfile" | grep -v '^\s*$' | sort) \
            <(printf '%s\n' "$STAGED" | sort))
        [ -z "$overlap" ] && continue

        out=$(python scripts/code_encyclopedia.py \
            --paths "$pathsfile" \
            --output "docs/encyclopedia/${name}-index.md" \
            --state "docs/encyclopedia/${name}-state.json" \
            --check 2>&1)

        if printf '%s\n' "$out" | grep -q "DRIFTED"; then
            if [ "$ENCY_BLOCKED" = 0 ]; then
                printf '\n\033[31m encyclopedia drift on file(s) this commit touches:\033[0m\n'
            fi
            printf '%s\n' "$out" | grep "DRIFTED" | sed "s/^/    [$name] /"
            ENCY_BLOCKED=1
        fi
    done

    if [ "$ENCY_BLOCKED" = 1 ]; then
        printf '\n   Commit BLOCKED. Either update docs/encyclopedia/<name>.md and re-accept the file,\n'
        printf '   or, if the guide'"'"'s claims still hold, re-accept just to refresh the hash:\n'
        printf '     python scripts/code_encyclopedia.py --paths docs/encyclopedia/<name>.paths \\\n'
        printf '       --output docs/encyclopedia/<name>-index.md \\\n'
        printf '       --state docs/encyclopedia/<name>-state.json --accept <drifted-file>\n'
        printf '   Then re-commit. Emergency bypass: git commit --no-verify.\n\n'
        BLOCKED=1
    fi
fi

# ── Lanista coverage ledger drift — Slice 2 of
#    docs/superpowers/plans/2026-08-13-colosseum-lanista-coverage-ledger-plan.md (D4).
#
# WHY THIS SHAPE: D4's trigger is an exact staged-path intersection, never a `*.qml` or
# directory glob — a broad gate gets bypassed, and this system exists to protect a
# narrow, already-reviewed fact ("is this exact surface still drivable?"), not to
# rediscover surfaces. Everything that does not intersect an accepted family's exact
# `<family>.paths` manifest — or the ledger/state files themselves — must stay a no-op,
# same as a clone with no docs/lanista-coverage/ at all.
#
# Once triggered, the check itself is deliberately NOT scoped back down to only the
# triggering family: scripts/lanista_coverage.py --check has no per-family filter, and
# its own Slice-1 header says the *.paths auto-discovery exists precisely "so a future
# staged-path-intersection hook (Slice 2) can dispatch across every accepted family in
# one pass." A single shared accepted-state.json backs every family, so once the gate
# is legitimately open it protects the whole accepted ledger, not just the file(s) that
# opened it — same fail-closed spirit as a corrupted accepted-state.json blocking every
# mode. If that ever surfaces drift in a family this commit did not touch, the DRIFTED
# line names that family explicitly and the same two clear paths (re-accept, or
# `git commit --no-verify` with a reason) apply — exactly like an unrelated encyclopedia
# guide blocking on drift you didn't cause.
if [ -d docs/lanista-coverage ]; then
    COVERAGE_TRIGGERED=0
    if printf '%s\n' "$STAGED" | grep -Fxq "docs/lanista-coverage/ledger.json"; then
        COVERAGE_TRIGGERED=1
    fi
    if printf '%s\n' "$STAGED" | grep -Fxq "docs/lanista-coverage/accepted-state.json"; then
        COVERAGE_TRIGGERED=1
    fi

    for pathsfile in docs/lanista-coverage/*.paths; do
        [ -f "$pathsfile" ] || continue

        # Trigger 2 (part): the family's own manifest file staged.
        if printf '%s\n' "$STAGED" | grep -Fxq "$pathsfile"; then
            COVERAGE_TRIGGERED=1
        fi
        # Trigger 1: a staged path intersects the manifest's listed dependencies.
        overlap=$(comm -12 \
            <(grep -v '^#' "$pathsfile" | grep -v '^\s*$' | sort) \
            <(printf '%s\n' "$STAGED" | sort))
        [ -n "$overlap" ] && COVERAGE_TRIGGERED=1
    done

    if [ "$COVERAGE_TRIGGERED" = 1 ]; then
        coverage_out=$(python scripts/lanista_coverage.py --check 2>&1)
        coverage_rc=$?

        if [ "$coverage_rc" = 2 ]; then
            # Operational/schema failure — the checker could not even compare
            # accepted-vs-current. Fail closed (matches a deleted/renamed watched file,
            # which surfaces the same way: "missing watched source").
            printf '\n\033[31m Lanista coverage ledger check could not run:\033[0m\n'
            printf '%s\n' "$coverage_out" | sed 's/^/    /'
            if printf '%s\n' "$coverage_out" | grep -q "acceptance state"; then
                printf '\n   docs/lanista-coverage/accepted-state.json looks corrupted or hand-edited.\n'
                printf '   --accept/--accept-all-drifted CANNOT self-heal this file — load_state() runs\n'
                printf '   before mode branching. Recovery: delete\n'
                printf '   docs/lanista-coverage/accepted-state.json, then run\n'
                printf '     python scripts/lanista_coverage.py --accept-all-drifted --accepted-by "<name>"\n'
                printf '   to rebuild it from the current ledger, and review the rebuilt acceptance\n'
                printf '   before committing.\n'
            else
                printf '\n   This is a schema error in ledger.json or a docs/lanista-coverage/*.paths\n'
                printf '   manifest (or a watched/evidence file is missing or unreadable). Fix it\n'
                printf '   directly, then re-run the check.\n'
            fi
            printf '   Emergency bypass: git commit --no-verify.\n\n'
            BLOCKED=1
        elif printf '%s\n' "$coverage_out" | grep -q "^DRIFTED "; then
            printf '\n\033[31m Lanista coverage drift (this commit touched a watched family):\033[0m\n'
            printf '%s\n' "$coverage_out" | grep "^DRIFTED " | sed 's/^/    /'
            printf '\n   NOTE: drift is reported for the WHOLE family — every file in its\n'
            printf '   docs/lanista-coverage/<family>.paths manifest — not only the file(s) you\n'
            printf '   staged, and possibly a family this commit did not intend to touch at all.\n'
            printf '   Update the ledger claim and accept, or re-accept if the claim still holds:\n'
            printf '     python scripts/lanista_coverage.py --accept <family> --accepted-by "<name>"\n'
            printf '   Then re-commit. Emergency bypass: git commit --no-verify.\n\n'
            BLOCKED=1
        elif [ "$coverage_rc" != 0 ]; then
            # Non-zero exit with no recognizable DRIFTED output at all — an unexpected
            # failure mode. Fail closed rather than silently treating it as clean.
            printf '\n\033[31m Lanista coverage check exited unexpectedly (code %s):\033[0m\n' "$coverage_rc"
            printf '%s\n' "$coverage_out" | sed 's/^/    /'
            printf '   Emergency bypass: git commit --no-verify.\n\n'
            BLOCKED=1
        fi
    fi
fi

if [ "$BLOCKED" = 1 ]; then
    exit 1
fi

exit 0
