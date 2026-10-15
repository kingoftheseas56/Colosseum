# Colosseum Codex Memory

This is compact product hot state for coding inside the Colosseum repository. It is not a project history, roadmap, or substitute for the live source. Current source, tests, build files, and runtime evidence win for mutable implementation facts.

## Product model

- Colosseum is one unified media application for manga, western comics, ebooks, audiobooks, movies, television, anime, and local media.
- The five worlds are **Tankoban, Biblio, Theatre, Universes, and Vault**.
- Tankoban is the manga/comics world.
- Biblio is the ebooks/audiobooks world.
- Theatre is the movies/shows/anime world.
- Universes is the fourth world: structured, content-rich franchise experiences that can span media.
- Vault is the fifth world: user-owned/local media. It remains a world even though it sits outside the normal world picker.
- Tributes are separate handcrafted franchise experiences such as atlases, interactive showcases, 3D scenes, or other stylized pieces. They are not a world and do not replace Universes.
- A franchise may have both a Universe and a Tribute.

## Shared product concepts

- Home is the shared shell.
- Continue is shared presentation for progress, while each medium keeps its own progress semantics.
- Collection is a manual cross-world library and is separate from progress and local file existence.
- Downloads is a shared surface, while each owning backend remains responsible for item actions.
- Open books, readers, and video surfaces are Colosseum sessions surfaced through the taskbar.
- Vault and Downloads are distinct: Vault indexes media the user already owns; Downloads tracks media acquired through Colosseum.
- Search remains medium-aware rather than one flattened universal search.

## Architecture

- Current desktop stack: Qt 6.11.1, QML, C++.
- The application UI direction is QML-only. Do not introduce a parallel web frontend unless the task explicitly requires a web surface.
- QML owns presentation and interaction. Durable state, lifecycle, persistence, transport, identity, business rules, threading, and authoritative decisions belong in C++ where practical.
- Colosseum should remain self-sustaining for core local use. Third-party accounts and remote services are optional enhancements.
- Colosseum owns canonical user state. Provider/tracker state may enrich, import, export, or synchronize it without becoming silent authority.
- Prefer local-first operation and useful cached/offline behavior where practical.
- Providers are replaceable planes, not the product architecture.
- Strengthen existing state owners before creating parallel stores, caches, identity systems, history systems, players, resolvers, databases, or extension mechanisms.
- Study real reference products/source and mature upstream mechanisms before inventing new machinery. Borrow mechanisms, not entire foreign architectures.

## Current major product facts

- Theatre playback defaults to MpvQt/libmpv.
- Player 2 remains experimental and is not the default playback path.
- Extensions are Stremio-compatible and medium-aware. Theatre can consume compatible torrent/direct stream results; Tankoban and Biblio keep native acquisition behavior.
- External provider credentials remain provider-owned and do not become canonical Colosseum account state.
- Colosseum accounts are optional for ordinary local use.
- Optional tracker integrations include Trakt, SIMKL, and MyAnimeList.
- Portable sync covers selected personal Colosseum state; machine-specific paths and local media remain local.
- Watch Party lives inside Theatre/player rather than as a separate product.
- Personalization is first-party and can vary by world.
- Windows 10/11 is the current stable packaged desktop platform. Linux has an alpha tester path and source builds exist for Linux/macOS.
- Vinyl is under development and is not part of the current five-world runtime.

## Coding stance

- Choose architecture by product ceiling first, then simplicity when ceilings are genuinely equal.
- Do not reduce the intended end state merely to make implementation easier.
- Prefer verified, honest partial coverage over fabricated completeness or misleading fallbacks.
- Preserve user ownership, portability, and existing Colosseum concepts.
