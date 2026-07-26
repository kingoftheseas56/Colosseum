# Colosseum Downloads Page Repair — Design

**Status:** design approved, written-spec review pending

**Date:** 2026-07-26

**Author:** Agent 0 (Codex), foundation

**Source audit:** Brotherhood `agents/audit-downloads-ux-2026-07-26.md`

**Approved by Hemanth:** 2026-07-26

---

## 1. Outcome

Downloads becomes an honest, safe recovery surface for all five media lanes:
manga, comics, books, audiobooks, and video.

The page must never advertise an action its owning backend cannot perform, must
not destroy a local copy under vague wording, and must be fully usable with a
keyboard or remote. Audiobooks become first-class participants in the manager
and totals instead of a separate visual appendix.

This pass closes all three P0 findings, all fifteen P1 findings, and the
Downloads-local P2 findings from the audit. Shared Theme, backdrop, and
ScrollGlide changes are deliberately deferred because their blast radius is
26–74 QML surfaces.

## 2. Ground truth

Verified against `master` on 2026-07-26. The current
`qml/DownloadsPage.qml` blob is identical to the audited blob
`a5b16ce67bad041b2436378841db2415ee3cec87`.

`LocalDownloads` composes over four downloader families plus the volume service:

- `MangaDownloader`
- `MangaTankobanService`
- `ComicDownloader`
- `BookDownloader`
- `DownloadStore` for Theatre

`AudiobookDownloader` is exposed separately as `Audiobooks`, even though the
page presents it as part of the same vault.

Only Theatre's `DownloadStore` currently owns real retry, pause, and resume
operations. Manga, comic, book, and audiobook downloaders discard or do not
retain enough of a failed request to promise a correct retry. A blind second
invocation is not an acceptable substitute.

Completed audiobooks are consumed through their paired book reader's Audio
surface. The retired standalone audiobook player must not be resurrected.

## 3. Scope

### In scope

- Capability-driven active-job and completed-item schemas.
- Audiobooks in the unified read model, manager, global totals, and storage
  totals.
- Honest state copy and failure reasons for every lane.
- Confirmation and result handling for destructive actions.
- Keyboard, remote, focus, accessibility, tooltips, and row context menus.
- State preservation across closing and reopening Downloads.
- Separating active-progress refresh from settled-library refresh.
- Correct byte-weighted progress and explicit unknown-length progress.
- Downloads-local rendering and capacity repairs.
- Automated model/contract tests and an eyes-on smoke checklist.

### Out of scope

- Adding retry support to downloader engines that do not preserve restart
  payloads.
- Adding pause/resume to non-Theatre engines.
- Replacing the shared per-page Theme pattern.
- Reworking the shared backdrop/`ShaderEffectSource` convention.
- Reworking shared `ScrollGlide` behavior.
- Changes inside `PlayerPage.qml` or `MangaReader.qml`.
- A new standalone audiobook player.
- A global design-system rewrite.

## 4. Architecture

### 4.1 One read model, different capabilities

`LocalDownloads` becomes the single Downloads-page facade and receives the
existing `AudiobookDownloader` instance. This does not pretend the engines are
the same. It normalizes identity and presentation while preserving backend
differences as explicit capabilities.

Every active job exposes:

```text
world, owner, id, groupKey, kind
title, subtitle, creator, art
state, stateLabel, error
received, total, hasKnownTotal, ratio
speed, etaSec
canPlay, canRetry, canPause, canResume, canCancel, canDismiss
```

Every completed item exposes:

```text
world, owner, id, seriesKey, seriesId, kind
title, subtitle, creator, art, path
bytes, addedAt, missing
canOpen, canDelete, unavailableReason
```

The page renders actions only from capability flags. It must not infer
capability merely because a state happens to be named `failed`, `running`, or
`done`.

### 4.2 State vocabulary

Canonical presentation states are:

