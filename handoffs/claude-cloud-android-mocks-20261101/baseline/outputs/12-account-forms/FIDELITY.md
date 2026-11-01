# Account Centre phone fitting — fidelity ledger

Task: 12-account-forms. Six standalone local HTML/CSS/JS studies. **Mock only:** never Colosseum implementation or Android runtime verification. All writes confined to this assigned output directory. Publication remains held.

## Approved references and scope

- Approved desktop: reference/desktop/qml/account/AccountCenter.qml and AccountProfilePage.qml, AccountSecurityPage.qml, AccountRecoveryPage.qml, AccountDataPrivacyPage.qml, AccountDevicesPage.qml, AccountYourColosseumPage.qml at commit a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41. Desktop is authoritative over older alpha.
- Shared components: reference/desktop/qml/Theme.qml, Main.qml, account/AccountField.qml, AccountPageFrame.qml, AccountButton.qml, AccountAuthoritativeSwitch.qml, AccountAvatarGlyph.qml, AccountDeviceIcon.qml, AccountSecurityIcon.qml, AccountRecoveryIcon.qml, AccountDataPrivacyIcon.qml, HouseWindowControls.qml, local SVG icons and Segoe UI/Inter fonts.
- Phone shell recovery: reference/alpha/qml/account/AccountCenter.qml at 77844eef0bd3b7fee2c1f884be4a76db6d63321d. Already uses horizontal section tabs, Back + Sign out at y60, content beginning at y112 and compact phone layout in both orientations.
- Classification: context/ANDROID-PORT-CLASSIFICATION.md; provisional geometry: context/VIEWPORTS.json. No rejected flat-card, new-sheet, or generic mobile substitution used.
- AccountPageFrame.qml is a separate wallpaper/intro/glass frame for account entry flows, not the actual host of these five Account Centre sections. Wrapping these six mock pages in that separate frame would invent a second shell, so it was not done.

## Shell and shared component mapping

