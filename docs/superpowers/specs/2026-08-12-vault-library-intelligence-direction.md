# Vault Library Intelligence — design direction (ratified 2026-08-12)

> **Status: DESIGN DIRECTION, not an implementation plan.** Ruled by Hemanth 2026-08-12 after a
> code comparison against Jellyfin. Scoping into slices goes through `brotherhood-brainstorming`
> → `brotherhood-writing-plans` when Hemanth calls for it. Recorded verbatim-in-substance so the
> ruling survives the chat.
>
> **The one-line law:** *Vault should steal Jellyfin's library intelligence, not Jellyfin's
> server architecture.* DLNA is planned because Hemanth explicitly wants it; accounts, remote
> administration, server/client architecture, transcoding infrastructure, and device profiles do
> not infect Vault. Vault remains Colosseum's local ownership/identity layer.

## The target architecture (Hemanth's shape)

Disk
→ Vault scanner understands what physically exists
→ resolver understands file/folder relationships
→ technical analyzer understands the actual files
→ identity providers propose canonical identities
→ Vault refuses ambiguity or accepts explicit user authority
→ Vault reconciles moves / offline drives / duplicates
→ **Theatre / Tankoban / Biblio owns the media object**

DLNA sits off the side: Vault-owned physical media → DLNA exposure. Never Jellyfin Server.

## Hemanth's priority front (his ordering, verbatim)

Technical media inspection + multi-version/source grouping + external subtitles + better
TV/episode resolution + move/duplicate detection + metadata-provider seam + independent
refresh/re-identify + identification diagnostics. "Those make Vault dramatically smarter without
changing what Vault *is*."

## The 35 evaluated features and their rulings

1. **Movie/TV folder understanding** (`Breaking Bad/Season 01/S01E01.mkv` → show→season→episode,
   not just "belongs to Breaking Bad"; filesystem understanding WITHOUT a second Theatre DB) — **yes.**
2. **Multi-version movies** (three Blade Runner files = one owned movie with versions; Theatre
   picks which to play) — **absolutely.**
3. **Extras** (trailers/interviews/deleted scenes/featurettes shelve UNDER the canonical movie,
   never as mystery movies) — **yes.**
4. **Better episode naming** (`Show.Name.2x04.1080p.WEB-DL.mkv`, multi-episode files, weird
   layouts; filesystem interpretation, not metadata authority) — **yes, high priority.**
5. **Specials** (Season 00/OVAs; design against OUR anime model, not blind Jellyfin inheritance) —
   **yes, designed for anime too.**
