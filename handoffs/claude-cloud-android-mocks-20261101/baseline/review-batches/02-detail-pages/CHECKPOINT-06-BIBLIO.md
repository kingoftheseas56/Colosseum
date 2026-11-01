# Checkpoint — Biblio book
Completed: 2026-10-31 23:01 UTC (local IST 2026-11-01 04:31).
Scope: finished-mocks/06-biblio-book/biblio-book.html, app.js and shared Collections component only. Original worker output and approved source remain unchanged.
Status: usable finished page; verified in the isolated local Chrome HTML runtime.
## Verified fixes
- B2-001: Collections Cancel now discards unsaved memberships; Save commits page-local memberships. Creation validates a blank name; Cancel creation returns to the picker. Approved cover/count rows, Search/Clear, New collection, Save and Create & add are restored.
- B2-002: Read chooser supports ArrowUp/ArrowDown/Home/End and returns focus after closing.
- Sources: original five-row torrent preview expands to seven; final audiobook/source rows remain scroll-reachable.
## Evidence and coverage
- biblio-baseline.json; biblio-original-interactions.json; biblio-finished-checks.json; biblio-stress-checks.json; biblio-shared-final-checks.json.
- captures/biblio-original-collections-portrait.png vs captures/biblio-finished-collections-portrait.png.
- Portrait 412×900 and 354×792; landscape 900×412 and 792×354; 1.3 font scale plus 24px top/bottom insets for popup stress. Desktop 1280×700 main layout checked. All visible local images loaded; no application-width horizontal overflow.
- Shared component final padding follows source 20px below 480px dialog width / 26px otherwise. Final stress captures and local Create & add actions repeated after this adjustment.
- A transient harness pass used unsupported Biblio query names; it was excluded and replaced by the correct width/height/scale/insetTop/insetBottom pass. Browser event history retains this provenance.
## QML implementation anchors
- BiblioBook.qml: detailHero/book body and existing readChoiceOverlay; preserve cover, Fraunces title/drop cap, full synopsis, source headings and source-row recipes.
- collections/AddToCollectionsPopup.qml, CollectionMembershipRow.qml, CollectionSearchField.qml, CollectionActionButton.qml: retain draft membership separate from committed collection membership, and source create/cancel lifecycle.
- Existing Theme.qml, LibraryButton.qml and KeyboardAction behavior remain authoritative. Phone reflow and popup scrolling are fitting changes; no new backend behavior is implied.
## Limits
- Illustrative data and the worker's explicitly disclosed frozen Cosmere artwork stand-in retained. No provider, download, native QML, app build, device or account operation tested.
- Tablet pass and final contact-sheet/index assembly are pending at this checkpoint.