| Original QML | HTML/CSS counterpart | Details preserved |
| --- | --- | --- |
| AccountCenter full-screen backdrop, rail, main layout | .app-frame, .shell, .sidebar, .main | Warm near-black #0d0c09, rail #121009, 232px desktop rail, original dividers, no mobile sheet. |
| AccountCenter railSections, identity and sign-out | .identity, .rail-item, .session-action | Five real section entries Profile ◌, Security ◇, Devices ▣, Recovery ↶, Data & privacy ◫ in source order, active gold, identity, sign-out text. |
| Alpha compactSections, backAction, compactSessionAction | .compact-nav, .back-action, .compact-session | Original 48px horizontal scrollable section rail, 48px Back and sign-out row, page inset 16px, phone portrait/landscape reach. |
| HouseWindowControls | .window-icons and source ../../reference/desktop/assets/icons/*.svg | Original desktop minimize/fullscreen/quit glyphs, absent on Android phone; no live OS operations. |
| Theme.qml / AccountCenter font specifications | CSS tokens and bundled @font-face URLs | Gold #f0c44a, ink, dimmer, glass/edge, Segoe UI page form and Inter shell rail. Review-local fonts referenced, never copied or distributed. |
| AccountButton.qml | .btn, .btn.primary, .btn.danger | Gold primary, translucent glass secondary, danger outline, source border radii, focus treatment and disabled appearance. |
| AccountField.qml | .input-field, .field-label, .reveal | 46px navy text fields, gold focus, 12px labels, 32px reveal button with ◉/○, password echo state. |
| Original Flickable and KeyboardScrollController | .page-scroll and adjustable review keyboard-space reserve | Original scrollable content and a keyboard-safe fitting experiment, not proof of native IME behavior. |
| AccountAuthoritativeSwitch.qml | .switch | 46×26 gold on/off track and 18px knob, unchanged action role. The mock only illustrates host-driven state. |

## Six page mappings

### Profile — account-profile.html

- AccountProfilePage.pageColumn title and profileStage become .page-title and .profile-stage. Hero medallion keeps the 126px circular ring, selected glyph, username, Colosseum account, divider, and selected medallion text.
- AccountAvatarGlyph and avatarGrid become source-derived inline SVGs and .avatar-grid with Initial, Laurel, Column, Book, Screen, Panels and the disabled Custom Later slot. The selected gold rim, 72/62px circles, and built-in pill are retained.
- usernameSection, AccountField, saveButton, and result text become .username-edit, .input-field, .btn.primary and .status-message. Save changes stays disabled until edited. Changes are only local demo data.
- Fitting changes: hero text stacks under its medallion, seven avatars wrap by available width without horizontal clipping, username field/save action stack under narrow content. Long username and keyboard states are inspectable.

### Security — account-security.html

- Approval state reproduces NEW DEVICE, New sign-in waiting for approval, Deny, Approve, Colosseum sign-in and Pending. The normal New-device protection row is hidden while approval is presented, mirroring source.
- protectionRow/AccountSecurityIcon shield becomes .setting-row with gold icon, original full text, Protection is on/off, and the original switch.
- passwordBlock/passwordEditor/passwordGrid becomes Password row, Change, three current/new/confirm AccountField password controls, Use 8–128 characters / Those passwords do not match, Cancel and Save new password.
- logoutBlock and its inline confirmation preserve Sign out everywhere, full warning, Cancel and Confirm sign out.
- Fitting: descriptions, icons and actions stack/wrap under narrow widths; three password columns on wide desktop become one scrollable column on phones. No sheet or alternative behavior.

### Recovery — account-recovery.html

- heroGrid/keyMark and AccountRecoveryIcon become .recovery-hero and 126/92px circular .key-disc. Full Your way back in text, one-time key explanation, and gold-dot Only the newest recovery key works are preserved.
- replaceSection, replacementForm, original warning, currentPasswordField, Cancel and Generate new key become .recovery-row and inline .expanded-content. Empty input shows original Enter your current password message; nothing generates/displays a recovery key.
- passwordLossSection retains If you lose your password and the full original recovery route explanation.
- Fitting: hero glyph stacks above copy under compact widths; expansion and password remain keyboard-scroll accessible without inventing a dialog.

### Data & privacy — account-privacy.html

- historyGroup preserves the History overline and Search history (Clear history, toggle and original description), Your Colosseum activity (Clear activity and toggle), Cross-device history (toggle and text).
- Original inline search/activity clear confirmations preserve question, Cancel, and corresponding Clear search history / Clear activity history text.
- accountDataGroup maps to Your account data, Export my data, Request export / Cancel export.
- privacyMap, portableLane, localLane map to .privacy-map and .map-grid with the full explanation, original two lanes, four facts each, source bullet rhythm and all descriptions: Continue progress / Collection / History / Portable preferences; Search history / Window & session state / Paths & media / Downloads & caches.
- secretNoteCopy preserves Secrets are a separate boundary and its full password/recovery/token/API-credential explanation.
- dangerZone and deleteConfirm keep the Account overline, red Delete account border, full permanence and provider-backup copy, Current password, Cancel and Continue to delete.
- Fitting: source rows/actions reflow vertically on narrow screens, source columns stack, no disappearance of map facts, inline expansion scrolls inside same page instead of a mobile sheet.

### Devices — account-devices.html

- Devices title, Refresh, 42px device count, trusted devices summary and full revocation explanation follow source layout.
- Trusted devices count, AccountDeviceIcon (source SVG monitor path), rows, gold THIS DEVICE mark, platform/relative last-seen text, Revoke and inline Cancel / Confirm revoke are retained.
- Illustrative device data sorted with current device first. Revoke never removes a row optimistically, sends no actual action.
- Fitting: lengthy labels and metadata wrap, confirmation actions stack under narrow widths, all source content remains visible via scroll.

### Your Colosseum — account-your-colosseum.html

- AccountYourColosseumPage pageTop/monthNav map to .month-header/.month-nav, including the two small bordered ‹/› buttons and selected month label.
- monthPortrait/monthHeading/metricsGrid map to original large month/year and 4 metrics: Watch time, Pages read, Completed, Active days.
- monthShelf/featureGrid/featureMark map to 4 source-style **abstract gold-line feature marks**, title labels and metrics; **the approved QML does not render cover imagery in these marks**. No fake posters/stock art were invented.
- activityColumn maps to Recent activity with date, title, subtitle, uppercase gold world.
- Fitting: month navigation wraps below heading, shelf moves beneath month portrait, highlights reflow 2 across on phone, activity rows wrap. Two sample months switch locally.

## Reviewer controls, behaviors, and source limits

The external reviewer strip offers 412×900 and 900×412, smaller 354×792 and 792×354, desktop 1280×700, tablet 800×1280 / 1280×800, editable dimensions/insets, 1.0×/1.3× font scale, source-relevant alternate states and an illustrative keyboard-reserve toggle. All are outside the app frame. The OnePlus is disconnected and sizes/insets are **provisional logical px**, not physical measurement or OS status/navigation chrome. A capture=1 URL option removes reviewer controls for browser screenshots.

Illustrative account name, devices, months, activity and metrics are labelled **outside** the app frame. Passwords start empty and are not saved. All control demos remain local-only: no account/provider API call, credential operation, export, recovery key generation, device approval/revocation, actual sign-out, or account deletion. No new product text or controls were added inside the mock.

**Behavior alternatives proposed: none.** First attempt retains the original controls, actions, inline confirmations, horizontal phone rail, scroll behavior and semantic hierarchy. A distinct mobile UI exception is unnecessary and is not being proposed.

**Known uncertainty:** AccountYourColosseumPage.qml exists in the committed source but was not mounted among the five inspected AccountCenter rail entries, nor found by name in Main.qml. The sixth page is therefore linked from review index.html only, not via an invented in-app rail item. The final production route remains unverified. There is no runtime QML build, APK, Android device test, real IME measurement, DPR/safe-area verification or actual authorization/backend check. Chrome screenshots can verify HTML layout only, not production runtime focus, Qt animation or native accessibility behavior.

## Render and verification evidence

- Tested JavaScript syntax on Laptop 1 with: node --check D:\Colosseum-Android-Phone-Mocks-20261101\outputs\12-account-forms\app.js — exit code 0.
- Screenshot command is recorded in render_fallback.ps1 and per-page corrective runs in refresh_portraits.ps1 and render_your.ps1. The Chrome command uses headless=new, a unique isolated user-data directory, --force-device-scale-factor=1, --window-size=logicalWidth,logicalHeight, --virtual-time-budget=1500 and a file:// URL with capture=1 / preset query; no shared browser tabs or global mouse/keyboard focus.
- Important capture-tool defect discovered and corrected: the earlier capture-mode rule used width:100vw !important, and desktop Chrome's minimum CSS viewport could be wider than a 412px PNG. This incorrectly cropped right-hand content. The capture-mode frame now respects its explicit 412px / 354px logical width from render(), independent of Chrome's internal viewport. Corrected actual 412×900 PNGs were visually inspected for Profile, Security, Recovery, Privacy and Devices; Your Colosseum was subsequently captured after its page-key correction.
- Profile: medallion/avatar grid, full account sign-out and Save changes layout fit. Security: full protection sentence now wraps; no clipped right-side action. Recovery: full key explanation and Replace action visible. Privacy: history rows and switches wrap; long lower sections remain scrollable. Devices: Refresh and Revoke buttons and full revocation copy fit. Your Colosseum: full title, month navigation, metrics and source abstract highlight visuals render.
- The approved QML is not executed. Screenshots are isolated HTML render evidence only, not runtime Android, real IME, native focus/accessibility, account-service or production layout proof.
