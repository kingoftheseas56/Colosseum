# 11-account-entry — fidelity and fitting record

**Status:** standalone local HTML/CSS/JS studies, not QML implementation or approval to ship. The read-only approved desktop source is frozen at **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41** (with reviewed Connections 05c961914). No Colosseum project, references, other batch jobs or beta was changed.

## Source authority

Task classification: context/ANDROID-PORT-CLASSIFICATION.md. Viewport contract: context/VIEWPORTS.json. Approved account originals: reference/desktop/qml/account/AccountWelcome.qml, AccountWelcomePhone.qml, AccountFlyout.qml, AccountCenter.qml, AccountButton.qml and AccountPageFrame.qml. Necessary transitive originals inspected: AccountChoice, AccountWelcomeChoice, AccountPanelHeader, AccountProfilePage, AccountSecurityPage, AccountDevicesPage, AccountRecoveryPage, AccountDataPrivacyPage, AccountAvatarGlyph, AccountSecurityIcon, AccountRecoveryIcon, AccountDataPrivacyIcon, AccountDeviceIcon, AccountAuthoritativeSwitch, Theme, Glass, Main, TopBar, +android/TopBar, HouseWindowControls, PhoneShellHost, PhoneProfilePage and PhoneWorldTabBar. The historical capture fixtures were not treated as phone-fit proof; the rejected Android sheets/cards were not used.

The HTML loads project wallpaper and SVG icons through ../../reference/desktop/assets, actual Fraunces and Inter asset fonts, and the local Segoe family from ../../reference/local-fonts. These are references, not redistributed files.

## Account welcome — account-welcome.html

| Source control or surface | HTML counterpart and preservation |
| --- | --- |
| AccountPageFrame / wallpaper and glass | The actual local Captured Motion wallpaper, dark scrim, translucent blur, original round panel, white edge and inset. |
| AccountWelcome intro | Original COLOSSEUM · WELCOME eyebrow, Fraunces “Welcome to Colosseum.”, gold rule and optional-account explanation. |
| AccountWelcome panel | Original START kicker, “How do you want to begin?”, subtitle and “Accounts are optional” footnote. |
| AccountWelcomeChoice / AccountChoice | Whole-row Continue without account, Sign in, Create account, each with original supporting copy, focus and hover treatment. |
| AccountWelcomePhone | Intro above panel at portrait phone widths; two columns in landscape and desktop; original content scrolls instead of being removed. |

Continue without account follows only a local mock navigation link. Sign in and Create account explain their existing destination outside the frame; no account initialization, credential operation or provider call occurs.

## Account flyout — account-flyout.html

| Source control or surface | HTML counterpart and preservation |
| --- | --- |
| AccountFlyout | 296px compact dark flyout, restrained gold border/sheens, identity medallion, sync line, Account / Devices or Data & privacy links, bottom Sign out/Sign in action. No modal sheet or new title. |
| TopBar / +android/TopBar account button | Signed-in gold-ring initial, signed-out/local identity; desktop original clock/date, six world pills with SOON chips, source Update/Trackers/DLNA/Wallpaper/window glyphs. |
| PhoneProfilePage / PhoneShellHost | Complete original Profile page context with Your Collections, Downloads, Extensions, Your Colosseum, Trackers, Stremio, Account entries. |
| PhoneWorldTabBar | Theatre, Tankoban, Biblio, Universes and Profile labels with local project icons; no fabricated OS chrome. |
| AccountFlyout interaction | Opens from existing account door, closes on outside tap or Escape, clamps within the viewport and scrolls internally at reduced height. |
| Illustrative states | Signed-in Luffy, local Not signed in, offline service, sync attention/pending and long-name stress variants selectable outside the mock. |

All session, sync, shell and navigation operations are local simulations or original static mock links; no real account/service/window changes.

## Account Centre — account-centre.html

