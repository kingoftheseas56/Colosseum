# Custom Five Universe Archive — 2026-07

These five bespoke universe pages were retired together on 2026-07-19. Hemanth will
restore them only when the larger collection of roughly twenty custom-made universe
pages is ready to return as one complete feature.

| Universe | Archived page | Original live path |
|---|---|---|
| One Piece | `qml/OnePieceUniversePage.qml` | `qml/OnePieceUniversePage.qml` |
| Marvel Cinematic Universe | `qml/CinematicPage.qml` | `qml/CinematicPage.qml` |
| Dragon Ball | `qml/DragonBallUniversePage.qml` | `qml/DragonBallUniversePage.qml` |
| Cosmere | `qml/CosmereUniversePage.qml` | `qml/CosmereUniversePage.qml` |
| Weekly Shonen Jump | `qml/MagazineUniversePage.qml` | `qml/MagazineUniversePage.qml` |

The files are preserved verbatim. Their shared dependencies remain under live `qml/` as
dormant restoration material: `Universes.js`, provider/API modules, shared controls, and
generic universe templates.

## Restore as one complete collection

1. Move the five files from this archive's `qml/` directory back to their original paths.
2. Restore the archived tests to `tests/` and update any paths changed by the archive move.
3. Restore Home's universe hero/Hall door and Main's universe routing/loaders.
4. Restore the artwork warmer only for pages that still need it.
5. Add the remaining custom pages and their exact category routes.
6. Run every restored universe contract/load harness, the full build, and an eyes-on Home smoke.

Do not restore only a subset: this archive exists so the universe collection returns as a
deliberate, sufficiently complete feature.
