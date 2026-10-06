# Colosseum Agent Instructions

## Authority
- Follow the user's current task first.
- At the start of a fresh session, read `MEMORY.md` completely. It is intentionally compact product hot state, not a history archive.
- Use live source, tests, build files, and runtime evidence for implementation facts.
- Treat plans, handoffs, recaps, and Git history as optional context; read them only when the task requires them.
- Do not preload documentation, skills, or historical context "just in case."

## Product invariants
- Colosseum is one product with five worlds: Tankoban, Biblio, Theatre, Universes, and Vault.
- Universes is the fourth world. Vault is the fifth world even though it is not part of the normal world picker.
- Tributes are separate stylized franchise experiences, not a sixth world and not a replacement for the structured Universe surface.
- Core local use must not depend on third-party accounts or services. External providers, trackers, and accounts are optional enhancements.
- Colosseum owns canonical state for identity, library, history, progress, and preferences; external services may enrich or synchronize it but do not silently become authority.
- Prefer local-first behavior and graceful cached/offline operation where practical.
- Keep providers replaceable and medium-aware rather than allowing one provider to define the product architecture.
- Before creating a new store, cache, resolver, history system, identity system, player, database, or extension mechanism, check whether Colosseum already owns that truth and extend the existing owner when practical.
- Do not lower the intended product end state merely because a shortcut is easier to implement.

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
- After completing and verifying a task, commit and push only your own work by default, unless Hemanth asks otherwise. Leave unrelated changes out of the commit.
- Do not reset, clean, stash, rewrite history, or discard unrelated changes to simplify a task.
- Do not commit credentials, tokens, machine-specific paths, local databases, caches, logs, screenshots, or build output unless they are intentional repository artifacts.

## Verification
- Hemanth testing is an optional acceptance ritual, never a prerequisite for completing otherwise verified work. Offer a short, relevant checklist when useful; do not require Hemanth to pilot the app.
- For recent implementations, record outstanding human/live-provider checks and their actual results. Skipped or unrecorded checks remain unverified; automated, agent-runtime, and Hemanth-tested evidence stay distinct.
- Start with the smallest meaningful check for the changed surface.
- Broaden testing only when affected dependencies, failures, risk, or the task itself justify it.
- Do not run full builds, full suites, runtime harnesses, or broad searches by default.
- Inspect the final diff for the files changed.
- Keep authored, inspected, built, tested, runtime-verified, committed, pushed, and released states distinct.
- State exactly what was verified and any material gap.
