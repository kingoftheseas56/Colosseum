# Batch 4 — QML porting notes

## Authority, scope and implementation target

The runnable layout examples are in [finished-mocks/index.html](finished-mocks/index.html). These notes describe fitting changes for Codex to implement later; this pass changed only Batch 4 HTML and its review artifacts. No app or repository source was edited.

Use the frozen [desktop QML](../../reference/desktop/qml/) at **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41** as the visual and interaction authority. Use [alpha](../../reference/alpha/qml/) at **77844eef0bd3b7fee2c1f884be4a76db6d63321d** for existing compact-window fitting. The android-drift snapshot at **9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9** is a rejected comparison, not a specification. Several helper names in the input list exist only in that rejected branch; the approved parent components' inline controls remain the authority.

The HTML fixtures use source artwork, supplied font files and transcribed source vector paths. They show local host responses and request intentions without contacting an account, extension or provider service. Port layout and focus corrections into the original components. Keep controller and presenter ownership intact; do not transplant fixture records or the HTML event simulation as app business logic.

The HTML before/after sheets compare the original worker output with the corrected copy. They are not approved native desktop screenshots. Current native acceptance and Qt/Android runtime behavior have not been established.

## Shared presentation and fitting rules

| Concern | Original component / source | Implementation guidance |
|---|---|---|
| Typography and palette | Theme.qml; AccountPageFrame.qml | Keep Segoe UI for ordinary account UI, Inter where AccountCenter/AccountFlyout explicitly use it, and Fraunces for existing display headings. Preserve gold #f0c44a, ink #f7f7f5, dim #c9c8d0, muted #9a99a5 and the original translucent borders/backgrounds. Do not apply the workbench's review typography inside the app. |
| Account fields | account/AccountField.qml | Keep the original 46 px field, radius 12, label 12 px demi-bold, label-to-field gap 10, text 13 px, left inset 14, password right inset 48, 32 px reveal control at the right and 2 px gold focus border. Preserve each field's maximumLength and Enter behavior. |
| Account buttons | account/AccountButton.qml and explicit page dimensions | Default component height is 46, but the source pages intentionally specify 32/33/34/42 px actions. Preserve those page choices and the original radius 12, 13 px demi-bold text and right elision. A large-font fixture must not spill text outside the same control. |
| Authoritative switches | account/AccountAuthoritativeSwitch.qml and security's inline switch | Retain 46×26 geometry and the 18 px knob. Emit changeRequested / the original controller request; reconcile checked state from the owner. The HTML's immediate response is an explicitly local fixture. |
| Safe areas | PhoneShellHost; alpha AccountCenter | Insets apply once at the shell boundary. Derive content width after margins, rail and safe areas before evaluating a page's compact rules. Do not replace all breakpoints with one “phone” class. |
| Scrolling and focus | Original page Flickables; SystemFocusContainment | Preserve one ordinary vertical content flow, horizontal compact section navigation where present, and the original focus entry/exit. Scroll the focused or newly expanded original control into view. Keep selection/caret and field drafts across unrelated UI state changes. |
| Review controls | HTML workbench only | Preset selectors, “Host fixture,” inset/text/IME controls and intent traces are outside the app frame. They are inspection aids and have no QML porting requirement. |

The tested CSS window sizes are 412×900 and 900×412, stressed at 354×792 and 792×354, with 1.3× text and 24 px top/bottom insets. Desktop is 1280×700. Tablet sanity views are 800×1280 and 1280×800. These are provisional fitting examples, not physical-device qualification.

## 1. Profile rows and their real routes

Source: [PhoneProfilePage.qml](../../reference/desktop/qml/PhoneProfilePage.qml) and [PhoneShellHost.qml](../../reference/desktop/qml/PhoneShellHost.qml). Finished: [profile](finished-mocks/10-profile/profile.html).

