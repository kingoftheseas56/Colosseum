# Prototype-phase acceptance and negative controls

**Not executed. No prototype or product-test code was written by this audit.** These are decisive fixtures for a separately authorized phase, mapped to the source comparison. Use controlled local HTTP/media/file fixtures and fresh test profiles; do not use real credentials, provider accounts or user downloads. Preserve Windows versus Linux results independently. A boundary double tests policy; only the real adapter tests engine behavior.

## Rules for recording a result

Record exact application/engine revisions, platform, build/API availability, fixture identity, source space/profile/document/context, steps, observed terminal and evidence. Separate policy-double, real-Qt, real-WebView2 and donor-test-source evidence. CEF is a reference here, not a required new runtime. 'Unsupported' can truthfully pass an honesty assertion while remaining a product capability gap; never call it functional parity.

For each negative control, deliberately break only the named fence in an isolated test variant. It must fail the same assertion that the correct version passes. Restore the fence and rerun. Do not count an untriggered negative control as evidence. A timeout does not prove successful close/cancel/resume or metadata consumption.

## Category 16: owned playback

| ID | Controlled scenario and required result | Decisive negative control |
|---|---|---|
| F16.1 | Two elements: one playing, one user-paused. Minimize/world-away then return resumes only the actually app-paused element. Ordinary inactive space within displayed app retains agreed playback. | Resume every paused element or treat inactive-tab visibility as app-away. |
| F16.2 | Overlapping app/world/window reasons; removing one reason does not resume while another remains. Explicit pause/play or page interaction while away revokes prior resume ownership. | Use only the latest reason or omit revocation for pause() on an already-paused element. |
| F16.3 | Same URL in a new document; source/element replacement; same-origin iframe removal/navigation/replacement; host move. Old ACK or receipt cannot resume a replacement or retired element/frame. | Replace object/document binding with URL-only lookup; accept a stale operation/host response. |
| F16.4 | Cross-origin, closed-shadow, late-added frame/root, WebAudio/provider-specific media and autoplay refusal produce accurate limited/unsupported/blocked observations; no universal control claim. | Count uninspected media or rejected play Promise as successful resume. |
| F16.5 | Pending native consent/close/download admission and observable licence/request blockers keep the page active when freezing cannot be shown safe. No discard-as-pause. | Ignore blockers or report frozen as proof media was app-paused. |

## Category 17: source-bound download admission

| ID | Controlled scenario and required result | Decisive negative control |
|---|---|---|
| F17.1 | Concurrent downloads from spaces A/B and an auxiliary window sharing a profile produce exactly one native admission each, correctly attributed despite selection changes. Same native ID under a new profile cannot reuse an old decision. | Add a per-window listener, use active space at completion, or key solely by native numeric ID. |
| F17.2 | Explicit default-auto-save policy versus ask/destination policy. Distinct one-use destination decision and metadata grant bind the same transfer; destination cancel writes no file. Replayed/expired response cannot answer a newer request. | Treat possession of a history grant as file/destination consent or reuse a response token. |
| F17.3 | Originless request or source page closes before Qt's queued public event; source view expires during consent. Establish a safe supported rejection/lifetime path or mark the case blocked. No native-safe claim from a JS null-view double. | Bypass lifetime qualification and unconditionally read the raw-backed view getter. Use crash-isolated qualification; do not exercise unsafe races in production. |
| F17.4 | Hold normal Requested download for delayed QML destination UI, then accept/cancel/profile-retire; native object remains valid only under documented/pinned ownership. | Drop retained ownership or answer a request after profile destruction. |
| F17.5 | Suggested/default names and caller overrides: separators, ../, absolute/UNC-like names, reserved names, collisions, unwritable destination and explicit overwrite. Chosen path remains admitted and native failures are visible. | Pass arbitrary caller filename/path as authorization or equate unique-name selection with atomic overwrite consent. |

## Category 18: truthful state and safe Open

