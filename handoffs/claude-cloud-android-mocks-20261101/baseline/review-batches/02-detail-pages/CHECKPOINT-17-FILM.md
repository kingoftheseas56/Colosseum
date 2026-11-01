# Checkpoint — Theatre film
Completed: 2026-10-31T22:58:18.307Z
Status: finished and verified in the isolated local Chrome HTML runtime.

## Fixed
- Collections now opens the source-backed shared picker with local save/cancel/create state.
- Opt-in TheatreSeriesView phone synopsis is complete; desktop keeps More/Less.
- In Library opens the original TheatreRemovalDialog pattern; Cancel preserves saved state, removal changes only this local fixture.

## Evidence
- theatre-original-baseline.json; theatre-finished-layouts.json; theatre-finished-interactions.json; theatre-final-jump-retest.json.
- Original and finished film portrait / compact portrait / landscape / short landscape / desktop captures in captures/.
- Film: More/Less, expanded cast (9 people), recommendation intent, collections create, removal cancellation verified.
- Normal 412×900, 354×792, 900×412, 792×354, 1280×700 desktop, 800×1280 tablet and 1280×800 tablet checked. No missing images or application-width horizontal overflow.

## QML anchors and limits
- Main.qml views.rebuiltTitlePages=false / theatreSeriesLayer selects TheatreSeries.qml by default. Do not promote TheatreSeriesView without Hemanth's approval.
- TheatreSeries.qml detailHero/heroCopy, seasonChromeFloat, season-menu FocusScope, episode row and SourcesSheet remain source authority. Reuse Theme, PlayerIcon, LibraryButton, AddToCollectionsButton and TheatreRemovalDialog.
- Preserve safe-area-aware mapToItem coordinates for jump positioning; preserve focus owner when transient menus close. Reflow existing controls without removing labels.
- Frozen local art retained; cast initials remain the existing missing-portrait fallback. Data, source routing, notifications and library changes are local fixtures. Native QML, player, providers, devices, service calls and builds are outside this pass.
- Final contact sheets, consolidated coverage and preservation proof remain to be assembled.
