# Tracker Connections · fidelity and fitting review

**Scope:** 21-tracker-connections, standalone HTML/CSS/JS studies only. Frozen approved desktop snapshot `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, including reviewed Connections refinement `05c961914`. The old Android alpha and rejected android-drift are **not** the target design. Nothing in the Colosseum app, repository, build, account, or service is changed.

**Open:** `index.html` or one of `tracker-connections.html`, `tracker-dossier.html`, `tracker-preferences.html` from a local HTTP server rooted at `D:\Colosseum-Android-Phone-Mocks-20261101`. All assets are local, addressed relative to each HTML file through `../../reference/desktop/assets/...`. The review controls are deliberately outside the Colosseum frame.

## Reference and mapping

| Approved QML source | HTML counterpart | Preservation and fitting |
| --- | --- | --- |
| `qml/Theme.qml` | `style.css` theme tokens | Exact gold `#f0c44a`, ink and dim text, glass strokes, wash backgrounds, radius treatment. Browser system Segoe UI for controls and actual `assets/fonts/Fraunces-Regular.ttf` for editorial headings. |
| `qml/TrackerSyncCenterPage.qml` lines 1595–1658, `BackAction.qml`, `HouseWindowControls.qml` | `shell()`, `.shell-head`, `.back-action`, `.window-buttons` | Canonical back chevron + label; desktop minimize/fullscreen/quit SVGs. Window controls hidden on Android as original QML; no fabricated OS status bar. Back follows local overlay stack. |
| `TrackerSyncCenterPage.qml` lines 1659–1679 | `.page-scroll`, `.page-column` | Scrollable full page below the 60-pixel header; desktop retains 80-pixel taskbar clearance, Android scroll adjusts for safe-area bottom inset. Hidden visual scrollbars but active scroll. |
| `TrackerSyncCenterPage.qml` lines 1681–1778 | `heading()`, `.page-heading`, `.page-actions` | Exact heading/subtitle, Preferences icon/label, Sync all icon/label and glass/gold/button treatment. Original 700-pixel two-column gate, stacking on phone. |
| `TrackerSyncCenterPage.qml` lines 1779–1874 | `nativeEmpty()`, `.native-empty` | Native Colosseum emblem, divider, `NATIVE LIBRARY` hierarchy, “Works without trackers.”, local History/progress wording, desktop “Always available” badge. Reflow for narrow portrait without deleting copy. |
| `TrackerSyncCenterPage.qml` lines 1875–1949 | `relay()`, `.relay` | Original relay icon, message, secondary summary, glass/border and 700-pixel grid reflow. |
| `TrackerSyncCenterPage.qml` lines 1951–1992 | `attention()`, `.attention-region` | Original gold warning stroke, exclamation, review/attention summary and “Native History is unchanged.” All readable on phone by wrapping. |
| `TrackerSyncCenterPage.qml` lines 1993–2158 | `connectedCards()`, `.connected-grid`, `.connected-card` | Connected heading, “This profile”, 44-pixel icon tile, icon, account, condition, LAST SYNC, WAITING / REVIEW, Open/Review action. 2 columns from 900 logical px, otherwise 1. |
| `TrackerSyncCenterPage.qml` lines 2159–2292 | `catalogue()`, `.catalogue-grid`, `.catalogue-card` | Same four actual provider SVGs, subjects, status glass boxes. 1 column phone, 2 from 760, 3 from 1380 logical px. The source’s “Capabilities are verified per provider” appears from 900. |
| `TrackerSyncCenterPage.qml` lines 2293–2441 | `external()`, `.stremio-card` | External services, original Stremio icon, Library/Progress/History chips at wide widths, status and “Open settings ›” original route button (local display-only). |
| `qml/Main.qml` near 5932–5956 and 6041; `Taskbar.qml` | `bottomDock()`, `.desktop-dock` | Desktop collapsed shell dock context behind page; hidden on Android, matching `PhoneShellHost.qml`/Main tabbar layering. Must not be mistaken for full Taskbar recreation. |
| `TrackerSyncCenterPage.qml` lines 2443–2575 | `dossier()`, `.overlay-veil`, `.dossier-head` | **Right-anchored dossier**, not a sheet. Same dark veil, 18-pixel outer margin, width max 610, radius22, provider icon, uppercase context, Fraunces title, original round × close, independent inner scroll. |
| `TrackerSyncCenterPage.qml` lines 2590–2744 | `.health`, `dossier()` health section | Status, THIS PROFILE/CAPABILITY GATE, original explanatory states including availability and connection. Connect button is not wired to any service. |
| `TrackerSyncCenterPage.qml` lines 2745–2842 | `accountGroup()`, `disconnectDialog()`, `removeDialog()` | Account/profile ownership text; original disconnect and imported-data removal labels, protected warning and local review dialogs. Confirmations **never** call backend. |
| `TrackerSyncCenterPage.qml` lines 2849–2885 | `capabilities()`, `.cap-pills` | Original outlined capability pills and no-capabilities text. |
| `TrackerSyncCenterPage.qml` lines 2895–2965 | `importGroup()`, `importDialog()` | Original import review section and Review import entry. Long title/progress choices preserved, with sample entries to stress wrapping. |
| `TrackerSyncCenterPage.qml` lines 2971–3053 | `deliveryGroup()`, `.delivery-title` | Original status badges/text, recovery concern, details wrapping. Sample one uncertain-delivery row and one waiting row. |
| `TrackerSyncCenterPage.qml` lines 3056–3121 | `automaticGroup()` | Three original provider switches and the reviewed “Review Colosseum updates before sending” action; layout follows source, not a settings sheet. |
| `TrackerSyncCenterPage.qml` lines 3124–3190 | `globalGroup()`, `tracker-preferences.html` | **Same global dossier** reached by Preferences: Tracker sync; Check connected trackers on launch; Deliver in the background; Show completion messages. Gold 42×23 switch exactly approximated; toggles update the local mock only. |
| `TrackerSyncCenterPage.qml` lines 3200–3434 | `exportDialog()` | Original “Send to {provider}” review, selection checkboxes, reason and selection count. Local selection only. |
| `TrackerSyncCenterPage.qml` lines 3435–3828 | `importDialog()`, `bulkDialog()` | Initial import title, native History safeguard, select, per-title choices, shared-choices review and Confirm/Close controls. Strict sample-only decisions, no progress/history alteration. |
| `TrackerSyncCenterPage.qml` lines 3845–4077 | `matchDialog()` | Title matching popup; original heading, note and “Back to import review”, with local sample candidates only. |
| `TrackerSyncCenterPage.qml` lines 4082–4256 and 4388–4596 | `disconnectDialog()`, `removeDialog()` | Original separate disconnect/removal reviews, pending/uncertain counts and native History protections; no account/provider operation. |