| ID | Controlled scenario and required result | Decisive negative control |
|---|---|---|
| F18.1 | Unknown total, zero-byte successful file, changing totals, and received==total before terminal. State and progress remain accurate. | Treat zero/ratio as completion or divide by an unknown total. |
| F18.2 | Interrupted with isFinished false/true, server rejecting Range, repeated resume, request loss. Offer only supported attempts and report actual result; no guaranteed CanResume invented. | Enable Resume solely from Interrupted or synthesize success when resume() returns void. |
| F18.3 | Cancel before/after completion, duplicate callbacks and Interrupted cancel followed by late native update. Distinguish Qt wrapper Cancelled/isFinished from underlying item/partial-file cleanup; exactly one truthful app outcome. | Assume every wrapper cancel reached native cancelDownload, or manufacture Completed after cancel. |
| F18.4 | Final native update changes state to Completed and changes received/total in that same update. Coalesced history captures final numbers exactly once after properties settle. | Commit immediately on first stateChanged and consume the grant before final byte signals; this must expose the source-derived current ordering risk. |
| F18.5 | Completed live/history identity with missing, replaced, unauthorized or wrong-account file; cancelled descriptor; folder versus shell-select action. No arbitrary path opens, truthful availability is shown. | Open a raw descriptor/caller string or rely on CanOpenDownload/old metadata alone. |

## Category 19: transfer/history/file separation

| ID | Controlled scenario and required result | Decisive negative control |
|---|---|---|
| F19.1 | Accepted ordinary network download survives source-tab close/navigation while original profile remains, with original attribution and eventual one-use history write. | Tie accepted transfer validity to current selected page/document generation. |
| F19.2 | Close last private space while its valid download/request remains. Retain necessary private profile until the chosen terminal/retirement policy settles; never write ordinary history. Saved private file remains unless separately deleted. | Retire on view count alone or claim off-record guarantees file deletion. |
| F19.3 | Account A→B/profile replacement during admission and completion. A's request cannot write B metadata, answer B UI or become a B transfer. Native shutdown/callback retirement is observed, not invented. | Retain grants across storage-path change or retarget to current account. |
| F19.4 | Atomic metadata write fails once, retry succeeds using same still-valid grant, synchronous changed-signal reentry and duplicate completion. Old state survives; exactly one final row. | Consume grant before successful commit or remove committing/replay guard. |
| F19.5 | Revoked grant, inadmissible auth/blob/data source and malformed descriptor are distinguished from transient write failure. File can be saved while history is intentionally unavailable. | Show every false result as retryable disk failure forever or loosen URL privacy policy just to make retry succeed. |
| F19.6 | Restart with completed metadata but deleted/moved file and no live request. Show history/file availability honestly; do not claim resume from metadata. Clearing history leaves saved files untouched. | Reconstruct a fictitious native transfer from a path/row or delete user files as metadata cleanup. |

## Category 20: native Save

| ID | Controlled scenario and required result | Decisive negative control |
|---|---|---|
| F20.1 | SavePage action stays Requested during destination decision; explicit-path save is invoked only after prior consent. No-listener explicit save behavior is documented/contained. | Call explicit-path save before asking permission and assume a later request UI can prevent writing. |
| F20.2 | Context A, then context B/navigation/host move/space-selection change before response. Reject stale A or dispatch valid A on original view only; never use B/current selection. | Keep copied QML snapshot but trigger the mutable current native context without serial check. |
| F20.3 | Link/image/media capabilities including MediaCanSave=false, canvas/empty media URL, hostile schemes and filenames. Public capability gating is explicit; menu presence alone is insufficient. | Trust default Quick menu/action.enabled as a complete media-save permission check. |
| F20.4 | Authenticated subframe resource/referrer, sandbox, opaque/blob context and replaced frame. Prove original context fidelity where supported; otherwise visibly report unsupported. | Invent originating frame identity or replace native context Save with main-frame StartDownload. |
| F20.5 | Two page saves before first target selection; request IDs/destinations remain correct under supported serialization. SingleHTML, CompleteHTML and MHTML roundtrip with actual outputs. | Overwrite the single SavePageInfo or attribute both saves to the latest destination. |
| F20.6 | Source page navigates or closes during serialization: retain required document dependency or report native cancel/failure, never promise resource-download survival. Page Save and existing Print/PDF tracked separately, with actual build/API capability. Protected stream/MSE/DRM export is not promised. | Navigate/close the source during serialization and treat acceptance/profile retention as guaranteed completion; also record PDF print as native download success or infer provider export from menu presence. |

## Prioritized later qualification

First exercise F18.4, F17.3/F17.4, F18.3, F19.2/F19.4 and F20.1/F20.2: these distinguish source-inferred ordering/lifetime hazards from unsupported assumptions. Preserve already-correct category16 and ordinary tab-close semantics while adding their regression cases. Then expand the remaining bounded acceptance matrix. This ordering is a proposed plan, not a request to run it during this audit.

The existing old-account boundary test is a source-inferred mismatch: it clears grants on the same mock store and expects an emptied QML token, while current retry handling retains the token. Reconcile its intended scenario with real account retirement; do not count it as an executed failure or proof of leakage.
