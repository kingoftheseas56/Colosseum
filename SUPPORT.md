# Support

Colosseum is developed in the open. The fastest route depends on what you are trying to report.

## Before opening an issue

1. Check the [latest release](https://github.com/kingoftheseas56/Colosseum/releases/latest) and update if practical.
2. Read the README's [Known boundaries](README.md#known-boundaries).
3. Search [existing issues](https://github.com/kingoftheseas56/Colosseum/issues) for the same symptom.
4. If you built from source, follow the [Windows build guide](docs/build/windows.md) and include your dependency/configuration details.

## Where to go

- **Bug or regression:** use the [bug-report issue form](https://github.com/kingoftheseas56/Colosseum/issues/new?template=bug_report.yml).
- **Feature request:** use the [feature-request issue form](https://github.com/kingoftheseas56/Colosseum/issues/new?template=feature_request.yml).
- **Build/setup problem:** open an issue after checking [docs/build/windows.md](docs/build/windows.md).
- **Security vulnerability:** follow [SECURITY.md](SECURITY.md) and do not post exploit details publicly.
- **Contribution question:** read [CONTRIBUTING.md](CONTRIBUTING.md).

Accounts and cloud sync are currently unavailable in public releases because no production account-service endpoint is deployed. That is a known product boundary, not a local setup failure.

## What to include with a bug

Please include the Colosseum version or commit, Windows version, what you clicked, what you expected, what happened instead, and whether the problem is repeatable.

Colosseum writes a rolling log at:

```text
%APPDATA%\Brotherhood\Colosseum\logs\colosseum.log
```

Attach the relevant tail when it helps diagnosis, but review it first. Redact personal file paths, usernames, tokens, credentials, private URLs, or other sensitive data.

Screenshots or short recordings are useful for visual/navigation defects. For crashes or hangs, describe the last successful action before the failure.

## Third-party sources

Colosseum integrates with external APIs, sites, extensions, indexers, and datasets that can change independently. Provider downtime or content availability is not controlled by Colosseum. Reports are still useful when Colosseum handles a provider change badly, such as crashing, hanging, showing a misleading state, or failing to fall back cleanly.

Colosseum does not host media. Support does not cover bypassing access controls or obtaining content you do not have the right to access.

## Response expectations

This is an independently maintained project and there is no guaranteed support SLA. Clear reproduction steps and logs make issues much easier to act on.
