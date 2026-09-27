# Account shared parts

Slice 0 ports only the five shared Account QML primitives into plain browser helpers. The design authority is the current main-checkout QML:

- `qml/account/AccountPageFrame.qml`
- `qml/account/AccountPanelHeader.qml`
- `qml/account/AccountField.qml`
- `qml/account/AccountButton.qml`
- `qml/account/AccountChoice.qml`

The helpers live on `CW.accountParts`. They do not subscribe to feeds, compute account state, or dispatch account actions.

## Loading

Load `parts.css` after `styles/tokens.css`, and `parts.js` after `components/dom.js`. Slice 0 deliberately does not edit the shared `index.html`; the Account overseer owns the final loader/integration step.

## API

`CW.accountParts.pageFrame(options)` returns the QML AccountPageFrame equivalent. Options are `eyebrow`, `headline`, `detail`, `panelWidth`, `panelMinimumHeight`, `backdropUrl`, and optional `content`. The returned node exposes:

- `.panelContent` — append the page-specific controls here.
- `.panel`, `.intro`, `.scrollRegion` — presentation refs for verification only.
- `.setPanelWidth(value)` and `.setPanelMinimumHeight(value)`.
- `.syncGeometry()` and `.dispose()`.
The layout math is the literal AccountPageFrame formula, including the 1040 compact breakpoint, the wide/compact margins, centered/minimum y positions, and scroll-content tail space.

`CW.accountParts.panelHeader({ kicker, title, copy })` returns AccountPanelHeader. Empty `copy` omits both the copy text and its 10 px spacer, matching QML.

`CW.accountParts.field(options)` returns AccountField. Supported options are `label`, `hint`, `placeholderText`, `password`, `reveal`, `maximumLength`, `controlObjectName`, `inputMethodHints`, `inputMode`, `autocomplete`, `key`, `onAccepted`, and `onInput`. The returned node exposes:

- `.input` and `.revealButton`.
- `.text` as a live getter/setter backed only by the input.
- `.reveal` as a live getter/setter.
- `.clear()` and `.forceInputFocus()`.

Current Account callers only use `Qt.ImhNoPredictiveText`. Passing a truthy `inputMethodHints` disables browser autocomplete/spellcheck, which is the corresponding web behavior.

`CW.accountParts.button({ text, variant, enabled, objectName, key, primaryInk, onClicked })` returns AccountButton. Variants are `secondary` (default), `primary`, and `link`.

`CW.accountParts.choice({ title, detail, enabled, objectName, key, onChosen })` returns AccountChoice.
Disabled buttons/choices are removed from the `data-focus` graph, matching Qt's disabled-focus behavior. Enabled controls use native HTML activation plus Colosseum's single shared focus engine; these helpers install no arrow-key handler.

## Secret boundary

AccountField never copies its value into a dataset, attribute, fixture, log, module variable, or persistent page state. A password/recovery value exists only in the live input until an owning page reads `.text` for its one action payload, then that page is responsible for calling `.clear()` at the QML-equivalent boundary.

## Verification harness

`dev.html` is a slice-local composition of all five primitives using non-secret placeholder data. `walk.mjs` performs the D-pad/keyboard traversal and captures the 1920×1080 and 1280×720 browser proofs. `source-parts-capture.qml` composes the same data from the real QML primitives for side-by-side capture.
