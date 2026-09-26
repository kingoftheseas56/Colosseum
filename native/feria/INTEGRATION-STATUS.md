# Feria integration status

Feria replaces Vinyl in Colosseum's world selector. `qml/Main.qml` loads
`qml/feria/FeriaWorld.qml`; `native/main.cpp` exposes the native discovery
composition before the main QML engine loads. The source is compiled through
`native/feria/CMakeLists.txt`.

Feria's service tiles and provider actions use locally stored provider marks
from `qml/feria/brand-logos/`. Sources are recorded in that folder's
`SOURCES.md`. The marks do not require network requests at runtime.

The discovery and destination layers are integrated. Provider URLs are
resolved, but the production WebView2 host is **not** connected to this QML
world yet. The provider page in Feria says this plainly and does not claim
that sign-in or playback is working inside the app. The live Feria root no
longer records elapsed host-window time as a watch session.

Feria now uses the shared four-world header. Its account button emits into
`Main.qml`'s real `AccountFlyout` and `AccountCenter`; the old Feria account
mock is disabled in the live route. Hemanth's combined Colosseum + streaming
service watch-time view remains pending. Before adding a service subtotal,
connect provider playback and collect verified play/pause/progress evidence
with stable title and service identity. Keep that subtotal separate from
Colosseum's existing `ProfileActivity.watchSeconds`, then show a combined
total with both components explained. Sample sessions and time spent in a
provider window are not watch evidence.

The 2026-09-25 MSVC `colosseum` build completed successfully. Offscreen QML
tests instantiated Feria and decoded all 21 brand marks and five utility
glyphs. A visible in-app journey and provider playback have not been verified.

The checkout is shared and contains unrelated in-progress work. These
changes were left uncommitted rather than stage another lane's changes.
