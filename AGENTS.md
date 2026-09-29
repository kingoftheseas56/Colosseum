# Colosseum Agent Instructions

## Authority
- Follow the user's current task first.
- Use live source, tests, build files, and runtime evidence for implementation facts.
- Treat plans, handoffs, recaps, memory, and Git history as optional context; read them only when the task requires them.
- Do not preload documentation, skills, or historical context "just in case."

## Task discipline
- Treat the current conversation as one bounded task.
- Inspect only enough of the repository to understand the surface being changed.
- Use the fewest tool calls that materially improve correctness.
- Do not turn a small task into a repository audit.
- Make the smallest complete change that satisfies the request.
- Do not add cleanup, refactors, migrations, documentation, or tests outside the requested scope unless correctness requires them.
- Continue through reversible in-scope work without asking for permission.
- Stop when the requested outcome is complete.

## Repository map
- `qml/`: Qt Quick/QML application UI and remaining QML/JavaScript glue.
- `native/`: C++ application entry, services, state, playback, readers, networking, and native integrations.
- `tests/`: native, QML, JavaScript, integration, and regression tests.
- `scripts/`: repository automation and quality utilities.
- `tools/`: development, diagnostics, previews, and Colosseum Harness tooling.
- `server/`: Colosseum server-side services.

## Reality anchors
- Application entry and native wiring: `native/main.cpp`.
- QML application root: `qml/Main.qml`.
- Native build graph: `native/CMakeLists.txt`.
- Test build graph: `tests/CMakeLists.txt`.
- Preserve the current QML application UI direction; do not introduce a parallel web UI unless the task explicitly requires it.
- Prefer C++ for durable backend/business machinery and QML for presentation and interaction, while respecting current ownership where migration has not occurred.

## Read only when relevant
- Windows build/setup: `docs/build/windows.md`.
- Repository terminology: `docs/terminology.md`.
- Verification references: `docs/README.md`.
- Security-sensitive work: `SECURITY.md`.
- Harness-backed runtime work: `tools/colosseum-harness/README.md`.

## Working-tree safety
- Preserve unrelated modified, staged, and untracked work.
- Do not reset, clean, stash, rewrite history, or discard unrelated changes to simplify a task.
- Do not commit credentials, tokens, machine-specific paths, local databases, caches, logs, screenshots, or build output unless they are intentional repository artifacts.

## Verification
- Start with the smallest meaningful check for the changed surface.
- Broaden testing only when affected dependencies, failures, risk, or the task itself justify it.
- Do not run full builds, full suites, runtime harnesses, or broad searches by default.
- Inspect the final diff for the files changed.
- Keep authored, inspected, built, tested, runtime-verified, committed, pushed, and released states distinct.
- State exactly what was verified and any material gap.
