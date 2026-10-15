# Contributing to Colosseum

Colosseum is a Windows-first Qt desktop media application. Contributions are most useful when they are focused, reproducible, and explicit about what was actually verified.

## Before you start

- Read the [README](README.md), especially **Known boundaries**, before treating unfinished behavior as a regression.
- Check [existing issues](https://github.com/kingoftheseas56/Colosseum/issues) before opening a duplicate.
- For a substantial feature or architectural change, open an issue first so the product and implementation direction can be agreed before code is written.
- For a security problem, do **not** open a public issue with exploit details. Follow [SECURITY.md](SECURITY.md).

## Development setup

The supported development path is Windows 10/11 with Visual Studio 2022 C++ Build Tools, CMake, Ninja, Qt 6.11.1 MSVC 2022 64-bit, MpvQt/libmpv, libtorrent/Boost/OpenSSL, and the runtime dependencies described in the build guide.

Start with [docs/build/windows.md](docs/build/windows.md). It uses neutral dependency prefixes rather than maintainer-specific machine paths.

## Working on a change

1. Create a focused branch in your fork or clone.
2. Keep the change as small as the requested behavior allows.
3. Match the existing C++/QML architecture and naming rather than introducing a parallel pattern without need.
4. Preserve unrelated work and avoid broad cleanup in the same pull request.
5. Update public documentation when the behavior or user-facing boundary changes.

## Verification

Run the narrowest relevant checks first, then widen only as needed. The repository's verification references are indexed from [docs/README.md](docs/README.md).

When you open a pull request, state the strongest level you actually reached:

- **Inspected**: source/diff was reviewed.
- **Built**: the affected target compiled successfully.
- **Tested**: name the exact tests or harnesses that passed.
- **Runtime-verified**: name the user journey or assembled-app behavior you exercised.

Compilation alone is not behavioral proof, and screenshots alone are not functional proof. If a relevant check cannot be run, say so rather than implying it passed.

For UI changes, include a screenshot or short capture when it materially helps review. For defects, include the reproduction path and the evidence that the same path no longer fails.

## Pull requests

A good pull request explains:

- what changed and why;
- the issue or user-visible problem it addresses;
- the affected surfaces;
- exact verification performed;
- known limitations or follow-up work.
## Public-repository hygiene

Do not commit credentials, tokens, private keys, personal logs, machine-specific user paths, generated build output, caches, or one-off scratch files. Use neutral fixture identities such as `TestUser` when a test needs a user-shaped path.

Keep release/signing material out of ordinary pull requests unless the change is specifically part of the release tooling and contains no private signing input.

## Community expectations

Participation in the project is governed by the [Code of Conduct](CODE_OF_CONDUCT.md). For usage and troubleshooting questions, follow [SUPPORT.md](SUPPORT.md). Security-sensitive reports belong in the private route described by [SECURITY.md](SECURITY.md).