Keep the 20 px left margin, 36 px starting y, original gold 11 px kicker and 30 px Fraunces heading. Rows have a minimum height of 60 or their wrapped text height plus 24; title 17 px demi-bold, note 13 px, gap 3, left padding 14, and the divider starts at 14. Preserve the pressed background and radius 14.

The approved chevron uses two 9×2 bars with Item.Left rotation origins, at x=1/y=3 and x=1/y=12, rotated ±45 degrees in a 10×16 item. The corrected flyout underlay follows that construction. Do not substitute a different glyph based on a visual guess.

Preserve rowRequested keys and PhoneShellHost.openProfileRow routing. In particular, **colosseum opens HistoryStats**, not AccountYourColosseumPage. Account opens AccountFlyout with right edge window.width−16 and y=76 in this phone host. That source anchor is intentionally retained; it is not the row's y position. The separately delivered Your Colosseum account page is a preview of that original component and must not be silently mounted as a sixth Account Centre section.

PhoneShellHost's wide navigation rail appears at width >=840 and is 92 px wide. This is distinct from Android compact-window classification using the minimum window dimension. A 900×412 phone can therefore have the original rail while its account content uses compact fitting.

The finished Profile page routes its in-batch Account and Stremio previews locally. Other rows emit the existing route intent without inventing destination screens outside this ownership.

## 2. Account welcome

Sources: [AccountWelcome.qml](../../reference/desktop/qml/account/AccountWelcome.qml), AccountWelcomePhone.qml, AccountPageFrame.qml, AccountChoiceButton.qml and AccountWelcomeChoiceButton.qml.

Retain all three optional-account choices, their text, artwork and source controls. The phone wrapper places the original introduction and panel in a scrollable column; it does not introduce a new sheet. Preserve the phone heading sizes (34/31 as defined), panel padding 18 and source choice styling. Only the welcome-specific choice permits wrapping and dynamic height.

On desktop, keep the 54/34 page margins, 52 px gap, panel maximum width 560, panel padding 34 and radius 18. The existing headline clamp gives 66.56 px at 1280 width; the original worker's smaller desktop headline was corrected. Use the real captured-motion.jpg artwork with the source scrim, not an illustrative replacement.

Sign-in and create-account are original onboarding routes. They are local intent controls in this mock because those destination flows are outside the assigned 13 pages. Continue local opens the existing local-mode account flyout.

## 3. Flyout and Account Centre composition

Sources: [AccountFlyout.qml](../../reference/desktop/qml/account/AccountFlyout.qml), [AccountCenter.qml](../../reference/desktop/qml/account/AccountCenter.qml), alpha AccountCenter and SystemFocusContainment.

The original flyout is an Item overlay with an outside-click catcher, not a replacement Popup or mobile account sheet. Keep its 296 px width, radius 16, #15130f surface, original gold border and 22×20 padding. Preserve mode-specific identity, navigation, retry and session actions.

Reinstate the source lifecycle: rememberInvoker; focusInitial on opening; directional action movement; Tab/Shift+Tab containment; Escape dismissal; restoreInvoker on closing; transfer the remembered invoker when opening the centre. Avoid leaving focus on an invisible underlay or on the document/window root after replacing children.

Desktop Account Centre keeps the 232 px rail, identity and five original sections: Profile, Security, Devices, Recovery, Data & privacy. The corrected centre composes the same form rendering used by standalone previews so field geometry and typography cannot drift. In QML, reuse the original Account*Page components rather than duplicating their bodies.

The alpha compact shell has a horizontal 48 px section row at y=12, back/session row beginning at y=60 with height 48, and page content beginning at y=112 with 16 px horizontal margins. Preserve the original left/right/Home/End section navigation on compact windows and up/down/Home/End rail navigation on desktop. Fit long section labels by horizontal scrolling, not by omitting a section.

Keep section selection, account mode and caller focus context while navigating. HTML query propagation corresponds to this state continuity; it does not imply that native routes should use URLs.

## 4. Account Profile

