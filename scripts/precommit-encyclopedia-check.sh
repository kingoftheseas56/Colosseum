#!/bin/sh
# precommit-encyclopedia-check.sh — warns (never blocks) when a commit touches a file
# covered by a docs/encyclopedia/ guide without that guide being kept current.
#
# WHY: docs/encyclopedia/ is gitignored — nothing on GitHub, no CI, ever sees it. The
# only enforcement point for "update the guide in the same change" (every guide's own
# header, and Colosseum/AGENTS.md) is local, at commit time. Silent drift already
# happened once (two shell.paths files drifted from unrelated work, unnoticed until a
# manual --check days later) — this hook exists so that never happens silently again.
#
# Reuses the real drift-detection tool (scripts/code_encyclopedia.py --check) rather
# than re-deriving "did the guide change" as a shell heuristic — that tool's state
# tracking correctly handles "guide prose updated but --accept never re-run", which a
# naive "was the .md file touched" check would miss.
#
# Installed via .git/hooks/pre-commit (not tracked — see that file's one-line shim).
# Never exits non-zero: this warns, it does not block. Escalate to a hard block once
# the team has lived with the warning for a while and it's proven not noisy.

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$REPO_ROOT" || exit 0
[ -d docs/encyclopedia ] || exit 0            # gitignored; absent on a fresh clone — no-op, not an error
command -v python >/dev/null 2>&1 || exit 0

STAGED=$(git diff --cached --name-only)
[ -z "$STAGED" ] && exit 0

WARNED=0
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
        if [ "$WARNED" = 0 ]; then
            printf '\n\033[33m⚠  encyclopedia drift on file(s) this commit touches:\033[0m\n'
        fi
        printf '%s\n' "$out" | grep "DRIFTED" | sed "s/^/    [$name] /"
        WARNED=1
    fi
done

if [ "$WARNED" = 1 ]; then
    printf '   If this changes how the subsystem works, update its guide (docs/encyclopedia/*.md)\n'
    printf '   in this commit, then re-run --accept (see the guide'"'"'s own footer). If not, this\n'
    printf '   is just a heads-up — commit is proceeding either way.\n\n'
fi

exit 0
