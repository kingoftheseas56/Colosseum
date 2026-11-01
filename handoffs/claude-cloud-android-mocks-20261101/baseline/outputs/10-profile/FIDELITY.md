# Phone Profile rows — source-to-HTML fidelity

**Owned study:** `outputs/10-profile/profile.html` · **Task:** `10-profile` · **Scope:** HTML/CSS/JS demonstration only. Neither Colosseum nor a QML app implementation was modified.

## Authorities inspected

- `reference/desktop/qml/PhoneProfilePage.qml` at approved desktop commit `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`: governing list and row appearance.
- `reference/desktop/qml/PhoneShellHost.qml`: seven authoritative labels/notes/keys, routes, contentBottomInset, Profile loader and shell navigation visibility.
- `reference/desktop/qml/PhoneWorldTabBar.qml`, `WideWorldRail.qml`: original 72px bottom tab bar and 92px wide rail, including original icon paths and active-gold treatment.
- `reference/desktop/qml/Main.qml` (worldDefinitions and Fraunces FontLoader occurrences), `Theme.qml`: original world order, icons, ink, gold, fonts.
- `reference/alpha/qml/PhoneProfilePage.qml`: recovering accepted original row treatment and top-inset max(36, safe.top) concept.
- `reference/android-drift/qml/PhoneActionRow.qml` and `PhoneProfilePage.qml`: comparison only. `PhoneActionRow.qml` does **not** exist in approved desktop/alpha; it is a `PhoneListItem` wrapper with text `›` and must not become the target.
- `context/ANDROID-PORT-CLASSIFICATION.md` item 16 and `context/VIEWPORTS.json`: restoration decision and provisional viewport contract.

## Component and control mapping

| QML component / property | HTML counterpart | Preservation / adjustment |
| --- | --- | --- |
| `Item page`, opaque `#07080c` Rectangle, fall-through-blocking MouseArea | `.app-screen`, `.profile-layer` | Opaque background covers underlying world. No page content is drawn behind it. |
| `Flickable`, StopAtBounds, clipped, bottomMargin=contentBottomInset | `.profile-scroll` | Independently vertical-scrollable, clipped to the original navigation reservation; no visible scrollbar. |
| `Column x:20 y:36 width:min(width-40,640)` | `.profile-column` | Same left inset/max width/36px baseline; the optional inset uses alpha's max(36, topInset) rule. Bottom 48px scroll breathing room retained. |
| `COLOSSEUM` | `.kicker` | Original all-caps copy, 11px Segoe, gold `#f0c44a`, 2.5px letter spacing. |
| Header gaps 8px and 22px | `.heading-gap`, `.list-gap` | Same rhythm and order. |
| `Profile` text | `.profile-title` | Original 30px Fraunces DemiBold intent, `#f7f7f5`, word wrapping. Local Fraunces file. |
| `Repeater` `rows` / `row.modelData` | Seven `button.profile-row[data-key]` entries | Exactly seven rows and original order, labels and notes, sourced from `PhoneShellHost.profileRows`. No synthetic product copy. |
| `row.height=Math.max(60,labels.implicitHeight+24)` | `min-height:60px`, 12px vertical padding | Baseline measured approximately 62.6px; expands for wrapped accessibility text (73.3–93.6px at 1.3×). |
| Press feedback Rectangle radius14, white 0.09 | `.profile-row:active` / `.is-pressed` | Original radius, tint and transient pressed-only feedback, no substituted list card. `captures/pressed.png` shows this state. |
| Labels anchors left14, right chevron-12, spacing3 | `.row-copy` flex column with 14px start inset, 12px chevron gap, 3px text gap | Source text hierarchy unchanged. |
| Label 17px, Segoe UI, Font.DemiBold, `theme.ink` | `.row-title` | Preserved baseline font size, actual local Segoe Semibold file at CSS weight 600, source ink. |
| Note 13px Segoe, `theme.inkDim` | `.row-note` | Preserved baseline, copy, and `#c9c8d0`; wraps rather than truncates. |
| `Item chevron` (10×16), 2 rotated Rectangle bars, rightMargin16 | `.chevron > i + i` | Two CSS-drawn 9×2px rounded bars with source 45°/−45° angles; **not** text `›` and not a font glyph. |
| White 0.08 1px bottom Rectangle, leftMargin14 | `.profile-row::after` | Original inset 1px dividers on all rows. |
| Full-row MouseArea / `rowRequested(key)` | Full-width row button with local `colosseum:rowRequested` event | Local event displays exact request in **external** control panel. Destination pages are intentionally not fabricated. |
| `PhoneWorldTabBar` and `WideWorldRail` / `Main.worldDefinitions` | `.nav-bottom`, `.nav-rail`, five `.world-tab` controls each | Same Theatre/Tankoban/Biblio/Universes/Profile order, original local SVGs, active Profile gold, 22px icons, 11px bottom /12px rail text. |
| `PhoneShellHost.openProfileRow` | `app.js` `endpoints` map and event output | All seven existing route destinations identified; no real account/backend operations. |