- `queued` — accepted but not transferring.
- `resolving` — finding files, metadata, or a stream endpoint.
- `downloading` — bytes or pages are actively arriving.
- `paused` — only when the owner can resume the same job.
- `retrying` — Theatre retry accepted and reconnecting.
- `failed` — terminal failure with a non-empty reason.
- `cancelling` — cancellation accepted but not yet confirmed.
- `deleting` — local deletion accepted but not yet confirmed.
- `done` — landed and available locally.
- `unavailable` — indexed, but the local payload cannot be opened.

`failed` is terminal and is not counted as active. The summary may say
"2 active · 1 needs attention"; it may not promise that failed jobs "will
land."

Unknown totals remain unknown:

- `hasKnownTotal == false`
- no numeric percentage
- no `0 B of 0 B`
- indeterminate progress treatment and copy such as `Downloading`

### 4.3 Revision channels

The facade exposes separate change channels:

- `activeRevision` for job state/progress.
- `libraryRevision` for completed items, deletion, and availability.
- `summaryRevision` for totals and attention counts.

The existing periodic pulse may refresh active progress only. It must not
rebuild settled series, ledgers, audiobook history, or lane totals every 400 ms.
Backend completion/deletion signals advance the library and summary channels.

### 4.4 Mutation result contract

Page mutations are requests with visible lifecycle, not fire-and-forget calls.
The facade exposes a request identifier and emits a terminal result:

```text
operationFinished(requestId, operation, world, id, success, message)
```

The page tracks pending requests by `world + id + operation`. While pending:

- the triggering control is disabled;
- its label becomes `Cancelling…`, `Deleting…`, or `Retrying…`;
- duplicate clicks/keypresses are ignored.

On success, the relevant model refreshes and a short status announcement is
shown. On failure, the row remains and the returned error is displayed. A
filesystem deletion that leaves the payload present is failure, even if an
index mutation was attempted.

Backend methods may need truthful boolean/result signals added where their
current `void` API cannot distinguish success from failure. Silent optimistic
success is not allowed.

## 5. Actions and destructive language

### 5.1 Active jobs

| Owner | Play | Retry | Pause/Resume | Cancel |
|---|---:|---:|---:|---:|
| Theatre | When a usable URL exists | Failed jobs | Supported states | Supported states |
| Manga | No | No | No | Supported states |
| Tankoban volume | No | No unless the service proves retained payload | No | Supported states |
| Comic | No | No | No | Supported states |
| Book | No | No | No | Supported states |
| Audiobook | No live playback | No | No | Queued/resolving/downloading |

Non-Theatre failures receive `Return to Tankoban`, `Return to Biblio`, or the
corresponding source route. The actual failure reason remains visible. This is
not labeled Retry.

Failed entries may expose `Dismiss` only when dismissal removes the failure
record and no local media. Dismiss and Delete are separate operations.

### 5.2 Completed items

`Remove` is forbidden. The action is labeled `Delete download` or
`Delete local copy`.

Confirmation includes:

- exact title;
- number of items for group operations;
- that local files will be deleted;
- whether reading/watch progress remains;
- explicit `Cancel` and destructive `Delete` buttons.

There is no auto-confirm, timeout confirmation, or click-through backdrop.

### 5.3 Group cancellation

Cancelling a group or season requires a confirmation that states the number of
jobs and warns when partial data will be discarded. Only rows that report
`canCancel` are included. The operation result reports partial failure rather
than claiming that the whole group was cancelled.

### 5.4 Audiobook actions

- Active audiobook rows retain the downloader's title and author.
- Failed rows retain and display the emitted reason until dismissed.
- Completed rows use `Open book` and route to the paired book reader's Audio
  surface.
- If the pairing cannot be resolved, `Open book` is absent and an
  `unavailableReason` explains what is missing.
- `Delete local copy` uses the same confirmation/result contract as every other
  lane.
- The empty-lane CTA opens Biblio.

