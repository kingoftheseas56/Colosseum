# Colosseum — agent notes

## The code encyclopedia — read before touching an unfamiliar subsystem

`docs/encyclopedia/` holds Colosseum's hand-written subsystem guides: what a piece of the
app is for, its flow, its traps, how to test it. Reading the right one first is measured to
roughly halve a cold agent's search time and wrong turns.

**Before exploring or changing an unfamiliar subsystem:** `ls docs/encyclopedia/*.md` and
read whichever guide covers it.

**`docs/encyclopedia/` is gitignored — real and current locally, invisible on GitHub.** A
fetch-only reader (a GitHub browse, a tool with no local disk) sees nothing there and
cannot conclude the subsystem is undocumented from that absence.

**After changing how a subsystem works:** update its guide in the same change — a stale
guide is worse than none. A local pre-commit hook (`scripts/precommit-encyclopedia-check.sh`)
warns when a commit touches a covered file without its guide — heed it, don't silence it.

No guide exists yet for what you're touching? `docs/encyclopedia/downloads.md` is a recent
worked example of drafting one from scratch, including catching and correcting an
inaccuracy before landing it.