| Source control or surface | HTML counterpart and preservation |
| --- | --- |
| AccountCenter | Dark full-page floor, original left rail medallion, five section doors (Profile, Security, Devices, Recovery, Data & privacy), active gold highlight, Back and sign-in/out. |
| HouseWindowControls | Original three desktop-only local SVG window commands; hidden in Android phone/tablet modes. |
| AccountProfilePage / AccountAvatarGlyph | Large circular avatar stage, BUILT-IN mark, seven vector medallions (Initial, Laurel, Column, Book, Screen, Panels, disabled Custom Later), account identity, Username field and Save changes. SVG geometry follows QML. |
| AccountSecurityPage / AccountSecurityIcon | New-device protection, original authoritative 46×26 switch, Password inline editor (Current/New/Confirm), Sign out everywhere confirmation and pending device approval / Deny / Approve. Original vector glyphs. |
| AccountDevicesPage / AccountDeviceIcon | Trusted count and explanatory text, Refresh, monitor-marked device rows, This device status, Revoke and inline confirmation. Illustrative device records only. |
| AccountRecoveryPage / AccountRecoveryIcon | “Your way back in.” circular key emblem, latest-key warning, Replace recovery key inline password form, “If you lose your password” guidance, exact key/replace vectors. |
| AccountDataPrivacyPage / AccountDataPrivacyIcon | Search history, Your Colosseum activity, Cross-device history, Export my data, complete two-lane portability map, Secrets boundary, Delete account and inline confirmations. Native vector path shapes. |
| AccountButton / AccountAuthoritativeSwitch | Glass secondary, gold primary and restrained danger buttons; gold/grey authoritative switches remain visually unchanged when an unfulfilled backend request is clicked. |

Portrait changes **reflow the original rail choices** into visible wrapped rows; landscape keeps its vertical rail. The main content scrolls independently. At 800px tablet width, original compact rows put actions below their copy and the privacy map stacks; 1280px layouts retain two map lanes. The original copy, controls and semantic sections remain. Device/user data are clearly labelled **illustrative outside the phone frame**, not copied from real accounts.

## Fitting changes, not design changes

- Presets: phone portrait **412×900** and **354×792**; phone landscape **900×412** and **792×354**; desktop **1280×700**; tablet **800×1280** and **1280×800**.
- Editable viewport, top/bottom/left/right safe insets and font scale 1.0/1.3 live solely outside the app mock. OnePlus logical sizes are provisional while the device is disconnected.
- Welcome stacks/wraps/scrolls; flyout clamps and uses an internal scroller; centre navigation wraps only where necessary; full action rows keep existing hit targets; no OS nav/status bar is rendered as Colosseum.
- Long usernames, mobile keyboard/inset space, pending approvals, open/closed flyout and inline editor/confirmation states are available without inventing a new Account sheet.

## Behaviour alternatives and approval

**None proposed.** Local click demos and explanatory notices are not product changes. Real account creation/authentication, sync, security switches, password/recovery requests, device approval/revoke, history/export, deletion, avatar save and exit/window commands are intentionally **never executed**.

## Verification and bounded claims

- JavaScript syntax: local Node syntax checks for app.js and verify-mocks.js succeeded.
- The first isolated Puppeteer/Edge run captured 18 viewport/state cases, checked image/font loading and section/popup bounds, and passed four scripted click journeys. Raw output is in captures/BROWSER-VERIFICATION.json.
- Later source-vector/tablet/desktop-shell refinements were statically checked and additional isolated Edge CLI screenshots were rendered. Later browser-automation reruns encountered a browser startup/profile-lock failure; **post-refinement interaction automation is not claimed as passed**.
- Captures show local headless browser HTML, not native QML or Android runtime. The selected project wallpaper is illustrative; Qt blur, DPI, font rendering, focus/keyboard, true OS safe areas and device performance are unverified. No APK, ADB, app build, deployment, uploads, provider or account operations occurred.
- The layouts use relative paths compatible with a local HTTP server rooted at the batch directory; browser screenshots used local file:// URLs instead. HTTP-server execution was not tested.
- At extreme 792×354 with insets and 1.3 scale, the flyout may scroll to reveal its lower session action; at 354×792 the longer welcome text scrolls. This is intentional fitting rather than omitted content.

## Capture method

Verified command family: isolated Microsoft Edge at C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe, flags --headless=new, --disable-gpu, --no-first-run, --disable-extensions, --disable-background-networking, --force-device-scale-factor=1, --virtual-time-budget=1400, --window-size=WIDTH,HEIGHT, --user-data-dir within the owned captures folder, --screenshot within captures, and file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/11-account-entry/PAGE.html?capture=1&v=PRESET&mode=STATE. The test suite is verify-mocks.js (installed BDC Puppeteer with a separate headless browser profile); the run report identifies its own source and checked states. Screenshots are not claims of QML integration.

No files outside outputs/11-account-entry were modified.
