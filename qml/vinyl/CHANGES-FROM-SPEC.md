# Changes from spec

- 2026-10-07: Visual review is deferred until the full prototype is ready. Hemanth: “I'll give feedback after you finish it brother, so go at it”

- 2026-10-07: Complete both screens before the review loop. Hemanth: “nah brother build the entire thing, we can go back and forth from there”

At the time of those earlier entries, no visual acceptance had been claimed. The assignment explicitly marks all Thriller metadata as unverified fixtures.

- 2026-10-07: Added a visible song Back action and distinct album index SVG icons. Hemanth: “there is no back from the song page. and the index sidebar in the album pages needs icons brother. just like the main app”

- 2026-10-07: Credits now keeps a fixed, viewport-bounded height and scrolls manually; HUD artwork and unused surface open the song without intercepting transport controls. Hemanth: “when there are too many credits in the credits sections, we don't increase the pop us size” and “when we click the bottom hud controller when in album view it should open the song view”.

## World prototype, 2026-10-08
- Hemanth approved the requested small data-driven visibility change in VinylAlbumSections.qml so snapshot albums omit absent sections while original fixtures retain them. Asked: “Can I make a small data-driven visibility change in that Vinyl-owned file?” His words: “yes” (Sentinel_9ba8e239e2388191bb5d189ba78d92f0).

- 2026-10-07 21:57 UTC: Hemanth gave broad visual approval of the cloud QML previews: “I’ve seen a lot of the QML previews from your cloud desktop they look perfect to me” (Sentinel_eb6ca3acc74c8191af93b8b7aa6a6ac4). This records approval of the preview appearance, not explicit completion of every per-slice walkthrough, Windows replay or adoption.

## Repair 1 (GLM on Windows, from Claude's 2026-10-08 receiving review)
No Hemanth word involved; sources are `agents/REVIEW-CLAUDE-2026-10-08-vinyl-world-dots-prototype.md` and the repair brief `agents/brief-glm-vinyl-world-repair-1.md`. Behavioural deviations from the spec text, made for 1280x720 screens:
- A tab's first entry parks the page at the tab bar instead of the carousel top; later entries return to where the listener left that tab, open-list detours included (review F2/F6 and the brief's Jobs 2 and 7). The carousel stays in the page, one flick up.
- While Home shows the picker, the page parks with the tab bar at the top of the page area and the picker owns one viewport: its question/search/Skip/Done header is pinned while the artists scroll inside (spec 4.1 "always available").
- The lens filter menu clamps its height to the visible page above the player bar and scrolls inside itself (review F1).

