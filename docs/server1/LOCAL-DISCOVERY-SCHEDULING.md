# Explicit local-root discovery scheduling

`LocalAddon::startIndexing` now schedules discovery instead of recursively enumerating every configured directory before returning. This is a native responsiveness change, not permission to discover additional roots. Automatic home-directory discovery, new providers, and network lookups are not introduced.

## Behavior retained

- Only the caller's explicit roots are considered, in caller order. Repeated roots remain repeated candidates. Already stored file paths are still skipped by indexing.
- Root symlinks and child symlinks are not traversed. Hidden child entries and the existing `node_modules`, `bin`, `src`, `build`, and `dist` exclusions remain excluded.
- Files through relative depth seven are eligible. The extensions remain `.torrent`, `.mp4`, `.mkv`, and `.avi`, case-insensitively.
- Discovery finishes before the first indexing callback. Indexing remains serial, and successful entries, not candidate paths or failed indexing attempts, count toward the existing 10,000-entry limit.
- Completion remains deferred and occurs once for a completed generation. Cancellation, replacement by a new scan, and owner destruction suppress the old completion. Late or duplicate provider callbacks cannot continue an obsolete generation.
- Index, lookup, and enrichment callables are retained across invocation so that synchronous owner destruction does not destroy an executing callable. The original callable identity is retained, including mutable value-captured state.

## Work budget and ordering

Each discovery turn performs at most 128 steps and checks a 4 ms elapsed-time budget between steps. Invalid roots, ignored files, empty-directory exits, directory entries, and ordered-child consumption all spend steps. Iterator filters deliberately include hidden entries and special entries: excluding them inside the iterator could make one iterator call skip an arbitrarily large number of entries without yielding.

An explicit depth-first stack replaces recursion. A directory's eligible children are inserted incrementally into a case-insensitive ordered buffer before traversal. This avoids both a bulk `entryInfoList` call and a bulk sort on the Qt application lane.

The old `entryInfoList(..., QDir::NoSort)` used the directory's default `Name | IgnoreCase` ordering. It did not mean filesystem order. The new comparator uses `QString::compare` with `Qt::CaseInsensitive`, matching [Qt's name comparator](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qdir.cpp#L238-L280). Distinct case-folded names preserve the previous filename order, including mixed files/directories and non-ASCII names.

There is one narrow ordering difference: names that compare equal ignoring case retain iterator order within that group. Qt's former [unstable sort](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qdir.cpp#L300-L314) did not specify an order for those ties. Tests compare exact old output when names have distinct folded values, and compare group order plus complete member/duplicate preservation for equal-fold names. This does not claim the old arbitrary tie permutation is retained.

## Limits

This bounds aggregate enumeration work between event-loop opportunities, not filesystem latency. A single directory-open, iterator, metadata, allocation, or ordered-buffer operation may take longer than the soft time budget. Cancellation invalidates a generation immediately; its next queued discovery turn does no further enumeration. Reclaiming an obsolete generation's buffers can still require linear destruction work.

Memory is not globally bounded: preserving discovery-before-indexing retains an `O(candidate-count)` path list, plus the eligible sibling buffers on the depth-first stack. The 10,000 accepted-entry limit is deliberately not a cap on discovered candidates. Filesystem contents are not snapshotted; changes between turns can change the discovered set. Queued children are rechecked before traversal so a directory replaced with a symlink is skipped. This is not an atomic filesystem sandbox and does not eliminate pre-existing filesystem races.

## Verification

`LOCAL-discovery-scheduling` uses disposable synthetic trees and a loopback-only HTTP fixture. It covers:

- HTTP and timer progress before indexing while scanning 8,192 ignored files, 8,192 hidden files, 8,192 empty directories, 8,192 invalid roots, and more than 10,000 eligible files;
- a marker created in a later root after four event-loop turns, proving traversal actually yields rather than only satisfying a wall-clock threshold;
- immediate and mid-discovery cancellation, restart, owner destruction, repeated/late provider callbacks, reentrant index/lookup/enrichment/completion teardown, and mutable sync/async provider state;
- exact previous-QDir ordering for distinct-fold names, Unicode/equal-fold groups, directory/file interleaving, repeated roots, exclusions, depth seven, and Unix symlink/cycle fixtures;
- 9,999 preloaded accepted entries followed by 10,000 failed candidates and then a successful candidate, verifying candidate enumeration is not cut off at the accepted-entry limit.

For negative controls, compile the same test with the pre-change `LocalAddon.cpp` and invoke `--responsiveness-only` and `--cancel-only`. The old implementation fails the later-root marker and mid-discovery cancellation checks respectively. The existing `LOCAL-lifecycle` and `LOCAL-differential` tests remain separate source-contract regression checks; the pinned source's discovery module M1083 does not authorize adopting its implicit roots or symlink-following behavior.
