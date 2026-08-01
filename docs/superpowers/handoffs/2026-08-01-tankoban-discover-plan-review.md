# Tankoban Discover Plan Self-Review

**Reviewer:** [Scoped helper (Codex), review]
**Definition of Done:** `docs/superpowers/specs/2026-08-01-tankoban-discover-design.md`, section 9
**Work reviewed:** `docs/superpowers/plans/2026-08-01-tankoban-discover.md` at commit `bc58a0e`

## Automated acceptance ledger

1. **MET** — Task 7 changes Tankoban to exact `Discover · Manga · Comics` order with Discover first/default.
2. **MET** — Tasks 3 and 7 put Theatre and Tankoban behind `DiscoverBrowser` and regression-test both wrappers.
3. **MET** — Task 3 preserves and reruns Theatre catalogue, pin, filter, paging, picker, and item-open harnesses.
4. **MET** — Tasks 3 and 7 test per-type in-session state restoration.
5. **MET** — Task 3 models one `filterGroup`/`filterKey` pair and tests replacement semantics.
6. **MET** — Task 6 locks Manga Genres/Demographics and Comics Genres/Publishers.
7. **MET** — Tasks 6 and 7 normalize series-only cards and route them to existing series doors.
8. **MET** — Task 8 specifies every See-all mapping and stale-filter fallback behavior.
9. **MET** — Task 6 delivers bundled Manga results before starting Jikan refresh.
10. **MET** — Tasks 3 and 6 use generation echo/fencing and test stale-response rejection.
11. **MET** — Tasks 3, 4, 5, and 6 define cursor paging plus canonical-identity dedupe.
12. **MET** — Task 5 pins deterministic house components and tests availability as a boost rather than a gate.
13. **MET** — Task 5 tests missing LOCG rank and caps metadata after redistribution.
14. **MET** — Tasks 4 and 6 expose an honest Trending-to-Popular fallback until two snapshots exist.
15. **MET** — Task 2 tests Explicit Content default and restart persistence through a temporary QSettings store.
16. **MET** — Tasks 1 and 9 test and wire the policy across Tankoban, Theatre, and Biblio.
17. **MET** — Tasks 1 and 9 carry explicit negative fixtures for Berserk, Game of Thrones, Ecchi, Mature Readers, ratings, violence, and horror.
18. **MET** — Tasks 1 and 9 explicitly preserve `ExtensionsStore` adult-install policy and test the boundary.
19. **MET** — Tasks 3 and 6 specify bundled-wall retention plus a quiet offline warning on network failure.
20. **MET** — Task 10 requires both native builds, the full focused acceptance runner, QML smoke, and integer `font.pixelSize` constraint.

## Eyes-on acceptance ledger

1. **MET** — Task 10 step 4 verifies Theatre/Tankoban tool parity after Tasks 3 and 7.
2. **MET** — Task 10 step 4 verifies the approved dense utilitarian treatment; the global constraints prohibit editorial decoration.
3. **MET** — Task 10 step 4 verifies Manga/Comics switching and independent restored state.
4. **MET** — Task 10 step 4 verifies catalogue/filter readability at the supported window size.
5. **MET** — Tasks 6 and 10 prevent and visually check post-interaction cover reordering.
6. **MET** — Tasks 7 and 10 verify Back restores the exact browse position.
7. **MET** — Tasks 2, 9, and 10 verify the exact Explicit Content wording and mainstream-work boundary.

## Edge review

- **Scope creep:** none found. The Settings surface and cross-world policy are explicit approved requirements; schema/query work is necessary for offline-first discovery and explicit rows.
- **Correctness and safety:** SQL catalogue/axis values are allowlisted, filter values are bound, live identity merges require MAL ids, no secrets are introduced, and Comic Vine/Metron remain outside the runtime graph.
- **Intent gaps:** none blocking. Stable filter keys, missing catalogue fallback, refresh interaction fencing, and untracked SQLite deployment artifacts are all stated explicitly.

APPROVE — every written acceptance item maps to a concrete task, test, or eyes-on gate with no unowned requirement.