## 6. Page structure and interaction

The visual hierarchy remains recognizable:

1. Chrome and page title.
2. Unified "Now arriving" manager when active or failed jobs exist.
3. Global vault summary.
4. Five settled-media lanes.
5. Expanded ledger for the selected series.

Audiobooks are not placed in a detached special section. They are a normal
lane with the same card, empty, availability, and deletion semantics.

### 6.1 Focus topology

Downloads is a `FocusScope` with one remembered focus target.

- `Tab` / `Shift+Tab`: traverse actionable controls in reading order.
- Left/Right: move within a toolbar, action cluster, or horizontal lane.
- Up/Down: move between manager rows, lanes, ledger rows, and major sections.
- Enter/Space: activate the focused action.
- Menu key or the platform context-menu shortcut: open row actions.
- Escape: close context menu, close confirmation, collapse the current ledger,
  then leave Downloads.

On opening, focus returns to the last valid target. If that item no longer
exists, it falls back to the nearest surviving section and then the Back
button.

Every actionable item has:

- visible focus treatment;
- an accessible name and role;
- a tooltip for icon-only actions;
- at least a 40 px visual target and 44 px effective hit target where layout
  permits.

### 6.2 Context menu

Each active and completed row has one keyboard-accessible context menu built
from the same capability list as inline actions. Inline and menu actions call
the same functions; the menu cannot expose a hidden no-op branch.

### 6.3 State preservation

Closing Downloads captures:

- vertical scroll position;
- open manager groups;
- selected lane/ledger;
- open ledger seasons;
- focused semantic item key.

The Loader may still unload the page. State lives in `Main.qml` as a compact
snapshot and is restored on the next `onLoaded`. This avoids a hidden page
continuing to process progress ticks while preserving the user's place.

Transient dialogs, context menus, and pending mutations are not restored.

## 7. Progress and totals

Group progress is byte-weighted when children expose known byte totals:

```text
sum(received) / sum(total)
```

Children with unknown totals do not contribute a guessed denominator. If all
active children are unknown, the group is indeterminate. Page/count-based manga
progress is labeled in pages and is not mixed into byte arithmetic.

Global totals include completed audiobooks:

- item count;
- per-lane count;
- known local bytes;
- active count excluding failures;
- attention count for failed/unavailable entries.

Copy distinguishes these values. For example:

```text
3 arriving · 2 need attention · 48 local items · 31.4 GB
```

## 8. Capacity and Downloads-local polish

- Replace large vertical `Repeater` collections with `ListView` so off-screen
  rows are not all instantiated.
- Horizontal lanes keep their shelf behavior but use a virtualized horizontal
  `ListView`, not an unbounded `Repeater`, and gain visible previous/next
  affordances whenever content overflows.
- Small fixed collections may remain declarative, but no collection whose size
  grows with jobs, series, seasons, or downloaded items may use an unbounded
  `Repeater`.
- Cap the content column to a readable maximum width and center it on ultrawide
  displays.
- Set `sourceSize` for page-owned network/local images using their rendered
  bounds and device pixel ratio.
- Replace Unicode transport/window glyphs owned by this page with the same
  Lucide SVG assets used by the shipped surfaces.
- Replace page-owned hardcoded gold values with the existing accent token.
- Page-local fold animations use the available reduced-motion preference; if
  no application preference exists, this pass adds no new global setting and
  keeps the transition short and non-essential.

The shared Theme object, backdrop capture, and ScrollGlide implementation are
untouched.

## 9. Failure and unavailable states

The facade distinguishes:

- genuinely empty lane;
- backend/read-model unavailable;
- indexed entry whose files are missing;
- failed active job;
- failed mutation.

An unavailable lane or read failure must never render the same CTA as an empty
library. It renders a concise explanation and a retry-refresh action only if
refresh is a real operation.