Source: [AccountProfilePage.qml](../../reference/desktop/qml/account/AccountProfilePage.qml) and AccountAvatarGlyph.qml.

Use the page content width to switch the profile stage at 560 px. Retain the 126 px hero medallion and 31 px name, including wrapping for the long-name fixture. Username content is at most 620 px; the field/action row stacks below 430 px available width. The corrected HTML removed generic tablet behavior that overrode these rules.

Use the original avatar glyph paths and selection ring. Custom / Later remains disabled. Preserve handleAvatarKey, focusAvatarAt, requestAvatar and the controller's setBuiltinAvatar outcome. Saving a username calls the existing renameUsername path, with usernameDirty/saveEnabled and usernameRequestPending controlling availability and “Saving…”.

Retain a live user's draft across unrelated updates, preserving the source distinction between baseline and pending username. Reconcile a successful controller response; do not optimistically declare an actual backend success. The mock deliberately simulates this response locally.

## 5. Security

Source: [AccountSecurityPage.qml](../../reference/desktop/qml/account/AccountSecurityPage.qml).

Two independent content-width rules matter: compactRows below 620 and compactPassword below 760. At 792×354 with 16 px margins, the 760 px form area can still show three original password fields; the short height is handled by scrolling. Do not infer stacking purely from orientation.

Keep current, new and confirmation fields; maximumLength 512; current-password presence; new-password length **8–128 Unicode code points**; and exact confirmation matching. Enter advances through fields and then submits only when passwordFormValid. Closing/cancelling, leaving the section, signing out or device locking must clear ephemeral secrets. An unrelated switch or approval update must not erase a current draft.

Preserve separate passwordRequestPending, logoutRequestPending, protectionRequestPending and approvalDecisionKey state. “Saving…”, “Signing out…” and “Working…” belong to their respective actions, not a new global busy flag. The finished pending fixtures demonstrate these independent source conditions.

Use requestProtectionToggle, requestApproval, submitPassword and confirmLogoutEverywhere. Approval remains Deny/Approve with the existing metadata. Sign-out confirmation focuses Cancel and restores its opener; request handling remains controller-owned, including the source sync-warning flow.

## 6. Recovery

Source: [AccountRecoveryPage.qml](../../reference/desktop/qml/account/AccountRecoveryPage.qml).

Apply compact below 820 content px, keeping the original 126/92 key disc, headings and explanatory copy. Replace key expands the existing current-password form, maximumLength 512, and focuses the field. Empty submission uses the original validation message. Cancellation clears the password and restores Replace key.

Retain beginReplacement, submitReplacement, cancelReplacement, requestPending and reconcilePresenter. “Generating…” is a pending request state. A new key and any reveal/handoff must come from the existing controller/presenter flow; the mock never manufactures or displays a recovery secret. Preserve the explicit warning that only the newest key works.

## 7. Data & privacy

Source: [AccountDataPrivacyPage.qml](../../reference/desktop/qml/account/AccountDataPrivacyPage.qml), AccountDataPrivacyIcon.qml and AccountAuthoritativeSwitch.qml.

Use compact rows and the single-column boundary map below 900 content px. Keep every original history control, export control, portable/local boundary explanation, secrets note and delete confirmation. The cloud icon paths were corrected to match AccountDataPrivacyIcon rather than retaining a malformed SVG number sequence.

Search-clear, activity-clear and deletion confirmations are independent. Opening one must not discard another or erase a deletion draft. Preserve the source Escape priority: deletion, then activity, then search. Focus the safe Cancel action when appropriate and restore the matching opener.

Use the original changeRequested and clear/dataExport/accountDeletionFlowRequested signals. Busy conditions are independent. Export retains a usable Cancel export action. Deletion keeps the current password ephemeral, disables its original field/action while accountDeletionFlowBusy, and hands off to the existing re-authentication flow. The HTML fixture records intent only.

## 8. Devices

Source: [AccountDevicesPage.qml](../../reference/desktop/qml/account/AccountDevicesPage.qml).

