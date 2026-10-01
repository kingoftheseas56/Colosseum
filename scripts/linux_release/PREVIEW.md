# Colosseum 1.1.7 Linux PREVIEW

Experimental Linux package derived from Colosseum 1.1.7 source
`36498faa7cbcf1c6f47bdea042806b1aa8135f7e` plus the reviewed Linux patch
series recorded, with hashes, in `PACKAGE.json`. The application version and
upstream tag remain 1.1.7. This is not a fully supported Linux release.

## Start here

Extract the archive and run `./AppRun` from its extracted directory. Target:
Ubuntu 24.04 x86_64, X11 or Wayland, glibc 2.39, working Mesa/OpenGL drivers, fonts and CA
certificates. Use local-only mode. Package creation alone is not qualification;
check the matching archive checksum and separate build/test/runtime verdict.

## Unsupported credential features

- Persistent Colosseum account sessions and account-backed sync are unsupported.
  The production account credential store uses Windows Credential Manager; on
  Linux it reports unavailable and refuses writes. New create/sign-in requests
  are rejected before contacting the server when storage is unavailable. Do not use
  account creation or sign-in as a supported preview workflow.
- Tracker credential persistence and authenticated SIMKL connections are
  unsupported. The production tracker vault is Windows-only. SIMKL availability
  gates disable connection and prevent authenticated requests without a usable
  secure store. Other providers do not gain Linux authentication support from
  this package.
- Stremio account credential persistence is also Windows-backed and unsupported.
  No bundled Stremio service or Stremio-service route is qualified.
- No plaintext credential fallback is provided. Do not work around these limits
  by putting tokens in settings, environment files, databases or scripts. A
  bundled libsecret library does not mean a secure Linux backend is implemented.

Account restoration explains that secure storage is unsupported and directs
users to local-only mode. SIMKL shows an unavailable provider/disabled Connect;
a direct unsupported connection attempt reports that secure credential storage
is unsupported in this preview. These are capability limits, not instructions
to repair Windows or repeatedly retry Linux sign-in.

## Runtime scope and limitations

- X11 and Wayland plugins are bundled. Consult the matching clean-runtime
  evidence for Xvfb and headless Weston results; this does not qualify all
  compositors or physical desktops. Player2 options are OFF.
- No standalone mpv/DVR, DRM, audible audio or hardware-graphics qualification.
- Software-Mesa decoded-frame and live movie-catalog checks, if passed in the
  matching evidence, cover only those paths. They do not establish all-provider,
  visual catalogue UI, offline-data or real-device playback support.
- WebEngine resources are bundled; reader runtime is unverified. The normal
  WebEngine sandbox remains enabled. Host sandbox failures remain failures.
- Dependency license/source redistribution obligations require separate review.

## Reading test results honestly

Unsupported credential features are not passing feature tests. A failing test
that exercises them remains a recorded failure, even though those features are
outside this preview's supported scope. Windows-only exclusions and upstream
skips must be reported separately from executed tests.

Known failed baseline: Actions run `36736450789` recorded failures in
`colosseum.qttest.account_attachment_runtime` (six cases reaching unavailable
Linux secure storage) and `colosseum.qttest.tracker_lifecycle` (two SIMKL cadence
cases with irrelevant credential-availability preconditions). The six successful-
account runtime cases remain unsupported and their failures remain visible.
The cadence cases exercise a credential-independent scheduler: patch 0008 removes
only those availability preconditions and retains all timing assertions. Their
rerun results, including new unavailable-vault guards, remain authoritative.

Separately, that run failed `account_core` (a missing fixture handoff),
`account_attachment_coordinator` (two Windows-specific read-only receipt-lock
fixtures) and `keyboard_key_events` (scroll position). Patch 0004 addresses the
first two fixture issues; both targets passed in run `36744998735`. Patch 0006
fixes the keyboard test's missing layout-readiness wait, and patch 0007 fixes
per-connection framing in the datastore test server used by `core_sync_adapters`.
Their native reruns remain authoritative. An X11 diagnostic does not erase an
offscreen failure.
Use newer exact-run evidence to update these outcomes; build and runtime results
for newer candidate bytes cannot be inferred from that baseline.

Core build, CTest, clang-tidy, packaging and clean-host runtime qualification are
independent gates. Pending, blocked, failed or never-run checks are not passes.
Consult the exact build's logs, `qualification.json` and `verdict.json` for test
names and outcomes; this notice does not certify that any particular run passed.
`PACKAGE.json` intentionally starts with `qualified: false` and `released: false`.
A later external verdict must identify the archive checksum it actually tested.
No gate is suppressed or reclassified merely because this is a preview.
