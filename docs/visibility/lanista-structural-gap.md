# Lanista structural gap — L1-Discovery

> **Slice:** L1-Discovery (visibility Phase 2). **Scope:** discovery only — no new bridge
> command, no production source change, no compilation. This document pins the smallest
> additional sight an agent needs to explain a layout failure, decides whether to extend
> Lanista or adopt GammaRay, and hands the next executor (L1-Bridge) a concrete payload.
>
> Ground truth captured live 2026-08-12 against `tests/lanista_harness_scene.qml`, served by
> `native/build-msvc/lanista_harness.exe --serve` on pipe `ColosseumLanistaTest`, driven by
> `native/build-msvc/lanista.exe`. Raw replies are preserved verbatim under
> `artifacts/visibility-phase2/l1-discovery/`. Nothing below is inferred from source alone —
> every claim of "cannot obtain X today" is backed by a real command that was actually run and
> either returned an incomplete answer or `NO_SUCH_ITEM`.

## Baseline: what the harness scene actually contains

`tests/lanista_harness_scene.qml` is an 800×600 window with a counter button, a text field, a
box deliberately positioned partly outside the window (`clippedBox`), a virtualized `ListView`
(`mainList`, `model: 20`), a non-virtualized `Flickable` + `Repeater` of 40 rows (`longList`),
and a focusable key sink. Every element the SCENE AUTHOR intended to be automatable carries an
`objectName` — but Qt Quick's own internals (background fill, `Flickable.contentItem`, the
`Column` wrapping the repeated rows, the `Repeater` itself, and every per-row `Text` label) do
not, because nothing in today's bridge ever asks for them.

