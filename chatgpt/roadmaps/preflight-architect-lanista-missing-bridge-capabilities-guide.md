# Preflight Architect: Lanista Missing Bridge Capabilities Guide

**Repository:** `kingoftheseas56/Colosseum`  
**Baseline:** `master` at `ae29768dcb0cb1a07c1be524aec10ca5fee9f60a`  
**Status:** advisory only; do not implement until the capability gates below are resolved.

## Recommendation

Keep one Lanista transport and add capability providers behind it:

```text
Lanista
├── Test Session
├── Semantic UI
├── Structured Events
├── Domain Probes
├── Evidence Capture
├── Act + Observe
└── MCP Facade
```

Do not create several unrelated local bridges with separate gates, schemas, and artifact stores.

Lanista should remain the local security and transport spine. The missing work is richer observation, deterministic orchestration, stable semantics, and evidence that an agent can interpret without guessing.

## Confirmed findings

- `native/devtools/LanistaServer.*` already provides a user-local named pipe, one-command-per-connection framing, structured errors, and separate Read, Drive, and Write gates.
- Any command may request a combined grab; whole-window capture is synchronous and item capture completes asynchronously through `grabToImage()`.
- `get-state`, `findItem()`, `dump-ui`, and `ui-snapshot` are built around the first root `QQuickWindow`.
- Secondary windows, popup-owned windows, and items outside that root are not represented honestly by the current window model.
- `ui-snapshot` discovers interaction from Qt base classes plus `objectName`.
- A plain `Item` made interactive by a child `TapHandler` or `MouseArea` is not identified as interactive by that superclass walk.
- Snapshot handles are scoped to the latest global snapshot; any client taking a new snapshot invalidates every prior handle.
- `ui-wait-for` polls one QML property and terminates on equality or timeout.
- `invoke-read` currently allows exactly six verified `TankobanVolumes` reads.
- `invoke-read` currently marshals only `QString` arguments and bridges only `QVariantList`, `QVariantMap`, and `bool` returns.
- `events-tail` reads a rotating JSONL file; `log-mark` adds a correlation annotation.
- The event log is best-effort, shared across launches, and not a typed application lifecycle stream.
- `native/tools/lanista.cpp` already supports scenarios, assertions, auto-grab on failure, dHash goldens, JUnit, Markdown reports, and eyes-on briefs.
- `tests/lanista_scenarios/app_home.json` requires the real app to be booted first and deliberately proves that the daily app cannot be driven.
- `dev.bat` enables live reload and disables the QML disk cache; it does not create an isolated data profile, select a unique pipe, or enable Drive.
- `native/tools/lanista-mcp/server.py` exposes three MCP tools; every command except snapshot and grab rides a generic `cmd` plus `payload` passthrough.
- The MCP pipe loop has no explicit client-side deadline.
- `PosterScoreboard` already records image-network outcomes by host and is exposed to QML as `NetScoreboard`.
- Host totals do not explain which card selected which source, whether a cache entry was used, what dimensions decoded, or why a specific `Image` chose its fallback.

## Capability arcs

### 1. Deterministic Test Session

Add a client-side test-session controller that owns one disposable app process.

It should:

- allocate a unique Lanista pipe;
- use isolated application-data, cache, settings, and artifact locations;
- select Read, Drive, and Write gates explicitly;
- choose window size, device-pixel ratio policy, locale, theme, backend, and animation policy;
- seed a named fixture or a copied test profile;
- capture stdout, stderr, QML warnings, process exit, and crash state;
- wait for Lanista readiness before a scenario starts;
- close the app gracefully and kill only as a bounded fallback;
- emit one machine-readable session manifest.

The daily app is not a test fixture.

A test session must never read or mutate the user's live collection, progress, downloads, cache, or settings unless the scenario explicitly declares that dependency.

### 2. Semantic UI Contract

Add stable agent-facing metadata to important QML surfaces.

Recommended shape:

```text
testId
role
label
value
actions
enabled
selected
checked
busy
stable
```

The contract should:

- be declarative;
- survive delegate recycling;
- identify a domain object separately from its current visual handle;
- expose supported actions rather than asking the agent to infer them from class names;
- preserve `objectName` as a debugging landmark, not the only automation identity;
- include windows, popups, focus, modality, transient ownership, clipping, and occlusion.

Do not turn every internal QML item into public test API.

Mark only behaviorally meaningful surfaces and keep their semantics stable.