6. **Local metadata files** (NFO-style; don't discard what the folder already says). Authority
   order is law: **explicit user decision > strong local identity > canonical Colosseum catalog
   match > weaker guesses** — **yes.**
7. **Local artwork** (poster.jpg/folder.jpg/fanart.jpg known; Colosseum artwork may still win as
   product decision) — **yes.**
8. **Multiple artwork types** (poster/backdrop/logo/banner/thumb/art/disc/screenshots; Vault
   exposes what exists, worlds decide usage) — **yes.**
9. **Refresh separate from scanning** — four independent jobs: **Scan** (what exists), **Identify**
   (what it is), **Refresh metadata** (newer info), **Enrich** (art/descriptions). Independently
   runnable; IMDb changing never pretends the drive changed — **absolutely.**
10. **Replace one metadata source without rescanning** ("re-identify these comics", never "rescan
    8 TB") — **yes.**
11. **Metadata providers as interchangeable pieces** (each source answers a standard question —
    steal the SEAM, not the architecture; Vault stays the authority arbiter; VaultIdentifier must
    not become God) — **yes, before adding more providers.**
12. **Explicit metadata locking** ("this identification is correct — stop changing it"; manual
    identity survives future refreshes/heuristic changes) — **yes.**
13. **Per-field locking** (keep title, refresh poster) — **later** (power-user territory).
14. **Media information** — what OUR COPY is (codec, resolution, bitrate, HDR/DV, audio codecs +
    languages, subtitle languages, channels, duration, container; per-medium equivalents).
    Theatre knows *Blade Runner*; Vault knows "2160p HEVC DV copy with TrueHD Atmos on drive D:" —
    **extremely yes.**
15. **Multiple media sources under one identity** (one Theatre object, N local files; folds into
    Theatre's source picker: Local 4K / Local 1080p / Torrentio / addons — local = the best
    source, already ours) — **absolutely.**
16. **External subtitles** (`Movie.en.srt`, `Movie.ja.ass`, forced tracks belong to the video;
    the player receives the whole local package) — **absolutely.**
17. **External audio / associated files** (relationships understood, per-medium; Biblio
    audiobook/text pairing benefits) — **yes, per-medium.**
18. **Ignore rules** (.DS_Store/samples/fragments = "deliberately ignored", first-class, better
    diagnostics) — **yes.**
19. **`.ignore`-style user control** ("never index this directory") — **yes.**
20. **Better mixed-folder handling** (explain what was found and where it goes: "1 Theatre object,
    1 Biblio, 1 Tankoban, 14 ignored" — build on our existing model, don't copy Jellyfin) — **yes.**
21. **Duplicate detection** (two copies of the same movie ≠ two movies; both physical copies under
    one canonical identity) — **yes.**
22. **File fingerprinting** (answer "same physical file I knew yesterday, elsewhere?" without
    hashing 80 GB remuxes every scan) — **yes, carefully — hashing is expensive.**
23. **Better rename/move detection** ("Alien moved", never "deleted + new Alien appeared";
    progress and manual decisions survive; keep pushing existing reconciliation) — **absolutely.**
24. **Scheduled scans** — **useful, not fundamental** (secondary to watching + startup/background
    reconciliation; we are a desktop app).
25. **Per-root scanning rules** (movies-only root, manga root, never-auto-identify root; root
    config grows scanning/identity policy) — **yes.**
26. **Library health** (drives online/away, files known/identified/ambiguous/changed/broken/
    deliberately-unidentified — never a mysterious spinner) — **very yes.**
27. **Explain why something wasn't identified** (first-class: "parsed as X", "3 candidates,
    refused to guess", "you chose Un-identify" — the UI face of our conservative identity law) —
    **extremely yes.**
28. **Manual identification/search** (auto refuses → user searches → picks → authoritative) —
    **yes** (17B shipped the core; surface everywhere).
29. **Bulk identification** (Unidentified/Ambiguous/Needs-attention queue with bulk tools; bulk
    NEVER means "silently guess harder") — **yes.**
30. **Collections** — **integrate, don't duplicate**: no Vault Collections; Vault feeds facts
    (owned locally / available offline / 4K local / on drive X / away) to Colosseum's real
    Collection.
31. **Rich search over local media** ("local 4K movies", "files with Japanese audio") — **feed the
    global search**, no separate Vault search box.
32. **Chapter information** (container chapters, audiobook chapters, EPUB nav preserved and
    exposed to the right world/player) — **yes.**
33. **Embedded artwork/metadata** (local evidence understood before the internet; not above
    explicit canonical identity) — **yes.**
34. **Music** — **blocked by product decision.** No Colosseum music world exists; Vault must not
    invent the ontology. If a music world ships, Vault understands local music at Jellyfin depth.
35. **Photos** — **not until Colosseum has a photo concept.** Same reasoning.

## How tonight's live findings map onto this direction (Agent 0 note)

The 2026-08-12 shelf-pass diagnoses are the direction's bugs arriving early: the scan/identify/
refresh conflation (#9-10) is exactly the boot-republish trigger gap; the Wire-times-two duplicate
is #15/#21 (multi-source under one identity) unbuilt; remove-root belongs to #25-26 (root policy +
library health); "Loki Season 1" unmatched is #4 (episode/naming resolution); the poisoned
Jurassic identity is the case #12's explicit locking + #27's diagnostics are designed to make
impossible-to-misread. The near-term Luna fixes (boot republish, mtime scale, watcher deepening)
are corrective slices consistent with this direction, not part of it.

`[Agent 0 (Claude), design-direction capture — ruled by Hemanth 2026-08-12]`
