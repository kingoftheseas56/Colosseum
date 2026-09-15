# K08-F2 contract review repair

The first candidate treated an empty failure reason as a no-op and compressed cancellation/disposal into ambiguous prose. The repaired contract instead terminalizes empty input with the exact fallback `file reader failed`.

It now requires this reader's ownership state to detach before any external `cancelRead()` or `deselect()` call, exact-once cancellation of detached active reads, disposal of waiting and out-of-order not-yet-ready work, preservation of the already-ready queue, rejection of synchronous reentrant and late delivery, and isolation from sibling readers sharing the same source or scheduler.

This repair changes the public header and evidence only. `FileReader.cpp` remains unchanged pending independent contract approval.
