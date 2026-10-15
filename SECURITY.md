# Security Policy

## Supported versions

Security fixes are targeted at the latest published stable Colosseum release and current `master`. Older releases may receive fixes at maintainer discretion, but users should normally update to the latest stable build.

## Reporting a vulnerability

Please do not publish exploit details, credentials, private file paths, or sensitive logs in a public issue.

1. Open the repository's [Security](https://github.com/kingoftheseas56/Colosseum/security) page.
2. If GitHub shows **Report a vulnerability**, use that private reporting flow.
3. If private vulnerability reporting is not available, open a minimal public issue stating that you need a private security contact. Do not include the vulnerability details until a private channel is established.

A useful report includes the affected version/commit, Windows version, reproduction conditions, impact, and any mitigation you already tested. Share proof-of-concept material only through the private channel.

## What belongs here

Examples include:

- arbitrary code execution or unsafe process launch;
- signature/update verification bypasses;
- credential, token, recovery-key, or session exposure;
- path traversal or unintended local-file disclosure;
- unsafe handling of untrusted extension/provider data;
- a vulnerability in Colosseum's bundled or first-party service code.

Ordinary bugs, feature requests, build failures, and third-party source outages should use the normal issue/support routes instead.

## Response expectations

The maintainers will try to acknowledge a private report, reproduce it, assess affected versions, and coordinate a fix before public disclosure. There is no guaranteed response-time SLA.

Please allow reasonable time for triage and remediation before publishing details that would put users at risk.

## Scope notes

Colosseum is a client and does not host media. External APIs, sites, extensions, indexers, and datasets are independent systems. Security reports should describe a vulnerability in Colosseum or first-party Colosseum services rather than a policy or availability issue in an unrelated provider.