## Fitting decisions made

- Portrait 412×900 and 354×792 reserve the source 72px bottom bar; 900×412 landscape and 1280px desktop show the source 92px rail at the current `width >= 840` breakpoint. Narrow 792×354 landscape retains the bottom bar, exactly following the inspected host.
- The list keeps its 20px left inset and 640px maximum width; it does not stretch to the desktop canvas centre. In landscape it scrolls *inside* the Profile area and never behind the wide rail or bottom bar.
- Text wraps naturally and increases row heights for `fontScale=1.3`; 24px inset stress tests keep the shell/navigation free of overlap.
- Editable width, height, top/bottom inset and font scale plus presets are **outside the app frame**; zero insets are the baseline. No fictitious Android status/navigation bar is rendered.
- Icons use the five **actual local SVG assets**; CSS filters approximate Qt's colorizing `MultiEffect` to represent active gold and dim neutral. They are not newly invented icon art.

## Behaviour alternatives / new UI exceptions

**None proposed.** This page contains seven route doors, not accordions or dropdowns; therefore no fabricated open-sheet or expanded-page state is shown. The source's pressed/idle states and original route request are demonstrated. Other pages' contents are not part of this assignment.

## Verification

Headless capture and geometry: isolated Puppeteer using installed Brotherhood Desktop Commander dependency, Google Chrome at `C:\Program Files\Google\Chrome\Application\chrome.exe`; no global mouse/keyboard or existing browser tabs, no external requests.

**Reproduction command:** `node D:\Colosseum-Android-Phone-Mocks-20261101\outputs\10-profile\captures\render-audit.cjs`

All eight specified cases passed browser measurements in `captures/audit.json`: `portrait.png` 412×900, `landscape.png` 900×412, `desktop.png` 1280×700, `narrow-portrait.png` 354×792, `narrow-landscape.png` 792×354, `tablet-portrait.png` 800×1280, `tablet-landscape.png` 1280×800, `inset-font-stress.png` 354×792 (24px insets, 1.30× fonts). Additional `pressed.png` and `studio.png` captures demonstrate source pressed state and the separated controls.

Checks include an automated exact comparison of all seven keys/labels/notes against approved `PhoneShellHost.qml`, exact headless canvas, font weight/size, 7 rows, original dividers and two-bar chevrons, minimum 60px hit regions, shell content reservation, wrapping/no horizontal overflow, last-row scroll reach, original `rowRequested("collections")` dispatch and reviewer-control interactions. No network requests were made. Source-relative icon/font paths are local to the batch root.

For local HTTP review (not started by this job), serve the **batch root** and navigate to `/outputs/10-profile/index.html`; relative asset paths point to `../../reference/...`.

## Uncertainty / review boundaries

1. `VIEWPORTS.json` is provisional. OnePlus CPH2487 was disconnected; these are not measured device logical pixels/insets/DPR. No physical-device or Qt QML runtime validation occurred.
2. Approved `PhoneShellHost.qml` chooses bottom bar for `width < 840`, so the 800px tablet portrait shows the bottom bar. Classification item 22 separately calls out a `min(width,height)<600` Android **phone reach** rule. This is a wider shell policy discrepancy beyond this Profile-row job; the mock faithfully depicts the inspected host rather than silently changing the breakpoint.
3. Browser font rasterization, synthesized Fraunces DemiBold and CSS icon colorization do not prove pixel-exact Qt/Android output. The local Segoe family copies must remain within this private batch and are **not** distributable.
4. The native row's routed pages, account flyout, input-method avoidance and live Android scrolling were not simulated; route events are local proof of intended control wiring only.
5. The BDC `render_html` convenience action returned `HTML_RENDER_OUTPUT_MISSING`; production of screenshots and measured geometry was independently completed through isolated headless Chrome/Puppeteer. No app build, repository edit, device command, publication or deployment was performed.
