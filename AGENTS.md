# Colosseum — agent notes

## The code encyclopedia — read before touching an unfamiliar subsystem

`docs/encyclopedia/` holds Colosseum's hand-written subsystem guides: what a piece of the
app is for, its flow, its traps, how to test it. Reading the right one first is measured to
roughly halve a cold agent's search time and wrong turns.

**Before exploring or changing an unfamiliar subsystem:** `ls docs/encyclopedia/*.md` and
read whichever guide covers it.

**`docs/encyclopedia/` is tracked in git** and ships with the repo, so it is present on
every clone and visible to any reader (GitHub browse, a fetch-only tool, a new agent). It is
*not* CI-enforced — the only enforcement point for "update the guide in the same change"
(every guide's own header, and this file) is the local pre-commit hook below.

**After changing how a subsystem works:** update its guide in the same change — a stale
guide is worse than none. A local pre-commit hook (`scripts/precommit-encyclopedia-check.sh`)
**blocks** a commit when it touches a covered file whose description has drifted. It tells
you the two ways to clear it: update the guide + re-accept, or (if the change left the
guide's claims intact) re-accept just to refresh the fingerprint. Genuine emergencies can
bypass with `git commit --no-verify`. Heed the block, don't reflexively bypass it.

**Fresh-clone setup — re-install the hook (one line).** Git never tracks `.git/hooks/`, so
the hook must be recreated on each new clone before it protects you. On any fresh checkout:
```sh
echo 'exec sh "$(git rev-parse --show-toplevel)/scripts/precommit-encyclopedia-check.sh"' > .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit
```

No guide exists yet for what you're touching? `docs/encyclopedia/downloads.md` is a recent
worked example of drafting one from scratch, including catching and correcting an
inaccuracy before landing it.
