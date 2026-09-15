#!/usr/bin/env python3
"""Verify P08-T5 upload contract prose and reject clause mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


CLAUSES = (
    "nonzero and increases monotonically within one open generation",
    "A later retry receives a new request identifier",
    "private live connection identity",
    "exact peer, block, and generation ownership",
    "on_request and on_cancel are intercepted and swallowed",
    "never enter libtorrent's default disk request queue",
    "interested and locally unchoked",
    "block offset must",
    "be an exact multiple of kWireBlockLength",
    "blockOrdinal * kWireBlockLength must",
    "equal offset, and length must be from 1 through kWireBlockLength",
    "A short final",
    "within the exact final-piece bounds",
    "advertised as committed",
    "Duplicate requests for one live connection identity and BlockSpan",
    "are suppressed only while that ownership remains live",
    "After terminalization",
    "a retry of the same block receives a new monotonic request identifier",
    "At most kMaxOutstandingUploadsPerPeer requests per peer",
    "kMaxOutstandingUploadsGlobal requests overall may be live",
    "admission caps count only live upload ownership records within one",
    "open transport generation. Both limits reset on generation replacement",
    "They do not bound queued payload bytes or native send-buffer backlog",
    "Only the current generation, after metadata readiness and within its exact",
    "piece count, may advertise a piece",
    "monotonic and idempotent within that generation",
    "replayed to every newly attached peer",
    "out-of-bounds, or stale-generation advertisement is",
    "rejected synchronously and increments uploadActionsRejected",
    "asserts an upstream commit; it does not read or verify storage",
    "payload size exactly equals the owned BlockSpan length",
    "submit() synchronously rejects a syntactically invalid response or abort",
    "one that does not match exact live ownership",
    "An accepted response or abort only enters the caller mailbox",
    "if it has become stale or invalid, uploadActionsRejected is",
    "incremented and no peer write is allowed",
    "uploadResponsesFramed once and adds",
    "exactly that payload length to uploadPayloadBytesFramed",
    "always terminalizes its upload locally and increments",
    "uploadRequestsAborted. No wire frame is required by this public contract",
    "exactly one successful response or abort terminalizes",
    "Remote cancellation, peer detach, generation replacement, and close",
    "exactly one UploadCancelObservation",
    "caller thread only enqueues a mailbox command",
    "network tick performs all peer-connection writes",
    "isCommitted before reading persistent bytes",
    "Circular-cache upload is forbidden",
    "uploadedBytes and uploadBytesPerSecond remain native libtorrent measurements",
    "must not be synthesized from upload actions or payload sizes",
    "ownedUploadsOutstanding is a current gauge",
    "uploadRequestsAccepted counts observations enqueued",
    "uploadRequestsRejected counts intercepted inbound requests denied before observation",
    "uploadResponsesFramed counts full piece frames handed to a live connection",
    "uploadPayloadBytesFramed counts only payload bytes copied into the native send buffer",
    "uploadRequestsCancelled counts cancel observations enqueued",
    "uploadRequestsAborted counts valid actions that terminalize locally",
    "uploadActionsRejected counts synchronous rejection of any upload action",
    "accepted response/abort mailbox actions rejected by network-tick revalidation",
    "before any peer write. It is distinct from inbound uploadRequestsRejected",
    "All seven upload counters are cumulative event counters",
)


def missing_clauses(text: str) -> list[str]:
    return [clause for clause in CLAUSES if clause not in text]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    text = args.header.read_text(encoding="utf-8")
    missing = missing_clauses(text)
    if missing:
        raise SystemExit("P08-T5 semantic contract missing: " + repr(missing))
    if args.mutations:
        for index, clause in enumerate(CLAUSES, start=1):
            mutated = text.replace(clause, "P08_T5_REMOVED_CLAUSE", 1)
            if not missing_clauses(mutated):
                raise SystemExit(f"P08-T5 semantic mutation {index} escaped")
            print(f"P08T5_SEMANTIC_{index:02d} REJECTED")
    print(f"P08-T5 semantic contract PASS clauses={len(CLAUSES)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
