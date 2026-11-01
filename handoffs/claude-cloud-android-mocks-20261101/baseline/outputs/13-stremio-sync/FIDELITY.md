# 13-stremio-sync — fidelity and fitting record

**Deliverable:** Standalone local HTML/CSS/JS review of the original centred Stremio Sync panel. This is not a Colosseum, Qt, Android, or beta implementation. Publication remains held. Writes are confined to this task's assigned output directory.

## Sources and authority

- **Approved desktop:** reference/desktop @ a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41. Inspected qml/StremioSyncPanel.qml (184 lines) and qml/Theme.qml.
- **Actual parent route:** reference/desktop/qml/Main.qml (overlay at lines 5261–5269; Connections loader around 5932–5955) and TrackerSyncCenterPage.qml (header around 1637–1720; Stremio card and Open settings action near 2281–2430).
- **Android recovery, secondary:** reference/alpha/qml/StremioSyncPanel.qml at 77844eef0bd3b7fee2c1f884be4a76db6d63321d, and its Theme.qml. Used the original compact spacing, action wrapping and font scaling; it does not override later reviewed desktop design.
- **Rejected comparison only:** reference/android-drift/qml/SettingsScaffold.qml. No approved desktop/alpha SettingsScaffold exists. Its viewport concepts did not authorize a new sheet, and it is not reproduced here.
- **Explicit task context:** context/ANDROID-PORT-CLASSIFICATION.md, particularly #18 (restore panel) and #28 (prevent opening IME), plus context/VIEWPORTS.json (provisional sizes/insets).
- **Real local resources:** reference/desktop/assets/icons/stremio-official.svg, Colosseum and source tracker/taskbar icons, assets/fonts/Fraunces-Regular.ttf, and local-fonts Segoe UI family, referenced but never copied/distributed.

## Source component → HTML counterpart

| Approved component | HTML mapping and fidelity |
|---|---|
| Shown/hidden overlay, dark intercepting rectangle and MouseArea | #overlay.stremio-overlay; dark scrim intercepts clicks, outside clicks do not close. #panelOpen is outside-frame reviewer control only. |
| stremioSyncPanel Rectangle | #panel.sync-panel: centered, width max 620px, height 440px or 470px during confirmation, #151821 fill, 1px theme edge, radius 24 desktop / recovered 16 phone. |
| Theme tokens | CSS gold #f0c44a, ink #f7f7f5, dim #c9c8d0, dimmer #9a99a5, edge white at 18%; actual Fraunces display and Segoe UI body. |
| Official Stremio Image | .official-icon points to the exact approved SVG, 38px desktop / 30px alpha phone. |
| heading() and detail() | #panelHeading and #panelDetail with exact approved status-dependent product text. 30px display on desktop and 22px alpha compact phone. |
| Heading Row, Column, divider | .panel-heading-row and .divider preserve order, colors, spacing and 1px line. Labels wrap without truncation. |
| Phone header Close/Cancel from alpha | #headerClose, visible on phone only, absent on desktop; existing recovered phone control, not invented. |
| Account/Last sync Grid | .metadata definition list keeps two columns, source labels, row order, colors and logical font sizes. Long account strings wrap in the value cell. |
| Optional last-result Text | #result: exact optional gold information slot; supplied content is explicitly labelled illustrative outside the frame. |
| Switch/disconnect confirmation Text | #confirmationText: exact source wording with original condition-based visibility. |
| Primary PanelAction | #primaryAction: gold filled, same status/confirmation labels and busy/paused/unavailable disabled policy. |
| Switch account PanelAction | #switchAction: same secondary pill and confirmation trigger; hidden during confirmation/unlinked states. |
| Disconnect PanelAction | #disconnectAction: original secondary pill and conditional visibility. |
| Cancel/Close PanelAction | #cancelAction: Cancel dismisses confirmation, Close dismisses panel. |
| KeyboardAction and Escape | Native button keyboard activation, primary-button focus and gold focus outline; Escape cancels confirmation first then closes panel. |
| Alpha Flickable and revealPanelAction | .panel-inner scrolls vertically within the centered surface; focused action is scrolled into view rather than moved to a sheet. |
| Main integration and TrackerSyncCenterPage | Source-derived full Connections background; Open settings › uses the actual Stremio entry label and reopens the centered panel. |