Apply compact below 820 content px. Retain current-device-first sorting, then the source recent-activity order, and the original device labels/metadata. Current device has no Revoke control. Revoke identifies a server ID, opens the existing confirmation, focuses Cancel, and restores focus to the tracked device.

Use requestRefresh, openRevoke, confirmRevoke, cancelRevoke, reconcileRevokeTarget and restoreTrackedDeviceFocus. Keep refreshPending and revokeRequestPending separate. Retain the row until the authoritative device list reconciles; a button click is not proof of successful revocation.

The finished mock adds the source empty response (“No trusted devices were returned by the account service.”), refresh error, “Refreshing…” and “Revoking…” fixtures. They are illustrative local service responses, not observed live account states.

## 9. Your Colosseum

Source: [AccountYourColosseumPage.qml](../../reference/desktop/qml/account/AccountYourColosseumPage.qml).

Preserve these distinct source thresholds: widePortrait when scroller.width >960; compactCards below 528; compactHeader below 440. Wide portrait uses the source split and two metric columns. The middle-width single-column portrait uses four metric columns; compact cards return to two. Highlights are four columns except compact two-column fitting.

The original worker incorrectly kept the split portrait at 868 px content width. The corrected landscape uses the source single-column arrangement and scrolls to highlights/activity. Keep the 30 px previous/next controls, 112 px month label, disabled boundaries and previousMonthRequested/nextMonthRequested signals.

The abstract line art on highlight cards is the approved QML presentation. It is not a missing poster asset. Keep the four-item highlight limit, three-line title limit and metadata elision. Do not substitute cover artwork to make it look more familiar. This component remains a standalone preview because the approved AccountCenter does not mount it as another rail section.

## 10. Stremio sync

Source: [StremioSyncPanel.qml](../../reference/desktop/qml/StremioSyncPanel.qml), approved surrounding settings presentation and alpha fitting.

Keep the original centred 620×440 panel (470 when confirming), desktop radius 24, source Stremio artwork, Fraunces heading and existing fields/actions. The alpha phone fitting clamps the panel into the safe rectangle, uses radius 16/padding 12, 22 px heading and 48 px actions, and scrolls its existing content.

All eight source statuses are represented: not connected, connecting, syncing, reconnect required, sync failed, paused, unavailable, synced. Preserve primary-action availability, switch/disconnect confirmations and Escape cancelling confirmation before dismissing the panel. Initial focus goes to the original primary action when enabled, otherwise an available Close action; opening must not focus an input or raise the keyboard automatically.

On short views, focusing an action can scroll the heading out of view. The top/bottom captures intentionally document this single scroll flow. Returning to the heading and reaching every action are both required. Closing restores the original settings invoker.

Actual Stremio connection, merge, sync and disconnection remain service-owned. All corresponding mock actions are local. Keyboard dimensions are geometric reserves only, with requested/applied values shown outside the app frame.

## 11. Extension setup

Sources: [ExtensionsSetupSheetAndroid.qml](../../reference/desktop/qml/ExtensionsSetupSheetAndroid.qml) and [ExtensionsSetupSheet.qml](../../reference/desktop/qml/ExtensionsSetupSheet.qml).

Retain the source Android panel maximum width 520 / window−32, radius 24, #0e1016 background, 22 px Fraunces heading, original close control, 44 px pill actions and 44 px rounded link field. Keep optional Skip separate from required setup.

The existing column now scrolls as one unit. The worker's sticky phone heading could cover instructions on short views and was removed; no replacement header or sheet was introduced. The full-height appearance and desktop embedded-page fallback remain intact.

Preserve isAddonLink, pasteAndInstall, finish and installRequested. The approved clipboard branch validates stremio:// or /manifest.json; the existing typed nonempty path hands the value to finish. Do not invent an extra typed validator during this fitting port. Retain source status copy, empty-clipboard state, pending Adding… and field reset when changing add-ons.