### 3. Structured Event Plane

Keep the JSONL mirror, but add typed per-session events and a wait primitive.

Required event families:

- route and page lifecycle;
- loader creation, ready, cancellation, and failure;
- model reset, row count, and selected identity;
- image source selection, request, cache, decode, render, and fallback;
- background work start, progress, completion, cancellation, and failure;
- QML warning and JavaScript exception;
- WebEngine readiness and console failure;
- player source, buffering, seek, frame, and terminal error.

Every event should carry:

```text
schema
sessionId
eventId
correlationId
type
source
at
monotonicNs
payload
```

Add `events-wait` with a bounded deadline and a predicate over typed fields.

Do not make agents poll a property every 50 ms when the application already knows the transition occurred.

### 4. Typed Domain-Probe Registry

Replace growth of the single hard-coded `invoke-read` function with a versioned, read-only probe registry.

Initial probe families:

- image diagnostics;
- route and loader state;
- sessions and progress;
- downloads and transfers;
- background activity;
- reader state;
- player state;
- cache and network health.

Each probe must declare:

- name and schema version;
- gate;
- accepted argument types;
- returned JSON shape;
- owning subsystem;
- side-effect contract;
- timeout behavior;
- error codes.

Do not expose unrestricted QObject reflection or arbitrary `Q_INVOKABLE` execution.

A probe exists because a test needs a stable observation seam, not because an object happens to be reachable.

### 5. Explainable Capture and Visual Quality

Preserve the combined state-plus-grab seam, but report the real timing model.

A grab should include:

```text
requestedAt
completedAt
requestFrame
completedFrame
windowId
targetId
logicalRect
pixelSize
devicePixelRatio
colorSpace
artifactSha256
```

For async item capture, do not claim that request-time state and later-frame pixels are strictly atomic.

Add:

- visual-idle wait;
- two- or three-frame stability detection;
- crop and mask support;
- exact pixel comparison;
- perceptual comparison;
- generated diff and heatmap;
- region-specific thresholds;
- minimum decoded-source dimensions;
- sharpness or blur checks for artwork;
- deterministic artifact naming.

dHash remains useful for broad visual drift. It is not sufficient to prove that a cover is sharp, correctly sourced, or large enough for its rendered card.

### 6. Act + Observe Transaction

Add one higher-level transaction that correlates an action with its evidence.

Conceptual shape:

```text
before
→ mark
→ act
→ wait for semantic completion
→ collect events
→ read after-state
→ capture final pixels
→ return one timeline
```

The transaction should support:

- click;
- keypress;
- text input;
- scroll;
- domain action;
- route change;
- image terminal state;
- background-work completion.

It must still use the same Read, Drive, and Write gates underneath.

Do not add retries or arbitrary sleeps inside the transaction. A missing completion signal is a missing bridge capability and should be reported as such.

### 7. MCP Facade and Client Reliability

Keep `lanista_call` as an escape hatch, but add typed tools for common agent workflows.

Recommended tools:

```text
lanista_session_start
lanista_session_stop
lanista_snapshot
lanista_find
lanista_act
lanista_wait
lanista_probe
lanista_events
lanista_capture
lanista_scenario
lanista_artifacts
```

The MCP layer should:

- impose connect, write, reply, and total deadlines;
- return Lanista error codes without flattening them;
- attach screenshots, diffs, logs, and manifests as resources;
- expose command and probe schemas;
- identify the current session and pipe;
- reject operations against an unintended daily-app pipe unless explicitly allowed.

Do not make every agent memorize the wire protocol before it can run one honest test.

### 8. WebEngine and Media Surfaces

Add bounded, least-privilege observation for surfaces that the QML tree cannot explain.

WebEngine candidates:

- URL;
- navigation state;
- DOM readiness;
- visible text;
- selected attributes;
- console errors;
- QWebChannel readiness;
- page screenshot.

Media candidates:

- selected source;
- engine and backend;
- playback state;
- buffering state;
- current time and duration;
- seek completion;
- dropped frames;
- render errors.

Keep WebEngine DOM mutation and media control behind Drive or Write as appropriate.

Do not widen Reader2 or hosted-player bridges merely because Lanista needs observation. Add the smallest test-facing seam with an explicit contract.

## First vertical slice: Biblio image diagnostics

Build the first typed probe around one visible Biblio card.

For each card, expose:

