# 14 — Extension setup and Tankoyomi configuration: fidelity review

**Scope:** Standalone, review-only HTML/CSS/JS in this directory. Not an Android build, QML implementation, runtime qualification, beta change, installation, account action, or publication.

**Authority:** Approved desktop checkout `reference/desktop` at `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, including reviewed desktop Connections change and collection close refinement. `reference/alpha/qml` at `77844eef0bd3b7fee2c1f884be4a76db6d63321d` is only an Android keyboard/interaction recovery reference. `reference/android-drift/qml` at `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9` is a *rejected contrast*, never the visual target.

## Open these files

- `index.html` — local task index; links both required pages.
- `extension-setup.html` — Android original setup panel, desktop embedded setup/failure state, and an approved-style surrounding Extensions Store context.
- `tankoyomi-configuration.html` — full original-language, provider priority, tabs and Behavior configuration surface.
- `style.css` and `app.js` — shared offline styling and local-only interactions. `render-mocks.js`, `verify-mocks.js` and `captures/` are retained repeatable evidence.

Serve the **batch root** with any already available local HTTP server, then navigate to `/outputs/14-extension-setup/index.html`. All relative asset paths resolve against `../../reference/desktop/assets/` and require no external internet. Individual pages also render as local `file://` URLs in headless Chrome.

The inspection controls stay **outside** the Colosseum viewport. Presets: phone portrait 412×900 and 354×792; landscape 900×412 and 792×354; desktop 1280×700; tablet 800×1280 and 1280×800. The reviewer can independently edit width, height, top/right/bottom/left insets, font scale (1.0/1.3) and simulated keyboard reservation. **All dimensions and insets are provisional, not measured OnePlus values.** No app-branded status bar, navigation bar or keyboard artwork is fabricated.

## Source-to-mock mapping: extension setup

| Approved source and control | HTML counterpart | Fidelity and state |
| --- | --- | --- |
| `ExtensionsStorePage.qml` backdrop and `WorldPage.qml` / `TopBar.qml` Back-only masthead | `.art-bg`, `.art-veil`, `.store-context`, `.store-top` | Original Jolly Roger Store wallpaper, Extensions title, subtitle and visible Back context. Back context is inert in the static underlay. |
| `ExtensionsHouseHero.qml` banner, corner Search/From URL and House app tray | `.house-hero`, `.house-actions`, `.house-tray`, `.house-app` | Real source banner and eight real local project logos. Tray preserves white-square app icons, name chips and source-on badges; scrolls horizontally on phone rather than shrinking to illegible scale. The banner is a fixed initial still rather than timed crossfade. |
| `ExtensionsInstalledSection.qml` / `ExtensionsInstalledCard.qml` | `.installed-head`, `.installed-rail`, `.installed-card` | Your Add-ons / installed count / See all, 48px logo, enabled text and capability line in original dark-glass horizontal card rail. Entries are representative, not account state. |
| `ExtensionsStoreRail.qml` / `ExtensionsStoreCard.qml` | `.essentials-head`, `.essentials-rail`, `.essentials-card` | Source Essentials five-name order from `ExtensionsStoreApi.js`; logo-stage, colour-wash, rounded art, title. Original stacked phone cards and wide horizontal rail. Descriptions are not invented because the external live catalogue isn't available. |
| `ExtensionsSetupSheetAndroid.qml` overlay/backdrop | `#setup-shade`, `.setup-area`, `.phone-setup` | Same rgba-black scrim, dark #0e1016 rounded glass panel, 520px maximum width, 22px content padding and centered positioning. Phone panel height is viewport/IME clamped, scrolls where needed. |
| Header title / 34px × close action | `.setup-heading h2`, `.setup-close` | Fraunces title and source Android ×. Inspector changes example add-on to demonstrate long text without modifying product chrome. Header stays sticky while keyboard forces body scroll (alpha recovery fitting). |
| Phone text / Open setup page action | `.setup-description`, `[data-open-browser]` | Exact source copy, cream primary pill and dimensions. External browser is **not launched**: review action writes only into the outside inspector. |
| Phone link input / Paste link → Install / optional skip | `#paste-input`, `#paste-button`, `[data-skip-setup]` | Same placeholder, rounded translucent input, outlined pill, dynamic text, conditional skip; controls are functional in local simulated state only. |
| Source status and optional skip gate | `#setup-status`, `#setup-required` (inspector only) | Source Adding… state is demonstrated locally; required setup hides skip. No real clipboard read, URL normalization, install or provider call. |
| `ExtensionsSetupSheet.qml` original desktop embedded panel | `.setup-desktop-panel` with `.desktop-head`, `.desktop-page`, `.desktop-foot` | Source desktop geometry, ✕ close, blocked-load recovery copy and Try again, Link only? field plus Install/Paste link/skip. Native WebEngine/remote website is intentionally not mimicked; offline failure state is the authentic demonstrated state. |

