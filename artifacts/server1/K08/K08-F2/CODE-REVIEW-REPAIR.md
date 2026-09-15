# K08-F2 production code-review repair

The first production candidate detached failure ownership correctly but left the pre-existing explicit close/destructor path range-iterating `activeReads` across external `cancelRead()` calls. A source completing synchronously from cancellation erased the current iterator and the focused test crashed.

The repaired implementation centralizes ownership detachment. Failure, close, and destruction now snapshot active tokens and selection ownership, clear the reader's active/waiting/out-of-order/lock state, and mark the selection detached before any external cancellation or deselection. Reentrant completion therefore finds no live token and is inert.

The public contract also states the threading boundary: every public reader call, refresh callback, and source completion runs on one serialized owner lane. FileReader adds no internal cross-thread synchronization. K11 must marshal transport observations and store completions to that lane.

This is an additive repair after `fc739b27`; history was not rewritten. The unchanged K10 raw failure evidence remains preserved.
