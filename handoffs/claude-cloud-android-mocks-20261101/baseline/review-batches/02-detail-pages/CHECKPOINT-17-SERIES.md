# Checkpoint — Theatre series
Completed: 2026-10-31T22:58:23.072Z
Status: finished and verified in the isolated local Chrome HTML runtime.

## Fixed
- Collections now opens the source-backed shared picker with local save/cancel/create state.
- Opt-in TheatreSeriesView phone synopsis is complete; desktop keeps More/Less.
- In Library opens the original TheatreRemovalDialog pattern; Cancel preserves saved state, removal changes only this local fixture.
- Season menu stays inside safe insets, supports arrows/Home/End/Enter/Escape, and restores focus. Jump preserves typed input across scroll and now reaches the selected row using coordinates relative to the scroll viewport. The original jump missed episode 15 by over 800 pixels at the short landscape stress size.
- Landscape ledger reflows the existing order/season controls onto one row, retaining all controls and sticky behavior. At 792×354 / 1.3 font / 24px insets, episode 15 and its Play/Download controls are visible beneath the 122px ledger chrome.
- Episode Download keyboard activation and context-menu dismissal are corrected. Notifications toggle only while saved.

## Evidence
- theatre-original-baseline.json; theatre-finished-layouts.json; theatre-finished-interactions.json; theatre-final-jump-retest.json.
- Original and finished series portrait / compact portrait / landscape / short landscape / desktop captures in captures/.
- Film: More/Less, expanded cast (9 people), recommendation intent, collections create, removal cancellation verified.
- All four phones retested for final episode navigation, keyboard focus, and last season option at 1.3 font / 24px insets.
- Normal 412×900, 354×792, 900×412, 792×354, 1280×700 desktop, 800×1280 tablet and 1280×800 tablet checked. No missing images or application-width horizontal overflow.

## QML anchors and limits
- Main.qml views.rebuiltTitlePages=false / theatreSeriesLayer selects TheatreSeries.qml by default. Do not promote TheatreSeriesView without Hemanth's approval.
- TheatreSeries.qml detailHero/heroCopy, seasonChromeFloat, season-menu FocusScope, episode row and SourcesSheet remain source authority. Reuse Theme, PlayerIcon, LibraryButton, AddToCollectionsButton and TheatreRemovalDialog.
- Preserve safe-area-aware mapToItem coordinates for jump positioning; preserve focus owner when transient menus close. Reflow existing controls without removing labels.
- Frozen local art retained; cast initials remain the existing missing-portrait fallback. Data, source routing, notifications and library changes are local fixtures. Native QML, player, providers, devices, service calls and builds are outside this pass.
- Final contact sheets, consolidated coverage and preservation proof remain to be assembled.
