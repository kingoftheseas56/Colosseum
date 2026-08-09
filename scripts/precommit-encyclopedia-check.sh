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
[ -d docs/encyclopedia ] || exit 0            # not every clone has the encyclopedia yet — no-op, not an error
command -v python >/dev/null 2>&1 || exit 0

STAGED=$(git diff --cached --name-only)
[ -z "$STAGED" ] && exit 0

BLOCKED=0
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
        if [ "$BLOCKED" = 0 ]; then
            printf '\n\033[31m encyclopedia drift on file(s) this commit touches:\033[0m\n'
        fi
        printf '%s\n' "$out" | grep "DRIFTED" | sed "s/^/    [$name] /"
        BLOCKED=1
    fi
done

if [ "$BLOCKED" = 1 ]; then
    printf '\n   Commit BLOCKED. Either update docs/encyclopedia/<name>.md and re-accept the file,\n'
    printf '   or, if the guide'"'"'s claims still hold, re-accept just to refresh the hash:\n'
    printf '     python scripts/code_encyclopedia.py --paths docs/encyclopedia/<name>.paths \\\n'
    printf '       --output docs/encyclopedia/<name>-index.md \\\n'
    printf '       --state docs/encyclopedia/<name>-state.json --accept <drifted-file>\n'
    printf '   Then re-commit. Emergency bypass: git commit --no-verify.\n\n'
    exit 1
fi

exit 0
