# Colosseum documentation

This is the public documentation front door for Colosseum. The [root README](../README.md) is the product overview; this page routes users and contributors to the maintained docs behind it.

## Start here

- [Download the latest release](https://github.com/kingoftheseas56/Colosseum/releases/latest)
- [Windows build from source](build/windows.md)
- [macOS build from source](build/macos.md)
- [Contributing](../CONTRIBUTING.md)
- [Support](../SUPPORT.md)
- [Security policy](../SECURITY.md)
- [Code of Conduct](../CODE_OF_CONDUCT.md)

## Release notes

- [1.1.4](release-notes/v1.1.4.md)
- [1.1.3](release-notes/v1.1.3.md)
- [1.1.2](release-notes/v1.1.2.md)
- [1.1.1](release-notes/v1.1.1.md)
- [1.1.0](release-notes/v1.1.0.md)
- [1.0](release-notes/v1.0.md)

GitHub's [Releases](https://github.com/kingoftheseas56/Colosseum/releases) page is the authority for published installers and release assets.

## Development and verification

- [Test verification](colosseum-test-verification.md)
- [Lanista assembled-app verification](colosseum-lanista-verification.md)
- [Verification tooling map](colosseum-verification-tooling-map.md)
- [Watch Party relay deployment](../server/watchparty-relay/DEPLOYMENT.md)
- [Account-service deployment](../server/account-service/DEPLOYMENT.md)

These references describe current repository tooling, but a passing isolated harness is not automatically proof that an installed release works end to end. Contribution and release notes should name the exact verification level actually observed.

## Design, research, and historical material

The wider `docs/` tree contains research, mockups, plans, prototypes, archived decisions, and other supporting artifacts. Those files are useful evidence for how Colosseum evolved, but they are not all stable user documentation or current product contracts.

When a design note conflicts with current source, tests, release notes, or an explicit current product boundary, use the current evidence.

Local encyclopedia material under `docs/encyclopedia/`, when present in a working environment, is maintainer/agent navigation material rather than a public documentation entry point.

## Documentation changes

If a change alters user-visible behavior, install/build steps, a known boundary, security expectations, or contribution workflow, update the matching public documentation in the same pull request.
