# Colosseum App Adapter

This file is the active app-level instruction surface for Colosseum. It describes how to work on the live repository without duplicating Brotherhood governance.

## Authority and inheritance

Use this order for Colosseum work:

1. Hemanth's current instruction.
2. Live Colosseum source, tests, build files, and runtime evidence for mutable implementation facts.
3. This app adapter and `CONTEXT.md` when present for Colosseum-specific procedure and vocabulary.
4. Brotherhood `../governance/GOVERNANCE.md` and `../governance/CONTRACTS.md` when this checkout is nested under the Brotherhood workspace.
5. Current product doctrine/specifications for intended product behavior.
6. Historical plans, handoffs, recaps, and Git history.

A standalone Colosseum clone may not have the Brotherhood parent directory. Do not fail bootstrap because a parent file is absent.

`agents/` in this repository is currently a handoff/evidence/mock area. It is **not** a second governance tree. Do not look for app-local `STATUS.md`, `routes.yml`, `GOVERNANCE.md`, `VERSIONS.md`, or `CONTRACTS.md` unless a future adapter explicitly creates and names them.

When the Brotherhood parent is present and a mainline role is assigned, read `../agents/agent-N/IDENTITY.md` for that role's durable ownership. Current routing is: Agent 0 coordination/architecture, Agent 1 manga+comics/Tankoban, Agent 2 books+Biblio, Agent 3 video player, Agent 4 Theatre/stream+torrent acquisition, Agent 5 library UX+theme. Hemanth's current assignment overrides the default routing.

## Reality anchors

The live implementation has three primary anchors:

- Native application entry and service wiring: `native/main.cpp`.
- QML application root: `qml/Main.qml`.
- Native build and harness graph: `native/CMakeLists.txt`.

The current UI stack is Qt 6 Quick/QML with Qt Quick Controls/Layout. The live `qml/` tree currently has no Kirigami imports. Kirigami may remain a historical design reference, but **"build only with Kirigami" is not an implementation rule.**

Colosseum is intentionally mixed while migration continues: native C++ owns substantial state/services, while QML/JavaScript still contains provider and network glue. The direction "QML paints/interacts; C++ owns durable machinery" is a design direction, not permission to pretend the current tree already obeys it everywhere.

Atlas currently indexes the C++ structure, including live symbols such as `AccountController`, `ProgressStore`, `CollectionStore`, `BiblioCatalog`, `Colosseum::Update::UpdateService`, and the `MangaTankoban` namespace. Read the actual source ranges before changing behavior.

## Atlas Scout workflow

`docs/encyclopedia/` is Colosseum's local internal agent knowledge base. It may contain hand-written subsystem guides, generated indexes, and Atlas-derived snapshots.

Before changing an unfamiliar subsystem:

1. Read the matching guide under `docs/encyclopedia/` when it exists.
2. Use Atlas Scout for C++ symbol definitions, references, callers, implementations, dependency neighborhoods, and structural orientation.
3. Read the exact source ranges the structural result points to.
4. Re-index after substantial code movement before trusting old graph results.
5. For QML/JavaScript, supplement Atlas with direct source inspection and text search. An empty Atlas result is never proof that no QML path exists.

Atlas is navigation evidence, not implementation authority.

### Local Atlas setup

Atlas Scout is installed in WSL at `~/.local/bin/atlas-scout`. Keep the cache outside the Windows working tree:

```sh
~/.local/bin/atlas-scout index "$(pwd)" --cache-dir ~/.cache/atlas-scout/colosseum
~/.local/bin/atlas-scout schema --workspace "$(pwd)" --cache-dir ~/.cache/atlas-scout/colosseum
~/.local/bin/atlas-scout doctor --workspace "$(pwd)" --cache-dir ~/.cache/atlas-scout/colosseum
```

## Encyclopedia privacy

`docs/encyclopedia/` is intentionally local-only and ignored by Git. A fresh clone may not have it. Do not publish it, claim Git can restore it, or treat its absence as missing source documentation.

After changing a documented subsystem, update the matching local guide/index when that encyclopedia is present. The source and tests remain the final implementation truth.

## Product doctrine boundary

When nested under Brotherhood, `../agents/COLOSSEUM_DOCTRINE.md` is product/design doctrine only. It must not assert stale implementation facts, provider choices, toolkit mandates, or build commands over the current source.

When present, `CONTEXT.md` owns shared terminology such as Collection, Library, Progress, reading lanes, and `seriesId`.

## Git discipline

The normal working branch is `master`. Do not create a worktree, side branch, active separate clone, or separate built instance without Hemanth's explicit permission.

The repository may be heavily dirty from concurrent work. Preserve unrelated changes. Never use destructive Git cleanup, reset, stash, or broad staging to make the tree look clean.

Rule 28 does not grant every agent commit authority. Follow the active Brotherhood commit contract when that governance is present.

## Verification

Before declaring a change complete:

- inspect the final diff for the files you touched;
- use the nearest relevant tests/harnesses under `tests/` and the build wiring in `native/CMakeLists.txt`;
- run QML-specific inspection/lint when QML changed and the tool is available;
- keep build/test/runtime claims separate;
- search for equivalent occurrences before claiming a defect class is fully fixed.

Do not infer runtime success from a source edit, a generated encyclopedia, or an Atlas relationship graph.
