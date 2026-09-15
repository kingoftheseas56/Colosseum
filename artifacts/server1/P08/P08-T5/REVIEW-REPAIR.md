# P08-T5 review repair

This additive repair supersedes only the ambiguous prose and statistics at `92ba8a97`; it does not rewrite that commit.

- `UploadAbortAction` terminalizes a valid live upload locally. The contract requires no reject frame and no longer exposes `uploadAbortsFramed`.
- `uploadActionsRejected` counts both synchronous syntax/ownership rejection and accepted mailbox response/abort actions that become stale or invalid before network-tick dispatch. Neither may write to a peer.
- `uploadPayloadBytesFramed` counts only payload bytes copied into the native send buffer. Native libtorrent `uploadedBytes` and `uploadBytesPerSecond` remain independent.
- The 4-per-peer and 20-global caps count only live admitted ownership records within one open transport generation and reset on replacement. They do not bound queued payload or native send-buffer memory.
- Duplicate suppression ends with terminalization. A later same-block retry gets a new nonzero monotonic identifier.
- Upload alignment is exact: offset is a multiple of 16 KiB, ordinal equals offset divided by 16 KiB, length is 1 through 16 KiB, and a short tail must remain within final-piece bounds.

PeerPlugin and LibTorrent2Adapter remain untouched. Live wire behavior is still pending the separately reviewed implementation packet.
