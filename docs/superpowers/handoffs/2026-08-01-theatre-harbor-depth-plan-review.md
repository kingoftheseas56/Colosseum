# Theatre Deep Catalogue Plan Self-Review

**Reviewer:** [Scoped helper (Codex), review]

**Definition of Done:** `docs/superpowers/specs/2026-08-01-theatre-harbor-depth-catalogue-design.md`, section 15

**Work reviewed:** `docs/superpowers/plans/2026-08-01-theatre-harbor-depth-catalogue.md`

1. **MET** — Tasks 1, 3, 4, and 9 define and render all three approved inventories, assert Top 10 first, and place GenreMosaic last.
2. **MET** — Global constraints plus Tasks 1, 6, and 9 explicitly reject heroes, progress rows, awards, accounts/API keys, and row blurbs.
3. **MET** — Task 6 pins the shared card to Discover's hover-only `★ <value>` presentation and tests that keyboard focus does not expose the hover reveal.
4. **MET** — Tasks 3, 4, and 7 define source-correct house/extension pins, infinite paging, and forwarding into the existing Theatre detail route.
5. **MET** — Task 1 tests six deterministic daily Movie shelves; Task 4 and the locked inventories keep Shows and Anime stable.
6. **MET** — Tasks 2 and 4 add paged MAL queries and explicitly test local-first paint with Jikan then Kitsu outcomes.
7. **MET** — Task 3 limits Movies/Shows to Cinemeta, bounds enrichment, rejects missing facts, and the global constraint bans TMDB/Trakt/API keys.
8. **MET** — Task 5 tests recognized contextual services, unknown `From Your Extensions` rows, disabled/required-extra exclusion, installed order, and stale removal.
9. **MET** — Task 8 specifies and tests move up/down, hide/show, rename/reset name, page reset, migration, and independent per-tab QSettings persistence.
10. **MET** — Tasks 3, 4, and 9 filter before ranking/paging and run the existing explicit policy/preference harnesses, including the Berserk/Game of Thrones boundary.
11. **MET** — Tasks 3, 4, 7, and 9 test progressive publication, generations, bounded four-worker enrichment, request coalescing, collapsed empty rows, and retained local Anime results.
12. **MET** — Tasks 5, 6, 7, 9, and 10 name Discover, extension, genre/detail, landing, search, Anime, and explicit-content regression gates.

**Scope creep:** None. Advanced filters, awards, accounts, landing widgets, Tankoban/Biblio behavior, TMDB/Trakt, drag-and-drop, and new detail pages remain excluded.

**Correctness and safety:** The plan binds/allowlists native Anime queries, uses canonical IDs, rejects stale generations, bounds Cinemeta concurrency, retains exact explicit classifications, and calls out the already-dirty `native/CMakeLists.txt` with partial staging.

**Intent gaps:** None blocking. The spec explicitly resolves unsupported `In Theaters`/`Coming Soon` claims and treats the mockups as non-literal references overridden by Hemanth's final blurb/rating corrections.

APPROVE — every written acceptance item has a concrete implementation task, failing-first harness, and verification command without violating the keyless or master-only constraints.