Qt.openUrlExternally, clipboard access, installation and embedded web loading were not exercised in this review. The finished mock emits local intentions. The desktop offline embedded-page fallback is deliberately shown; it is not a fabricated configuration website.

## 12. Tankoyomi configuration

Source: [TankoyomiConfigurationPage.qml](../../reference/desktop/qml/TankoyomiConfigurationPage.qml) and TankoyomiFlag.qml.

Use **header content width <720** for compact heading/master layout and **root width <1050** for stacking language/provider panels. These are separate rules. Preserve 48/32 px Fraunces headings, original Tankoyomi logo, jolly-roger backdrop, flags, master switch, tabs, ranked rows, existing reorder arrows and Reset order.

The default-language selector remains the original inline defaultMenu Column. Remove the worker's extra nested scroll-box limit; opening it grows the original language panel and therefore the outer content height. Scroll the selector plus menu into view when they fit; if a longer real language inventory exceeds the viewport, retain normal outer scrolling and focus visibility. Do not replace it with a native select or a new mobile sheet.

Default selection restores the selector's focus; Escape closes the menu before returning focus; provider movement retains focus on the same provider/action. Configuration/About keep their original tab behavior. Reset order must reorder current records **without changing provider enabled state**; the original worker rebuilt all-enabled defaults.

The native Manga bridge owns language/provider inventory, order, enabled flags and default language. Extensions.revision drives master state refresh. Preserve the component methods setDefaultLanguage, toggleProvider, moveProviderUp/Down, resetProviderOrder and setMasterEnabled. The fixture names and enabled responses are samples, not a replacement inventory, quality ranking or authority for provider hosts.

## 13. Decision still required: short-landscape IME

See [the runnable three-way comparison](finished-mocks/keyboard-decision.html) and [measured evidence](evidence/proposals/account-keyboard/audit.json).

At 792×354, 24 px top/bottom insets, 1.3× text and 152 px IME reserve, the original fixed account shell leaves **26 px** of page viewport. The original 46 px field cannot fit. This was reproduced for username, security password, recovery replacement and deletion password.

The finished default keeps the approved fixed shell and explicitly bounds its *preview* reserve to 82 px, leaving 96 px for the form. This avoids presenting an unusable inspection viewport, but it does **not** establish correctness for a real 152 px native keyboard.

The opt-in **imeLayout=scroll** proposal keeps all original navigation, back/session controls and styling, and lets them join the vertical scroll while IME is visible. It retains the full 152 px reserve and yields a 154 px scrollable viewport; all four fields are reachable. Hemanth must decide whether this behavior is acceptable before native adoption.

If approved, bind the temporary scroll arrangement to actual compact-window and input-method state, compute the real visible safe rectangle, call the existing focus/ensure-visible logic after keyboard geometry settles, and restore the existing fixed layout and sensible scroll/focus context when it closes. Do not hardcode the HTML fixture's 82/152 values into QML. Native Qt IME geometry, overlays/resize behavior and physical orientation still need app-side verification.

## Evidence and remaining qualification

[REVIEW.md](REVIEW.md), [COVERAGE.csv](COVERAGE.csv), [FINDINGS.csv](FINDINGS.csv), per-page JSON audits and contact sheets identify what was checked. Passwords used by automation are synthetic; logs retain only lengths or validation outcomes. No generated recovery secret or real account state is present.

All fixes marked verified are verified as local HTML behavior at the recorded input hashes. Native controller errors, real device insets, Android keyboard behavior, Qt text/font rasterization, live service outcomes and integration routes outside these 13 pages remain outside this assignment. Keep those distinctions when choosing and implementing the layouts.

### Reproducing the expanded menu and setup symptoms

The preserved original worker variants are also rendered at matching landscape and short-stress settings in `evidence/before/expanded-examples/`. Their audit binds those screenshots to original input hashes. The later corrected views are in `evidence/after/states/`. This supplements the default before/after sheets.
