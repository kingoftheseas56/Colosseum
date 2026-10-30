# Proposed simpler sharing UI for 1.3.0

Hemanth rejected the current sharing dialog as ugly, unlike Colosseum, and too complex. This proposal was prepared immediately before the work was paused. **It has not been implemented or visually accepted.** Use it as a starting point after an explicit instruction to resume, not as permission to start work now.

## Intended experience

Sharing an ordinary collection should take a few seconds in a compact popup. Gifting remains a personal, optional extension of the same collection. Local collections remain the primary feature. Keep all locked publication, gift, privacy, offline, identity, revocation, and recipient-copy semantics.

Rejected approaches: retaining the three-tab publishing console leaves the complexity visible; a new full-page wizard makes everyday sharing feel larger. Use one small default sharing popup and separate, reachable gift/receive/manage views inside it.

## Default share view

- Heading `Share`, collection name and a restrained collection icon or existing cover treatment. Reuse existing components; do not build a new artwork system.
- One short sentence: `Anyone with the link can view this collection.` Change the sentence appropriately for protected links.
- If no active ordinary link is connected to this exact local collection: one primary `Create link` button. This explicit click is the publishing action. Never create or update a public link on opening the popup.
- After creation, show the selectable link and primary `Copy link`. Reuse the connected ordinary link when reopening; do not create duplicates on each Copy.
- `Make a gift` is a quiet secondary action, leading to its own view. No dedication, signature, preset grid, cover uploader or live preview in the default share view.
- `Link settings` discloses optional passphrase controls. `Shared links` is a subdued management entry, not a tab bar. Existing linked ordinary collections can expose `Publish changes` with a short explanation that private edits stay private until this action.
- Signed-out users see a concise sign-in explanation. If adding a sign-in action, route through the existing AccountCenter authority; never create another authentication flow.
- No happy-path revision numbers, publication jargon, receipt/retry instructions, long warnings, or receive/file controls. Show recovery only when the corresponding error occurs.

## Gift view

- Back to Share, heading `Make a gift`, collection name.
- Personal message and optional signature. Optional `Appearance` disclosure contains the three existing presets and bounded custom cover controls. Keep these available without dominating the form.
- Optional preview is restrained or disclosed; it must not make the composer an entire page by default.
- Primary `Create gift link`, secondary `Save draft`. Drafting stays offline; creating a hosted link requires sign-in. Save the presentation before publishing as the existing controller requires.
- After success show the created gift link, a clear Copy action and a brief fixed-edition explanation. Do not silently update or reuse a sent gift edition as a new gift.

## Receive and manage

- Opening from the overview's receive entry starts directly on a small `Open a collection` view: paste link, Open, and a quiet saved-edition-file action. Passphrase controls may be disclosed or appear after an unsuccessful attempt; never parse localized error prose to infer domain state.
- Keep actual closed gift -> Open -> explore -> Keep Gift behavior and offline retained copies. Long received contents can scroll.
- Management is a separate view with Back. Compact link rows show the title, relevant protection/status and Copy. Update/stop-sharing actions are secondary and contextual. Preserve owner/source identity checks.
- Keep visible, correctly named confirmations for revocation and uncertain retry recovery; remove generic `Confirm` copy where a real verb fits.

## Important privacy constraint

The current update API replaces the passphrase, and an empty string removes protection. Simplifying the UI must NEVER silently clear protection because a hidden input is empty. For an already protected link, the update flow must require a passphrase for the new snapshot or an explicit, explained choice to remove protection. Do not change server semantics to avoid this. No automatic publishing, automatic private-edit sync, or passphrase persistence/logging.

## Colosseum visual ownership

- Canonical tokens: `qml/Theme.qml`; display face Fraunces used sparingly, Segoe UI for controls/body; ink #f7f7f5, secondary #c9c8d0/#9a99a5, sparse gold #f0c44a. Do not introduce new global tokens or a rebrand.
- Canonical material: existing `qml/Glass.qml`, with the actual persistent wallpaper supplied via `CollectionsPanel.wallpaperItem`, a readable dark scrim and restrained edge. Inspect `qml/DlnaSharePopup.qml` for a text-led Colosseum glass surface. Supply the backdrop through the popup's owning panel; never sample the whole home/window.
- Reuse CollectionActionButton, existing CollectionIcon, file/cover validation, QML Controls.Basic, and Qt Popup overlay/focus behavior. Do not modify shared button styling for this one feature.
- Target default logical width around 460-520, content-driven height around 300-400. Default sharing should fit at 1280x720 without scrolling. Narrow widths stay within viewport margins; gift/received/manage views may grow and scroll within viewport limits.
- One clear primary action at a time, light typography hierarchy, generous but disciplined gaps, no three-tab strip, no giant navy administration form, no decorative dashboard cards or repeated explanatory paragraphs.
- Established Theme/Glass implementation is the maintained design equivalent for this bounded feature revision. Do not add repo-wide DESIGN/UX manifests, install frontend dependencies, refactor other screens, or run a whole-repository UI audit.

## Future implementation and acceptance

Keep changes focused on CollectionsSharingPopup, its owning panel and concise action copy. Extend the existing controller only if needed to identify exact completion reliably; never infer state from localized message strings. Preserve source/owner identity and profile isolation across pending requests. No new backend/store/schema/API feature is needed for this UI revision.

Update meaningful sharing QML tests to exercise visible navigation: compact default view, gift disclosure, correct publish payloads, existing-link reuse, explicit protected-update choices, receiving, and recovery. Do not click hidden obsolete controls to preserve old tests. Run affected native checks if their code changes, and refresh the real runtime_ready target and QML manifest.

Inspect actual desktop and narrow-width captures. The default share view should fit without scrolling. Verify keyboard focus/Tab/Escape, inline errors, stable busy states and retained form values. Exercise ordinary create/copy/update and the gift/receive/Keep/offline flows affected by navigation. Final acceptance must use the exact combined source, rather than relabeling old captures as current.