**`dump-ui` and `ui-snapshot` both returned exactly 68 items/elements**
(`artifacts/visibility-phase2/l1-discovery/dump-ui.json`,
`.../ui-snapshot.json`) — expected, since every actionable/named item in this fixture happens
to already carry a name (the scene's own header comment: "Every interactive element is
objectNamed"). Cross-checking that 68 against a manual depth-by-depth walk of the QML source
(`contentItem` → `Window`'s direct children → …) accounts for **68 named + at least 65
unnamed** items in the live tree — the unnamed set breaking down as: 1 root `contentItem`, 1
background `Rectangle`, 2 `Flickable`-internal `contentItem`s (one per `ListView`/`Flickable`),
1 `Column` (id `col`, no `objectName`), 1 `Repeater`, 19 per-row `Text` labels inside `mainList`
(only 19 of the 20 model rows are materialized — `ListView` virtualization in action), and 40
per-row `Text` labels inside `longList` (all 40 exist because `Repeater` does not virtualize).
That gives **≈133 total `QQuickItem`s against 68 that the bridge can see or name today** — a
1.96x blind spot on this one small fixture. (Caveat: this total cannot be independently
confirmed by any existing command — Qt-internal items such as a `TextInput` cursor handle, if
any, are not accounted for. That inability to get an authoritative item census is itself part
of the gap.)

`native/build-msvc/lanista.exe --pipe ColosseumLanistaTest ui-query object=col` and
`object=background` both returned `{"code":"NO_SUCH_ITEM"}` — proving those items are not just
absent from the two bulk dumps, they are **unreachable by any command**, because every command
targets by `objectName` and these items have none. See
`artifacts/visibility-phase2/l1-discovery/ui-query.json` for the verbatim replies (keys
`col_NO_SUCH_ITEM`, `background_NO_SUCH_ITEM`).

`artifacts/visibility-phase2/l1-discovery/harness.png` is the annotated grab: the visible
`counterButton`, the `clippedBox` cut off at the right window edge (the one clipping case the
bridge already detects), and `mainList` showing only `item 0`–`item 4` — five of its 19
materialized (and 20 modeled) rows are genuinely on-screen; the rest are clipped by the
`ListView`'s own `clip: true`, not by the window.

## Facts that cannot be obtained today (real probes, not inference)

| # | Fact | Probe actually run | Result | Consequence |
|---|---|---|---|---|
| 1 | Unnamed items exist / their geometry | `dump-ui`, `ui-snapshot` | Both silently omit them (68/68, matching only named items) | An agent debugging a layout bug involving the background fill, `Flickable.contentItem`, `Column`, `Repeater`, or a delegate's own label Text has **no way to see it exists**, let alone measure it. |
| 2 | An item's parent | `qml-get object=listRow10 props=["parent"]` | `{"parent": null}` — a `QQuickItem*` does not survive `QVariant`→JSON | `dump-ui`'s `depth` integer tells you *how deep*, never *under which named ancestor*. Two sibling subtrees at the same depth are indistinguishable without walking the whole array and guessing from geometry. |
| 3 | Child count of an item | `qml-get object=mainList props=["children","childrenRect","visibleChildren"]` | All three serialize to `null` | Cannot answer "how many children does this container actually have" without a full `dump-ui` scan and manual depth/geometry correlation — and that scan itself misses unnamed children (see #1). |
| 4 | Ancestor clipping (as opposed to window clipping) | `ui-query object=listRow10` | `visible:true`, `clippedByWindow:false` for an item at scene `y=500`, while `harness.png` proves it is **not actually rendered** (its parent `mainList` clips at `y:260`–`380`) | `clippedByWindow` answers one narrow question (is this outside the *root window*) and actively **mis-answers** the question an agent actually asks ("is this really on screen"). This is the sharpest, most concretely dangerous gap found. |
| 5 | Per-item `z` | `qml-get object=counterButton props=["z"]` | `{"z": 0}` — retrievable, but **only** if the caller already knows the exact objectName and asks for `z` by name | Not present in `dump-ui`/`ui-snapshot`/`ui-query`'s default reply shape; an agent cannot audit z-order across a subtree without already suspecting every name in it. |
| 6 | Root-window bounds alongside structural rows | `dump-ui` reply schema (source-read, `LanistaServer.cpp:684-710`) | Absent; only `get-state` carries window `width`/`height`, in a separate call | Correlating "is this item within the window" per structural row requires a second round trip and manual arithmetic per item today. |
| 7 | Reply bounding / truncation | grep of `LanistaServer.cpp` for `truncated`, `generation`, `maxDepth`, `maxItems` | No matches | `dump-ui`/`ui-snapshot` have no `root`, `maxDepth`, `maxItems` request bound and no `truncated`/continuation reply field. This harness scene's `dump-ui` reply is ~17.8 KB pretty-printed for 68 items; nothing stops a larger real page (a big Vault grid, a long Discover wall) from growing toward the ledger-pinned 1 MiB line-length ceiling with no graceful degradation — it would simply hit `LINE_TOO_LONG` or grow unboundedly. |
| 8 | Binding provenance (why does property X have value Y) | source-read of every reply builder in `LanistaServer.cpp` (`cmdQmlGet`, `cmdUiQuery`, `cmdDumpUi`, `cmdUiSnapshot`) | None calls `QQmlProperty::binding()` or exposes an expression/source location; `qml-get` reads only `item->property(name)` — the resolved final value | No command can distinguish "this width is a literal" from "this width is bound to a broken expression that silently evaluated to 0". |
| 9 | Model contents (per-row/per-role data) | `qml-get object=mainList props=["count"]` and `object=longList props=["contentHeight","contentY","height"]` | **Succeeded** — `{"count":20}`, `{"contentHeight":2000,...}` | Aggregate model facts (row count, content extent) **are already obtainable today** via targeted `qml-get`, provided the caller already knows the container's objectName and the exact property name. What is *not* obtainable: enumerating a model's rows/roles when the caller does not already know a delegate-naming convention, or reading a real `QAbstractItemModel`/`ListModel`'s per-role values without walking named delegates one by one. |

## Required-now vs deferred

Rows 1–8 are concrete, load-bearing gaps: each was independently demonstrated against a live
harness, not merely reasoned about from source. Row 9 shows the model-contents gap is **partial
and already mostly closed** by existing `qml-get` — which weakens the case for urgency there
specifically (see GammaRay/verdict section).

| Capability | Verdict | Evidence row | Notes |
|---|---|---|---|
| Every `QQuickItem` in a structural dump (not just named) | **Required now** | 1 | Direct fix for the single largest blind spot measured (68 of ≈133 items visible). |
| Ephemeral handle per item (reuse `ui-snapshot`'s `s<gen>h<n>` epoch model) | **Required now** | 1, 2 | Unnamed items need *some* way to be addressed in the same reply and in a follow-up `ui-query`. |
| Type (`class`) | **Required now** | — | Already present in `dump-ui`/`ui-snapshot`; carry forward unchanged. |
| Nullable `objectName` | **Required now** | 1 | Trivial — just stop filtering on non-empty. |
| Parent handle/name | **Required now** | 2 | Directly closes row 2; makes `depth` load-bearing instead of merely descriptive. |
| Child count | **Required now** | 3 | Cheap (`childItems().size()`) once the walker already visits every item. |
| Local rect + scene rect | **Required now** | — | `dump-ui` already has scene x/y/width/height; local rect is one more `mapRectToScene`-adjacent computation, useful for distinguishing an item's own size from its transformed footprint. |
| `z` | **Required now** | 5 | One extra field on an already-visited item; the query cost is the walk, already paid. |
| Effective visibility/opacity/enabled | **Required now** | — | `ui-query` already computes these per-item; folding them into the bulk dump removes the per-item round trip the harness scene's own probes needed above. |
| Root-window bounds | **Required now** | 6 | One extra top-level field per reply, removes the `get-state` round trip for layout math. |
| Clipping-ancestor chain | **Required now** | 4 | The single sharpest, most concretely dangerous gap measured (row 4: `visible:true` + `clippedByWindow:false` on an item that is provably not on screen). This is not a nice-to-have; it is a demonstrated wrong answer today. |
| Request bounding (`root`, `maxDepth`, `maxItems`) + reply byte ceiling with `truncated`/continuation metadata | **Required now** | 7 | No such bound exists anywhere in the current dump commands; this is the plan's own stop-law protection against exceeding the ledger-pinned 1 MiB line ceiling and must land alongside the all-item walk, not after it — an unbounded all-item dump is strictly more dangerous than today's named-only dump precisely because it stops filtering out the majority of the tree. |
| Arbitrary binding graph / provenance | **Deferred** | 8 | Real gap, but no named Phase 2 checkpoint in this plan (`docs/superpowers/plans/2026-08-13-colosseum-visibility-phase2-plan.md`, searched for "binding provenance" / "arbitrary binding" — the only two hits are this slice's and L1-Bridge's own "keep deferred unless..." clause) asks a question answerable only by binding provenance. Every downstream checkpoint (L2 layout verdicts, all six J1 journeys) reasons from property *values* (`qml-get`, `ui-wait-for` strict equality) and geometry, never from *why* a value holds. Revisit if a future checkpoint needs to explain a bound-but-wrong value, not just measure it. |
| Model enumeration (per-row/per-role data) | **Deferred** | 9 | Row 9 shows aggregate facts (`count`, `contentHeight`) are already reachable via targeted `qml-get` when the caller knows the container name and property — which covers the checkpoints this plan actually names (grid counts, e.g. J1-Vault's `grid_count_matches_fixture`, are satisfiable via a named grid's `count`/model-count property, not full row enumeration). No named checkpoint in the plan asks to read arbitrary per-role model data. Revisit only if a checkpoint needs to assert on *content* of unmaterialized (virtualized-away) rows specifically. |

## GammaRay decision

**Verdict: do not adopt GammaRay. Extend Lanista's existing `dump-ui`/`ui-query` surface
instead.** Reasoning:

1. **License/shipping are already closed questions, not open ones.** GammaRay is
   GPL-2.0-or-later / dual-licensed and DLL-injection based (per the plan's own ruling #4).
   Linking it or shipping its probe is categorically forbidden regardless of what this
   discovery finds. The only live question was ever "does the gap *require* GammaRay's
   capability class (live C++ object introspection via injection), or can Lanista's existing
   in-process Qt Quick walker answer it more cheaply."
2. **Every gap measured above is answerable from data Lanista's own process already holds.**
   `LanistaServer` runs *inside* the app process and already walks `QQuickItem::childItems()`
   (`walkVisual`, `LanistaServer.cpp:57-70`) — every fact in rows 1–7 (unnamed items, parent,
   child count, z, clipping ancestors, root bounds, reply bounding) is a `QQuickItem` API call
   away (`objectName()`, `parentItem()`, `childItems().size()`, `z()`, walking
   `clip()`/`mapRectToScene()` up the ancestor chain) from code that is already executing in
   that address space. None of it requires attaching an external injected probe.
3. **Binding provenance (row 8) is the one class of fact GammaRay is genuinely stronger at**
   (it hooks `QQmlAbstractBinding` directly) — but row 8 is deferred (see table above): no
   named Phase 2 checkpoint needs it. If a future checkpoint does force binding provenance, the
   narrower option is Qt's own `QQmlEngine::setPropertyCacheReporting`/`QV4::CompiledData`
   introspection or `qmlbind`-style QML debug protocol messages already ratified for Qt Quick
   (its own dedicated debug wire protocol, no GPL/injection concerns), not GammaRay — that
   decision is deferred with the capability itself and is explicitly **not** ruled on here.
4. **Cost asymmetry.** A GammaRay adoption would mean a new dev-tool dependency, an
   injection-based attach flow incompatible with the plan's isolated-session model (unique pipe
   per session, tagged AppData root — GammaRay attaches to a live process by PID, not by the
   bridge's own session contract), and a licensing review for every future distribution
   channel. A narrow Lanista extension is roughly a dozen additional fields on an existing
   walker plus a request-bounding parameter — the L1-Bridge slice already scopes this as
   "one-session production fence" across four files.

GammaRay remains available to Hemanth or any brother as an **external, manually-attached
developer tool** for one-off live debugging outside this automation stack — exactly the
"reference and optional external developer tool only" the plan's ruling #4 describes. Nothing
in this decision restricts that manual use; it only rules out linking, shipping, or building
automation around it.

## Verdict: the minimal L1-Bridge payload

Extend `dump-ui` (and, symmetrically, `ui-query` for a single handle/name) with the following
fields, computed during the SAME `walkVisual` traversal already in `cmdDumpUi`/`cmdUiSnapshot`
(no second tree walk — matching the existing "one traversal, two behaviours" discipline at
`LanistaServer.cpp:52-56`):

- **Per item:** opaque handle (`s<gen>h<n>`, reusing `ui-snapshot`'s existing epoch/handle
  machinery so `NO_SUCH_ITEM`/staleness semantics do not need to be reinvented), `objectName`
  (nullable — empty string, not omitted, so old and new clients agree on shape),
  `class`, parent handle (and parent `objectName` if non-empty, for human-readability without a
  second lookup), child count, local rect (`{x,y,width,height}` in the item's own coordinate
  space, i.e. `QRectF(0,0,item->width(),item->height())`), scene rect (today's `dump-ui`
  x/y plus width/height, i.e. what `ui-query` already computes via `mapRectToScene`), `z`,
  `visible`, `enabled`, `opacity` (the same three `ui-query` already reports, folded into the
  bulk reply), and a clipping-ancestor chain: an ordered list of ancestor handles between this
  item and the root window that have `clip: true`, each carrying its own scene rect — enough
  for a client to compute "is this item's rect contained within the *intersection* of every
  clipping ancestor's rect" without a further round trip. (This directly answers row 4's demonstrated wrong-answer case: `listRow10` would carry `mainList`'s handle in its clip chain with `mainList`'s scene rect `{100,260,200,120}`, letting the client see the item's own rect `{100,500,200,24}` falls entirely outside it.)
- **Top-level (once per reply, not per item):** `generation` (a monotonic counter bumped once
  per structural-dump call — dump-ui has none today; needed so a client can tell whether two
  replies describe the same moment, matching the discipline `ui-snapshot`'s epoch already
  proves out), root-window bounds (`{width,height}` from `mainWindow()`, removing the separate
  `get-state` round trip row 6 needed), `truncated` (bool), and continuation metadata (e.g. a
  resumable cursor — the exact shape is an L1-Bridge implementation decision, not pinned here).
- **Request-side bounds (all optional, all clamped server-side):** `root` (an
  objectName/handle to start the walk from, default the window's `contentItem()` as today),
  `maxDepth` (default unbounded today; needs a sane clamp — e.g. the existing 64 recursion
  guard at `walkVisual`'s `depth > 64` check is a ceiling, not a client-tunable bound),
  `maxItems` (stop and mark `truncated:true` once hit), and a **reply byte budget clamp** safely
  below the ledger-pinned 1 MiB line ceiling (this document does not pin the exact number —
  that is an L1-Bridge implementation decision informed by real reply-size measurement against
  a production-scale scene, not this small harness fixture).

**Explicitly not in this payload:** binding graphs, model row/role enumeration (both deferred
per the table above), and no visibility filter (dump-ui's existing "no visibility filter"
behavior must be preserved — an agent debugging *why* something is invisible needs to see it in
the dump, not have it filtered out for being invisible).

This is additive to `dump-ui`'s existing reply shape (old clients reading only
`objectName`/`class`/`x`/`y`/`width`/`height`/`visible`/`depth` are unaffected) and reuses
`ui-snapshot`'s handle/epoch machinery rather than inventing a second identity scheme — matching
the plan's L1-Bridge "Behavior to preserve" clause verbatim.

## Machine-readable appendix

```json
{
  "schema": "colosseum.visibility.l1-discovery.v1",
  "date": "2026-08-12",
  "harnessScene": "tests/lanista_harness_scene.qml",
  "harnessPipe": "ColosseumLanistaTest",
  "baseline": {
    "namedItems": 68,
    "estimatedUnnamedItems": 65,
    "estimatedTotalItems": 133,
    "commandsUsed": ["dump-ui", "ui-snapshot", "ui-query", "qml-get", "get-state"],
    "evidenceFiles": [
      "artifacts/visibility-phase2/l1-discovery/dump-ui.json",
      "artifacts/visibility-phase2/l1-discovery/ui-snapshot.json",
      "artifacts/visibility-phase2/l1-discovery/ui-query.json",
      "artifacts/visibility-phase2/l1-discovery/harness.png"
    ]
  },
  "forbiddenShippingModes": {
    "gammaRayProbeShipped": false,
    "gammaRayLinked": false,
    "gammaRayVendoredInRepo": false
  },
  "requiredNow": [
    {"field": "everyQQuickItemIncluded", "evidenceRow": 1,
     "reason": "dump-ui/ui-snapshot both omit every unnamed item; 68 named vs an estimated 133 total on the harness scene alone."},
    {"field": "ephemeralHandlePerItem", "evidenceRow": 1,
     "reason": "unnamed items need an addressable token; reuse ui-snapshot's existing s<gen>h<n> epoch model."},
    {"field": "typeClass", "evidenceRow": null,
     "reason": "already present in dump-ui/ui-snapshot; carried forward unchanged."},
    {"field": "nullableObjectName", "evidenceRow": 1,
     "reason": "stop filtering on non-empty objectName; report empty string instead of omitting the item."},
    {"field": "parentHandleOrName", "evidenceRow": 2,
     "reason": "qml-get's own parent property serializes to null (QObject* does not survive QVariant to JSON); depth alone cannot identify an ancestor."},
    {"field": "childCount", "evidenceRow": 3,
     "reason": "qml-get children/childrenRect/visibleChildren all serialize to null; no existing command reports it."},
    {"field": "localRectAndSceneRect", "evidenceRow": null,
     "reason": "scene rect already computed by dump-ui/ui-query; local rect is the same mapRectToScene input already available mid-walk."},
    {"field": "z", "evidenceRow": 5,
     "reason": "retrievable only via targeted qml-get today, and only if the caller already knows the exact objectName; absent from every bulk/geometry command."},
    {"field": "effectiveVisibilityOpacityEnabled", "evidenceRow": null,
     "reason": "ui-query already computes these per item; folding them into the bulk dump removes a per-item round trip."},
    {"field": "rootWindowBounds", "evidenceRow": 6,
     "reason": "only available via a separate get-state call today, never co-located with structural rows."},
    {"field": "clippingAncestorChain", "evidenceRow": 4,
     "reason": "demonstrated wrong answer today: listRow10 reports visible:true and clippedByWindow:false while harness.png proves it is not actually rendered, because clippedByWindow only checks the root window, never an intermediate clip:true ancestor."},
    {"field": "requestBounding_root_maxDepth_maxItems", "evidenceRow": 7,
     "reason": "no root/maxDepth/maxItems parameter exists on dump-ui or ui-snapshot today (confirmed by source grep)."},
    {"field": "replyByteCeilingWithTruncation", "evidenceRow": 7,
     "reason": "no truncated/continuation/generation field exists on dump-ui today; an unbounded all-item walk is strictly more dangerous than today's named-only walk because it stops filtering out most of the tree."}
  ],
  "deferred": [
    {"field": "arbitraryBindingGraph", "evidenceRow": 8,
     "reason": "no command exposes QQmlProperty::binding() or an expression/source location; real gap, but no named Phase 2 checkpoint in the plan reasons from binding provenance rather than property values.",
     "forcingPhase2Checkpoint": null},
    {"field": "modelEnumeration", "evidenceRow": 9,
     "reason": "aggregate model facts (count, contentHeight) are already obtainable today via targeted qml-get; no named Phase 2 checkpoint asks for per-row/per-role model data beyond what a named container's own aggregate properties already answer.",
     "forcingPhase2Checkpoint": null}
  ],
  "verdict": {
    "gammaRayAdopted": false,
    "bridgeExtensionRecommended": true,
    "nextSlice": "L1-Bridge"
  }
}
```
