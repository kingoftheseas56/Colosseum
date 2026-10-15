# The Universe Page Law

**Ratified by Hemanth, 2026-07-13. Binding on every agent, AI, and future contributor who
touches a universe page. The short form: a universe page is a curated set of
metadata-provider series ids — never a name search.**

This law was earned, not designed. The first universe expansion (2026-07-12) shipped a page
that put a yaoi anthology behind Lord of the Rings' Read button because it trusted a name
search. The repair — canon-over-search, id-pins, live verification — became the
architecture, and this wake (2026-07-13) made it explicit doctrine.

## The rules

1. **Every entry is a provider identity, not a string.**
   - Screen (films/shows): a Cinemeta id-pin — `{ t: "Title", id: "tt…" }` in
     `qml/Universes.js`. Same-name impostors are the norm, not the exception: two shows
     both named "Avatar: The Last Airbender", four films answering to "Dune", the 2003
     "Clone Wars" outranked by its 2008 namesake, Ms. Marvel and She-Hulk sitting on
     *adjacent* catalog ids. A bare name string is only legal when a live search proved it
     unambiguous.
   - Manga/anime: a MAL / AniList / Kitsu identity — an id where the loader supports it, or
     a live-verified exact query. AniList trap: same-title NOVELS and one-shots outrank the
     manga; send `format`, prefer ids.
   - Western comics: a pinned GetComics archive — `comics: { tag: "<slug>", tagId: <id> }`.
     Resolve slug-first; a WordPress tag search floods exact tags out of its own results.
   - Books: exact verified titles (Apple Books resolution). Watch US/UK splits ("The Tower
     of Swallows" vs "…of the Swallow") and ampersands ("Fire & Blood", never "and").

2. **The metadata id is the gate — never the release date.** If no house provider carries
   an identity for a work, the work does not enter, no matter how famous. If a provider
   carries an id for unreleased work, it enters and wears the small gold **UPCOMING** tag
   (`probeUpcoming` in `qml/SagaApi.js` — a same-year entry is probed once against the full
   meta; Cinemeta's own `status: "Upcoming"` is a valid verdict when no date exists).

3. **Providers dress the slots; curation owns them.** `slotByCanon` fills a curated slot
   only from a matching hit — by id when pinned, by normalized title otherwise. An
   unmatched slot stays **empty**. It is never filled with a fuzzy stand-in. A universe
   page can therefore never grow a title its curation didn't name.

4. **Middlemen get ladders.** Any provider that proxies another (Jikan proxies MAL) WILL
   have outages the origin doesn't share. The lane must fall to an independent second well
   (Jikan→Kitsu, precedent `9bc65cc`/`6aa50e3`) and return to the first well when it heals.
   The id is what survives the outage; the page never blanks.

5. **Every pin is verified live before it lands.** Research reports, memories, and
   Wikipedia are leads; the provider's own answer is the only evidence. Verification is the
   commission, not a step ("did you double check with wikipedia?" — and then check the
   provider too).

## Where the law is enforced

- `qml/Universes.js` — the ONE curation point; carries this law in its header.
- `tests/test_universe_expansion_p0.ps1` — needles the pins, the doors, the UPCOMING
  machinery, and fails the build if the law's artifacts vanish.
- `tests/saga_canon_harness.qml` — proves slotByCanon's empty-slot behavior headlessly.
- Per-template p0s + lazy-Loader load gates — one per universe template.