No raw filesystem path, URL query secret, or unbounded backend error is placed
directly into the UI. User-facing errors are bounded and sanitized while the
full diagnostic remains in logging.

## 10. Files expected to change

The implementation plan will confirm exact paths, but the design anticipates:

- `qml/DownloadsPage.qml`
- `qml/Main.qml`
- `native/engine/LocalDownloads.h`
- `native/engine/LocalDownloads.cpp`
- `native/engine/AudiobookDownloader.h/.cpp`
- narrowly required downloader result APIs
- Downloads-focused QML/model/contract tests
- `native/main.cpp` only for passing the existing audiobook object into the
  facade

`native/main.cpp` already contains unrelated uncommitted work. Any edit must be
made against the working copy, preserve that work, and be staged/committed by
explicit hunk or path discipline.

## 11. Verification gates

Automated tests must prove:

1. Capability flags match every owning backend.
2. Non-Theatre failed jobs never expose Retry.
3. Failed jobs are excluded from active count and included in attention count.
4. Audiobook active/completed items participate in manager, lanes, and totals.
5. Known-byte group progress is byte-weighted.
6. Unknown totals remain unknown.
7. Destructive mutations cannot execute without confirmation.
8. Mutation pending state blocks duplicate requests.
9. Success and failure results both update visible state.
10. Keyboard activation reaches every action and context menu.
11. Snapshot restoration preserves scroll, folds, ledger, and semantic focus.
12. A lane failure is distinguishable from an empty lane.
13. Existing Downloads routing for manga, comics, books, and Theatre remains
    intact.
14. The stock build gains no new dependency.

Real-app checks must cover:

- one genuine failure and cancellation in every owner;
- Theatre retry, pause, resume, and play-while-arriving;
- forced filesystem deletion failure;
- group/season cancellation confirmation and partial failure;
- an audiobook-only active queue;
- completed audiobook open into the paired book Audio surface;
- unequal file sizes and an origin without declared length;
- keyboard/remote-only navigation at 100 series / 500 items;
- close/reopen restoration;
- ultrawide layout and horizontal overflow;
- Hemanth's eyes on wording, focus, progress, and destructive behavior.

A green harness does not close an audit finding. Hemanth's eyes on the real app
remain the closure gate.

## 12. Audit disposition

### P0

- Dead non-Theatre Retry: closed by capability-driven actions and honest source
  routing.
- Misleading Remove/delete: closed by exact labels, confirmation, and mutation
  results.
- No keyboard/remote operation: closed by focus topology, context menus,
  accessibility, and semantic focus restoration.

### P1

All fifteen are in scope:

- audiobook manager/totals/failure/open/empty-state/title integration;
- failed-versus-active summary truth;
- per-lane failure reasons;
- hit targets;
- destructive group cancellation;
- mutation acknowledgment and errors;
- byte-weighted progress;
- split active/library refresh;
- page-state restoration;
- unavailable-versus-empty distinction.

### P2

In scope:

- vertical virtualization/bounded delegates;
- Lucide assets in place of Unicode glyphs;
- image decode bounds;
- tokenized page-owned accents;
- reduced-motion-aware local folds;
- content-width cap;
- horizontal overflow affordances.

Deferred with recorded blast radius:

- shared no-op/full-screen backdrop capture pattern — 26 surfaces;
- shared per-page Theme construction — 74 surfaces;
- shared ScrollGlide/scrollbar interaction.

## 13. Definition of done

This repair is done only when:

- every visible action is executable on its backend or is absent;
- every destructive action names deletion and confirms before acting;
- every backend failure has an honest, persistent user-facing state;
- all five lanes participate in one truthful manager and summary;
- keyboard/remote users can reach the complete page;
- leaving and reopening does not lose the user's place;
- active updates no longer rebuild the entire settled vault;
- scoped automated and stock-build verification pass;
- no unrelated dirty work is staged or committed;
- Hemanth validates the real page and closes the findings.