## Source-to-mock mapping: Tankoyomi

| Approved source and control | HTML counterpart | Fidelity and state |
| --- | --- | --- |
| `TankoyomiConfigurationPage.qml` root and shared shell / `Theme.qml` | `.app-viewport`, `.tank-bg`, `.tank-scroll` | Source Store wallpaper and dark overlay, Segoe UI text, Fraunces headings, #f0c44a gold, translucent dark glass and source page margins. |
| `BackAction.qml` capsule and page search/window controls | `.back-capsule`, `.window-actions` | Native-vector-chevron shape; desktop displays search, minimize, fullscreen and close; phone hides desktop-only controls. Navigation remains a local demonstration. |
| Breadcrumb/logo/title/rule/explanatory copy | `.breadcrumb`, `.brand-row`, `.tank-logo`, `.tank-rule`, `.tank-intro` | Original wording and typography with actual `addon-logos/tankoyomi.png`. Title 48 desktop / 32 compact; text wraps rather than disappearing. |
| Main Tankoyomi enable switch / status | `#master-switch`, `#master-description` | Original gold 44×24 switch and two source status strings. Toggling is local, not saved to Extensions. |
| Configuration / About tab controls | `.tank-tabs`, `#configuration-panel`, `#about-panel` | Original two labels, gold underline, original About title and paragraphs. |
| Languages glass panel, flags, 58px rows / DEFAULT/active | `.language-card`, `#language-list`, `.language-item`, `.flag` | Same rounded panel, typography, source-layout language status, UK/ES/BR/FR flag geometry based on `TankoyomiFlag.qml`. Flags are CSS-reproduced source drawings, not remote emoji assets. |
| **Original** `defaultSelector` and `defaultMenu` (not drift TouchComboBox) | `#default-selector`, `#default-options` | In-panel 42px selector, ▲/▼ and directly expandable list. Popup is inside and scrolls/clamps; no replacement full-screen Choose an option sheet. Default choice changes selected language and source list locally. |
| Provider route heading, flag and explanatory text | `.route-card`, `.route-eyebrow`, `.route-head`, `.route-guide` | Original text, gold eyebrow and 50px flag, 112×34 Reset order button. |
| Provider ranked ladder, letter tiles, enabled/disabled states | `#provider-list`, `.provider-item`, `.rank`, `.letter`, `.provider-copy` | Original gold ranking circles, serif initials, rows, rank/id metadata, disabled opacity, amber controls; status labels hide in compact layouts. |
| Provider toggle, Move up/down, Reset order | `[data-toggle]`, `[data-move]`, `#reset-order` | Local-only enabled/reorder/reset state, preserving original action semantics. Phone uses 44px *invisible-equivalent layout hit boxes* around unchanged arrow glyphs/toggle tracks. |
| Behavior panel and zero-provider state | `.behavior-card`, `.behavior-grid`, `#provider-list` empty state | Exact source labels/description strings, three behavior segments and the source zero-provider text. |

## Fitting decisions and alternatives

1. **Setup panel** stays the original centered, rounded popup. Phone and landscape constrain it inside adjustable safe insets and a simulated keyboard top edge; long copy wraps; overflow scrolls. A sticky close header is derived from the separately anchored alpha recovery header while retaining the approved panel face. No new sheet title, page flow, or install control is introduced.
2. **Tankoyomi** retains original language and route panels. Compact portrait stacks them; short phone landscape places those *same* two panels side by side when width permits to avoid burying the provider ladder. Original content and controls remain in a single vertical scroll view.
3. Header/master switch and text reflow; provider rows retain rank tile, letter tile, text and controls, with controls wrapping into a second row at small widths. The original desktop label/host hierarchy is kept when values exist. Source dropdown stays inline with height/width clamping, never a separate sheet.
4. Original desktop mode retains large page gutters and the side-by-side configuration. Tablet presets exercise non-phone reach. Long real add-on names, the longest language label, smaller font-scale cases, and simulated IME states have review toggles outside product UI.
5. **Behaviour proposals: none.** No alternate or NEW phone UI is proposed. The original behaviour remains the target. The local mock deliberately stops before all real backend actions.