No `SettingsScaffold.qml` exists in `reference/desktop/qml` (exact filename search returned zero matches). The approved `TrackerSyncCenterPage.qml` contains its own complete source for the global preferences dossier. No invented substitute component was used.

## What changes to fit the original UI

The logical viewport is independently editable outside the app frame (412×900, 900×412, 354×792, 792×354, desktop 1280×700, tablet 800×1280 and 1280×800). The same sample HTML reflows—not different card designs. Buttons wrap, connected/catalogue grids use the original width gates, relay and header stack, and long provider/account strings remain scrollable/readable without page-wide horizontal scrolling. The dossier stays right-anchored even at phone widths (phone: nearly full width with the original 18-pixel edge gaps), and the health/account/import/delivery/preference groups are scrolled *inside* its original surface. Modal review content uses internal scrolling within viewport and safe-area insets. Small phone margin, icon and padding refinements are **sizing of the original elements**, not replacement.

The desktop-only shell window buttons and collapsed taskbar context remain desktop-only. The Android full-page tracker overlay is above PhoneShellHost navigation as in the source; no fake Android system bar, no new dock or new app tab was added. Insets are blank reserved space, not rendered OS chrome. On the disconnected sample, the original native-library empty state replaces the relay/connected section. On attention, original warning states and recovery-focused opening are represented.

The font-scale control increases the same type, without replacing fonts. The source's live `ShaderEffectSource` backdrop cannot be evaluated in standalone HTML; this mock uses the existing bundled `assets/wallpaper/cold-ripple.jpg` behind the dark gradient as an **illustrative static project-asset backdrop**. This is not a proof that Colosseum uses that wallpaper at runtime.

## Interactions and behavior changes

All useful actions remain locally demonstrable: open/close provider or global dossier; connected, disconnected and attention scenario variants; preference/provider switches; open/close import, export, disconnect and imported-data reviews; select import/export entries, review shared choices and open a title matcher; Back and Escape close the current overlay; scroll long dossiers or lists. The original buttons are used. **No new product behaviors are proposed.** Representative account status, review titles, counts and delivery flags are fixture values, explicitly marked outside the product frame. They are not connected provider data.

The displayed send/import/disconnect/remove/Sync all/Connect/Stremio commands are intentionally inert beyond local review and feedback. Browser view cannot validate real authentication, sync guarantees, account/profile changes or native data preservation; the actual model revision guards and provider machinery from QML were deliberately not reimplemented.

## Review boundary and uncertainties

- This is not a rebuilt or validated Qt/QML app. Only the static approved QML source was inspected; Android runtime, live device geometry and content, keyboard focus bridge, Android Back gesture, OS insets, IME, telemetry, accessibility tree and animation behavior remain unverified.
- The OnePlus is disconnected. Viewports, insets, and user font scaling are **provisional logical pixels**, not measured device data.
- Fraunces comes from the project's local font asset; Segoe UI resolves on Laptop 1. The review artifact contains no redistributed local Segoe font files.
- Exact live provider names, account labels, connection status, capabilities, accessible enabled states, all review choices and authorization status depend on the native model. Sample data tests fit only.
- Side-panel body is scrolled to see longer content; a screenshot of the top viewport does not prove all below-the-fold controls. The HTML interaction itself supplies access to them.
- If any do-or-die wholly new phone UI becomes necessary, obtain approval before implementation; none is proposed here.
