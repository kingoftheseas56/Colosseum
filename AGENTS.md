# Colosseum repository instructions

These instructions apply to contributors and automation working in this repository.

## Source of truth

Use this order when evidence disagrees:

1. The current task, issue, or maintainer instruction.
2. Live source, tests, build files, and runtime evidence for implementation facts.
3. This file and `docs/terminology.md` for repository procedure and shared vocabulary.
4. Current public documentation and ADRs for intended behavior.
5. Git history and older plans as historical context only.

Read the implementation that owns a behavior before changing it. Do not infer current behavior from an old plan, generated index, or stale test name.

## Reality anchors

The primary application anchors are:

- native application entry and service wiring: `native/main.cpp`;
- QML application root: `qml/Main.qml`;
- native build and harness graph: `native/CMakeLists.txt`;
- verification surfaces: `tests/`.

Colosseum uses Qt 6 Quick/QML with substantial native C++ state and services. QML/JavaScript still owns some provider and network glue. Treat "QML paints/interacts; C++ owns durable machinery" as a design direction, not a description of every current path.

## Working rules

- Preserve unrelated working-tree changes. Inspect status and the relevant diff before editing.
- Prefer the smallest complete change that follows an existing repository pattern.
- Trace direct callers, consumers, build wiring, and relevant tests before changing shared behavior.
- Do not introduce user-specific absolute paths, credentials, tokens, or machine-local service endpoints into tracked files.
- Keep generated output, caches, local databases, screenshots, and diagnostic logs out of source control unless they are intentional fixtures or public documentation.
- Use `docs/build/windows.md` for the supported Windows source-build path.
- Use `docs/terminology.md` when Collection, Library, Progress, reading lanes, or `seriesId` terminology matters.

## Git discipline

The normal branch is `master`. Do not reset, clean, stash, rewrite history, or discard unrelated changes to simplify a task. Do not commit or push unless the current task explicitly authorizes it.

## Verification

Before declaring work complete:

- inspect the final diff for every file you touched;
- run the narrowest relevant test or harness, then the surrounding checks needed for the touched surface;
- use the build wiring in `native/CMakeLists.txt` when compilation evidence is required;
- run QML-specific inspection or lint when QML changed and the tool is available;
- search for equivalent occurrences before claiming a defect class is completely fixed;
- keep authored, compiled, tested, and runtime-verified states distinct.

A successful edit is not runtime proof, and a successful compile is not behavioral proof.