## Representative data and remaining uncertainty

The local **sample** configuration selects Español, English, Português (Brasil) and Français, with illustrative Spanish provider rows ZonaTMO, NiAdd, Mangasins and leercapitulo and English WeebCentral. These are display samples, **not** a verified current installed source list, enabled state, quality, source support or live domain/host values. A language with zero configured sample providers demonstrates the existing zero state. Optional `allowedHosts[0]` is not rendered because the native Manga bridge values were unavailable; no host was fabricated.

The Store underlay uses authentic named tiles, actual source artwork and the original installed/Essentials card shapes. Entries, toggles, installed count and capabilities are sample data; live catalogue descriptions, pagination, and banner cycling are outside this two-surface review. No new product copy is inserted into the setups or Tankoyomi itself.

Original desktop `SettingsScaffold.qml` and `TouchComboBox.qml` do **not** exist under approved desktop or alpha paths; the inspected ones occur only in the rejected drift snapshot. They were **not copied** or taken as blockers. The approved inline `TankoyomiConfigurationPage.qml` dropdown is the sole target.

These captures are HTML/browser render and behaviour evidence only. They do **not** prove QML pixel parity, native Qt/Android keyboard translation, clipboard permissions, device DPR/insets, actual accessibility semantics, setup WebEngine behavior, live Manga/Extensions bridge values, app integration, or APK/device performance. The OnePlus was disconnected; final device geometry awaits a real measurement. Initial BDC `render_html` calls returned `HTML_RENDER_OUTPUT_MISSING`; the repeatable isolated-headless-browser path below was used instead.

## Repeatable visual and interaction checks

From an existing local Node installation with the **already installed** Puppeteer and Chrome (no dependency installs, no shared browser focus/tab mutation):

```powershell
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\14-extension-setup\render-mocks.js"
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\14-extension-setup\verify-mocks.js"
```

The render runner uses installed `C:\Program Files\Google\Chrome\Application\chrome.exe` in a dedicated `captures/.isolated-headless-profile` and saves `captures/render-audit.json`. It uses the page URL's `?capture=1` mode to render only the app frame at **exact browser viewport** width/height: 412×900, 354×792, 900×412, 792×354, 1280×700, 800×1280, 1280×800, plus menu-open, closed, long-name, About and IME variations. `verify-mocks.js` uses a second isolated profile and writes `captures/interaction-audit.json`; it additionally scrolls to lower providers and the setup link control, screenshots those scrolled states, and checks button hit regions and horizontal overflow.

Screenshots are **viewport crops of scrollable full pages**; content below the fold remains accessible by scrolling. FIDELITY and REPORT refer to browser mock quality only, not released app quality.

## Final evidence boundary

A **completed** 19-state isolated headless Chrome render sweep recorded zero browser errors, zero broken project images and zero horizontal control overflow (`captures/render-audit.json`). After fidelity refinements to the Store underlay and the addition of two real long-name samples, the 21-state concurrent rerun successfully emitted a subset of zero-error captures but stalled on a loaded laptop and was terminated **only within its own headless process tree**; it must **not** be described as a complete final 21-state sweep. The runner has since been returned to single-page sequential execution.

The separate **post-refinement** `verify-mocks.js` completed **26/26 passing local interaction/scroll/tap-target assertions** with no JavaScript errors or external requests. The final `captures/capture-reconciliation.json` independently checks PNG magic bytes, dimensions and presence for **26 captured screenshot files** (including four scrolled-provider states and one focused short-keyboard input) and byte-equivalent canonical `captures/extension-setup/{portrait,landscape,desktop}.png` / `captures/tankoyomi-configuration/{portrait,landscape,desktop}.png`. This verifies retained screenshot files, **not** real-device behaviour or a fresh browser pass of all 21 states.

To re-check screenshot-file integrity without launching Chrome:

```powershell
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\14-extension-setup\audit-existing.js"
```