## Fitting decisions

1. **Desktop:** Retains 620px cap, 24px radius, 34px padding, 18px gaps, 38px icon, 30px heading, 42px actions and 440/470px target height. Actions remain a single row when space allows.
2. **Phone in portrait AND landscape:** Uses the original alpha layout treatment: 12px outside clearance, 16px radius, 12px interior padding, 10px vertical rhythm, 30px icon, 22px heading, 48px actions, and wrapping of the *existing* action buttons. No card or sheet substitution.
3. **Safe-area and keyboard preview:** The panel is centered inside a bounded stage excluding adjustable top/bottom/left/right insets and an explicitly selected simulated keyboard reserve. Available vertical height clamps the original panel; content scrolls. No keyboard automatically appears on panel opening.
4. **Long fields and font scale:** Dynamic account, result summary, confirmation, and description wrap at 1.0 and 1.3 reviewer font scales. Original static copy is unchanged.
5. **Background context:** Shows actual Connections heading, native-library area, available-tracker section, Stremio external-services card and its Open settings › action. This is a *representative* parent composition, not a pixel-identical reconstruction of all tracker data, window/taskbar motion, or wallpaper.

## Behaviors and alternatives

The source action pathways are demonstrated **only by local fixture transitions**, without actual connections or credentials. Connect/reconnect leads to the Connecting preview, Sync now leads to Syncing, switching/disconnecting invokes the original two confirmation states, Cancel cancels, and Close returns focus to the source-derived Open settings action. The reviewer can select all original status headings and long-content scenarios outside the frame.

**Behavior alternatives proposed: None.** The hatched simulated IME area is test instrumentation explicitly marked NOT APP UI, not an OS keyboard or product redesign. Native focus targets a button, not a text field. Sample dynamic account names, last-sync time, and result summary are illustrative and labelled outside the viewport.

## Verification and hard boundaries

- Run command on Laptop 1: node D:\Colosseum-Android-Phone-Mocks-20261101\outputs\13-stremio-sync\capture-and-verify.cjs
- Isolated Chrome/Puppeteer headless browser contexts, temporary user profile removed after completion, private ephemeral 127.0.0.1 HTTP server serving the batch root. No network dependency or shared browser interaction.
- captures/audit.json records **12 passing cases**, covering 412×900, 900×412, 354×792, 792×354, 1280×700, 800×1280 and 1280×800, switch/disconnect, closed/open, long values, 1.3 font scale and keyboard insets. All asset/font checks, panel bounds, horizontal overflow and reachable original actions passed in the tested cases.
- captures/portrait.png, landscape.png, desktop.png, nine other state/size captures, plus keyboard-portrait-actions.png and keyboard-landscape-actions.png are actual browser renders. A local click journey tests switch → cancel → disconnect → connect → close → reopen with no provider calls.
- The separate BDC render_html endpoint returned HTML_RENDER_OUTPUT_MISSING, so its result was not used as verification evidence. All listed passing captures were produced independently with installed Puppeteer and Chrome.
- **Extreme IME geometry limitation:** 792×354 with an actual 260px keyboard and 24px top+bottom insets leaves only 46px, less than the approved 48px phone button. An accessible full-size action is impossible without keyboard dismissal/resizing. The demonstrated keyboard-landscape stress preview explicitly uses a 140px reserve; controls remain editable and no claim about a real OnePlus keyboard is made.
- HTML screenshots are *not* QML/native/Android runtime verification. Real keyboard focus, Qt ScrollBar behavior, native window safe areas, OnePlus logical viewport/DPR, and real profile-owned sync results remain unverified. No app source, device, beta, or other worker output was altered.