```text
work identity
card/test identity
candidate artwork list
selected artwork URL
requested sourceSize
rendered card size
request start and finish
redirect chain
HTTP status
network error
content type
content length
response bytes
cache key
cache hit/miss
cache eviction
decoder selected
decoded width and height
QML Image.status
fallback selected
terminal reason
```

Correlate these fields with:

- one low-resolution cover;
- one blank cover;
- one healthy cover;
- cold cache;
- warm cache;
- scroll away and back;
- navigate away and back.

`NetScoreboard.summary()` may be exposed as a coarse health probe, but it is not the per-card diagnostic contract.

## Decision gates

1. **Transport ownership:** one Lanista pipe and one gate model; capability modules register behind it.
2. **Session ownership:** the client launches and owns the test process; the app reports state but does not orchestrate itself.
3. **Isolation:** test data, settings, cache, artifacts, and pipe must be disposable and unique by default.
4. **Semantic identity:** prefer an attached QML automation contract; preserve `objectName` for debugging.
5. **Events:** per-session typed events are authoritative; JSONL remains a durable mirror.
6. **Domain reads:** use a typed probe registry; do not weaken the current allowlist into generic invocation.
7. **Capture truth:** distinguish request time from completion time and frame identity.
8. **Visual verdict:** combine semantic assertions, domain evidence, and pixels; no screenshot-only success claim.
9. **MCP ergonomics:** typed tools for normal flows; generic passthrough remains available for bridge development.
10. **WebEngine/media:** defer mutation; add read-only observation first.
11. **Agent compliance:** the skill that requires agents to use these capabilities belongs to a separate arc, but every capability must emit evidence that such a skill can require.

## Evidence required before design freeze

Produce:

- process-launch and shutdown map;
- application-data, settings, cache, and artifact location matrix;
- root, secondary-window, popup, and WebEngine surface inventory;
- representative QML interaction-pattern matrix;
- Biblio artwork pipeline from model data to rendered `Image`;
- event-source map for routes, loaders, images, work, readers, and players;
- probe schema and gate matrix;
- capture timing diagram for window and item grabs;
- MCP deadline and artifact-size budget;
- scenario taxonomy for smoke, feature, regression, visual, performance, and destructive tests;
- performance baseline for snapshots, probes, events, grabs, and artifact assembly.

## Verification contract

A capability is not complete because its command returns JSON.

For every capability slice, prove:

- daily-app safety;
- isolated-session behavior;
- coded failure on invalid input;
- bounded timeout;
- stable schema;
- useful evidence on both pass and failure;
- no silent fallback to the wrong target;
- no GUI-thread stall outside the agreed budget;
- scenario-runner and MCP parity;
- artifact cleanup and containment.

The test harness should compare one working and one failing path at each new seam.

## Stop conditions

Return for review if:

- a test session can touch daily user data without an explicit override;
- a Read-gated command can mutate application state;
- a probe requires unrestricted reflection;
- stable semantics require media-specific branches in shared visual components;
- event instrumentation causes measurable GUI stalls;
- a scenario needs arbitrary sleeps to pass;
- screenshot timing cannot distinguish request state from completed pixels;
- a visual assertion can pass an unreadable or undersized image;
- multiple modules claim authority over the same session, event, or artifact identity;
- WebEngine or media inspection requires broadening a production bridge beyond least privilege.

## Separate arc

Automation beyond Lanista is intentionally out of scope here.

A later research arc should compare external desktop, accessibility, browser, network-replay, video, performance, and CI orchestration tactics against the in-process Lanista model.

That arc should answer where an outside observer adds independent evidence and where it merely duplicates a stronger in-process seam.

## Required next output

A **Lanista Test Session + Biblio Image Diagnostics Design Decision Brief**, not code or a full implementation plan, containing:

- process and data isolation;
- pipe and gate selection;
- session manifest;
- per-card image schema;
- event correlation;
- capture timing;
- artifact contract;
- failure handling;
- security boundaries;
- performance budget;
- acceptance criteria;
- open questions.

## First action

Inspect and map the current path through:

```text
dev.bat
native/main.cpp
native/devtools/LanistaServer.h
native/devtools/LanistaServer.cpp
native/devtools/LanistaEventLog.*
native/tools/lanista.cpp
native/tools/lanista-mcp/server.py
tests/lanista_scenarios/
native/net/PosterScoreboard.*
Biblio shelf artwork selection and its QML Image delegate
```

Build the process-isolation matrix and the per-card image observation table before freezing a design.
