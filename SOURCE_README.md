# Argon Colosseum source-reading snapshot

Canonical source: kingoftheseas56/Colosseum master at 536f1be5fceb23484092b58a424b4f79e4533e05
Generated branch: argon-source-master. Never merge this disposable export
into production master. It is not a buildable app or a release branch.

To discuss the Linux TV OS, read these first:
1. docs/ARGON_COLOSSEUM_OS_CONTEXT.md — Arc 66 accepted Linux/TV OS plan.
2. docs/ARGON_SESSION_ARCHITECTURE.md — current hybrid session lifecycle.
3. native/SessionStore.h, qml/Main.qml and qml/Taskbar.qml.
4. qml/feria/FeriaSessionDeck.qml and qml/feria/FeriaQtWebEngine.qml.

Current code has loader-dehydrated readers, a warm mpv host, and retained
browser sessions with QtWebEngine freezing/exit discard paths. These are
app-level resources, not kernel-managed processes. No OS image or boot proof
is established by this snapshot.

Gemini GitHub import budget is 100 MB; this branch uses a more conservative
80,000,000-byte / 4,500-file target. SOURCE_POLICY.json explains filters;
SOURCE_MANIFEST.json supplies paths, hashes and omissions. Universe raster
art, replay/test fixture trees, local agent instructions, and CI workflow
definitions are intentionally excluded. No automatic Gemini refresh.
