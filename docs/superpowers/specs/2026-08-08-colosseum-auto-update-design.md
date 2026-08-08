# Colosseum Auto-Update Design

**Date:** 2026-08-08

**Status:** Secure updater foundation implemented on master; living-gallery visual completion approved

**Target:** First updater-enabled stable release, then every later stable GitHub Release

**Visual direction:** Approved living release gallery, captured as the user-visible contract in section 2

## 1. Objective

Colosseum shall tell users when a newer stable version is available, provide a dedicated Update
page that downloads and presents that release, verify the downloaded installer, replace the
installed application safely, and reopen on the new version without disturbing the user's
library, downloads, progress, settings, or cached release chronicle.

Publishing a stable GitHub Release is the release event. Drafts, prereleases, tags without a
published release, and releases without a valid signed update manifest must never notify users.

## 2. User-visible contract

### 2.1 Colosseum taskbar

The Colosseum taskbar gains a permanent Update icon. It opens the Update page in every state.

When a newer release is available, the icon gains the approved monochrome notification treatment:
a small silver-white badge plus a restrained pulse. Availability must remain legible when animation
is disabled, so the badge and accessible label carry the meaning independently of motion. The pulse
stops after the user opens the Update page; the badge remains until that release is installed or
superseded. No notification meaning may depend on gold, another accent colour, or motion alone.

The taskbar must never signal a release that Colosseum cannot verify or install.

### 2.2 Update page

The approved visual concept is canonical: a full Colosseum page, not a native dialog, dashboard,
card wall, or external updater window. It is a **living release gallery** with two simultaneous
purposes:

1. carry the current update from discovery through download, verification, and restart; and
2. remain the permanent illustrated chronicle of the latest installed release after the update.

The page renders these states:

| State | Primary message | Primary action |
|---|---|---|
| Checking | Checking for updates | None |
| Up to date | Everything is up to date | Check for updates |
| Available | Colosseum [version] is ready | Download update |
| Downloading | Updating to [version] | Pause download |
| Paused/interrupted | Update paused | Resume download |
| Verifying | Verifying the update | None |
| Ready | Ready to enter [version] | Restart and update |
| Installing | Colosseum is updating | None; the app exits promptly |
| Recoverable error | The update could not finish | Retry |
| Verification failure | This update could not be verified | Check again; never execute |

The release presentation remains visible throughout Available, Downloading, Paused, Verifying,
Ready, and Up to date. The screenshot is the stage: one verified real Colosseum screenshot fills
the page behind a deliberate monochrome wash and directional black gradients. Large Fraunces
chapter typography crosses the image; Inter carries only compact metadata and controls. There are
no cards, floating glass panels, marketing subtitle under the page title, coloured accents, emoji,
or decorative infographics competing with the real application imagery.

Each release is an ordered gallery of one to eight chapters. A chapter contains a short section
name, a display title, one concise body line or paragraph, and optional verified artwork. Numbered
chapter controls (`01`, `02`, ...) select a chapter; **Next chapter** advances and wraps. The active
chapter is unambiguous without relying on colour. Missing artwork falls back to the local captured
motion background without collapsing the typography or controls.

The bottom status rail is persistent and stateful. In Up to date it carries **Everything is up to
date** at left and **Check for updates** at right. During download it carries the state at left,
the exact downloaded/total bytes plus percentage and a thin progress track in the centre, and
**Pause download** at right. Verifying, Ready, errors, and manual-update states reuse the same rail
rather than inserting a new panel. A smoothed time estimate may appear only when stable; the page
must not promise an exact time when it is not.

Colosseum 1.0 is the canonical post-update/reference composition because it is the first stable
release: `Reader`, `Discover`, `Biblio`, `Theatre`, and `The house` are the five reference chapters,
using real Colosseum captures. The same local QML template must render a future `Updating to 1.1`
state from signed release data; the 1.0 reference is not permission to special-case the renderer to
one version.

Chapter changes use a restrained image crossfade and no automatic carousel. Reduced-motion mode
removes the crossfade and taskbar pulse while preserving chapter controls, badge, progress text,
and every state label. The page must remain usable at 1280x720 and the normal desktop display size.

The page also shows the installed version, latest checked version, last successful check time,
and a low-emphasis link to the full GitHub release notes.

### 2.3 User control

Colosseum checks automatically, but never spends hundreds of megabytes without consent. The user
starts the installer download with **Download update**. Once verification succeeds, installation
starts only with **Restart and update**.

There are no forced updates, surprise restarts, background silent downloads, or mandatory release
channels in this design.

## 3. Bootstrap limitation

An already-installed binary cannot acquire an updater retroactively. Colosseum 1.0 has no update
client, so 1.0 users must manually install the first updater-enabled release once. That installer
must preserve their data and establish the new version identity and update machinery. From that
release onward, stable updates can arrive inside Colosseum.

The release notes and download page for the bootstrap release must state this plainly. Replacing or
silently mutating the existing 1.0 GitHub asset is forbidden.

## 4. Architecture

### 4.1 Native update service

A focused C++ `UpdateService` owns update policy, network requests, release parsing, download
resumption, integrity/authenticity verification, persisted state, and installer handoff. QML only
paints the service state and invokes its commands.

Its QML-facing contract includes:

- installed version, latest version, publication date, release title, summary, highlights, and
  cached artwork URLs;
- state, notification visibility, progress bytes, total bytes, percentage, and time estimate;
- last successful check and a user-readable error category;
- commands to check, download, cancel, resume, discard, and restart into the update.

The service uses one dedicated `QNetworkAccessManager`. Checks and downloads remain asynchronous;
no GitHub request, disk hash, or large-file write may block the GUI thread.

### 4.2 Application version identity

The installed application gains one authoritative semantic version:

- CMake declares the release version;
- the same value is compiled into the executable;
- `QCoreApplication::applicationVersion()` exposes it at runtime;
- the NSIS DisplayVersion, installer filename, Git tag, manifest version, and release title derive
  from that same release input.

The canonical release version is three-component `X.Y.Z`; the Git tag adds a leading `v`. The page
may omit a trailing `.0` for editorial display (`1.1.0` becomes `1.1`), but comparison and asset
identity always use the full canonical value. Malformed, non-increasing, or mismatched versions are
rejected rather than guessed. The existing `v1.0` release is a pre-updater legacy exception, not a
format accepted by future manifests.

### 4.3 Check cadence

Installed stable builds check after the shell is usable, never on the critical first-paint path.
They check again every six hours while running. A persisted timestamp prevents relaunch loops from
hammering GitHub. **Check again** bypasses the time gate.

Requests use GitHub's public latest-release endpoint with the documented media type, API version,
and a Colosseum user agent. No account, token, analytics identity, or telemetry is required. ETag
and `If-None-Match` are retained so unchanged releases return cheaply.

Developer/source-tree runs do not auto-check unless explicitly enabled at launch; a development
build must not accidentally install a public release over its checkout.

### 4.4 Release discovery and trust

GitHub is the transport and publication signal, not the sole trust root. Each updater-aware release
contains these assets:

- `Colosseum-<version>-setup.exe`;
- `colosseum-update-v1.json`;
- `colosseum-update-v1.json.sig`; and
- optional release artwork referenced by the manifest.

The JSON manifest contains the schema version, release version/tag, publication metadata,
installer filename, byte size, SHA-256, minimum updater version, structured presentation content,
and hashes for every remote artwork asset. It is signed with an offline-controlled Ed25519 private
key. Colosseum embeds only the public key and verifies the signature before trusting any manifest
field.

The signature covers the exact UTF-8 manifest bytes, with no BOM and no runtime reserialization.
Release tooling writes those bytes deterministically; the client verifies the raw bytes before
parsing JSON. This avoids two parsers disagreeing about what a "canonical" JSON document meant.

The private signing key must never enter the repository or installer. Release tooling may read it
from an explicit secure path or protected release environment. Key rotation requires a separately
designed transition that trusts both old and new public keys for a bounded period.

The updater also compares the manifest installer digest with GitHub's release-asset SHA-256 digest
when GitHub supplies one. A mismatch blocks the update. Installer redirects are restricted to
HTTPS and safe redirect policy; the final asset must originate from GitHub's release delivery
chain.

Remote presentation data is inert. The manifest may supply text, numbers, layout template IDs, and
hashed PNG/WebP artwork asset names. The client resolves those names only against assets on the same
verified GitHub Release; it never follows an artwork URL supplied by presentation data. It may not
supply QML, JavaScript, HTML, shaders, fonts, commands, automatically opened URLs, or any other
executable presentation content. The optional full-notes link must resolve to the same GitHub
Release and opens only after an explicit user click. Local QML owns every visual template.

### 4.5 Release presentation cache

After signature verification, Colosseum stores the manifest and verified artwork in the application
data update cache. That cache lets the latest release chronicle survive offline and remain visible
after installation. A newly installed version can adopt only a cache whose signed version matches
its own compiled version.

If no valid cache exists, the page shows the locally bundled release title/summary until the next
successful check. Update metadata never becomes a hard dependency for launching Colosseum.

### 4.6 Download and resume

The installer downloads into the application data update cache, never into the installation tree.
Incoming bytes stream directly to a `.part` file. Colosseum persists the release version, expected
size and hash, URL identity, ETag, and received byte count beside it.

On retry or relaunch, the service requests the remaining range. If the server does not honor the
range or the remote identity changed, Colosseum discards the partial file and restarts cleanly. A
completed file must match the signed byte size and SHA-256 before it becomes Ready. Failed or
cancelled downloads never become executable.

Only one installer version is retained. A newer valid release supersedes and safely removes an
older partial update after the service has confirmed the paths remain inside its dedicated update
cache.

### 4.7 Safe installation and rollback

The existing per-user NSIS installer remains the packaging system. Update mode extends it rather
than replacing it.

On **Restart and update**, Colosseum launches the already-verified installer from the update cache
with explicit update mode, current process ID, installation path, restart request, and log path,
then quits. The installer waits for that exact process to exit before touching the payload.

Update mode performs a side-by-side replacement:

1. extract the new payload into a validated sibling staging directory;
2. verify required payload sentinels before swapping;
3. rename the current payload to a bounded backup;
4. move the new payload into the canonical install directory;
5. refresh shortcuts and the per-user uninstall registry entry;
6. launch the new executable with an update-result marker; and
7. retain the previous payload until the new version records one successful shell-ready boot.

If extraction or swapping fails, the installer restores the prior payload and relaunches the old
version with a rollback result. The Update page then offers Retry and points to a friendly error;
the technical log remains available for diagnosis without being shown as the primary interface.

User data must live outside the replaceable program payload before update mode is enabled. The
bootstrap release must audit and migrate any remaining 0.1-era user-writable folders from the
installation tree into the application's data location before declaring itself update-ready.
Uninstall behavior is a separate concern and must not be invoked during update.

## 5. Release publishing contract

The existing packaging/publishing scripts become one deterministic release command or workflow.
For a version `X.Y.Z`, it must:

1. require a clean, committed release tree and a matching `vX.Y.Z` tag;
2. build and package from that exact commit, not the daily working build;
3. run the release verification suite;
4. build `Colosseum-X.Y.Z-setup.exe`;
5. generate the structured update manifest and artwork hashes;
6. sign the canonical manifest bytes;
7. create or update a **draft** GitHub Release and upload all assets;
8. verify the uploaded names, sizes, GitHub digests, manifest signature, and downloadability;
9. prove an update from the previous stable installer on a clean Windows test account; and
10. publish the release only after every gate is green.

Publication is atomic from the updater's perspective: users see nothing while the release is a
draft. If a published release lacks a valid signed manifest or exact installer asset, installed
clients ignore it and keep the last valid chronicle.

The manifest presentation model supports a small, intentional vocabulary rather than arbitrary
layout. In the living gallery, each accepted highlight becomes an ordered chapter:

- release eyebrow, title, summary, and full-notes URL;
- feature chapter: world/section, title, body, optional artwork;
- statistic chapter: label, value, context, optional artwork;
- before/after chapter: two short captions and up to two images; and
- milestone chapter: title, body, optional number, optional artwork.

This is sufficient for stylized text and infographics while keeping the renderer consistent with
Colosseum.

## 6. Failure behavior

- A background check failure is quiet: no taskbar badge, modal, or startup interruption.
- The Update page shows the last valid chronicle plus a low-emphasis offline/check-failed status.
- A download failure preserves a valid resumable partial file and offers Retry.
- A changed ETag, invalid range response, wrong size, wrong hash, bad signature, malformed manifest,
  or unexpected asset blocks execution.
- Verification failures delete the untrusted installer after path validation and retain enough
  non-sensitive diagnostics to explain the block.
- Running out of disk space is detected before download when the expected size is known and again
  before side-by-side installation, accounting for the new payload plus rollback backup.
- A release that requires a newer updater than the installed one shows an honest manual-update
  path to the GitHub release rather than attempting an unsafe install.
- A rollback returns the user to the working version and records the failed target so Colosseum
  does not retry automatically.

## 7. Testing and verification

### 7.1 Deterministic tests

- strict semantic-version parsing and ordering, including malformed and downgrade cases;
- exact-byte manifest signature verification before parsing, schema parsing, and public-key
  rejection;
- exact asset selection, size/digest comparison, and safe redirect rules;
- update state-machine transitions and notification persistence;
- presentation-model allowlist and rejection of executable/unknown content;
- progress/time-estimate smoothing and zero/unknown-size behavior;
- download cancellation, range resume, changed ETag, ignored range, truncation, corruption, and
  disk-full handling using a local HTTP fixture;
- persisted cache adoption only when signed version equals the running version; and
- development-build auto-check suppression.

### 7.2 QML tests

- taskbar Update icon is always reachable;
- badge and pulse appear only for a newer verified release;
- opening the page marks the release seen without clearing availability;
- every page state exposes the correct action and never overlaps unsafe actions;
- release cards remain visible during download and after Up to date;
- reduced-motion mode retains a non-animated badge and accessible status; and
- long release text, missing optional artwork, narrow windows, and offline cache remain usable.

### 7.3 Installer integration matrix

- fresh install of the updater-enabled release;
- bootstrap upgrade from public 1.0 with libraries, progress, settings, and downloads retained;
- updater-enabled release N to N+1 through the Update page;
- download interruption and app restart resume;
- corrupted installer and corrupted/signature-invalid manifest rejection;
- app still running while installer starts;
- insufficient disk space before download and before swap;
- extraction failure, swap failure, and forced rollback;
- successful relaunch into N+1 and later cleanup of the N backup; and
- update followed by normal uninstall, proving update never invoked destructive uninstall logic.

The release is not complete until a clean-machine test installs the previous public version,
creates representative user state in all three worlds, updates through the real GitHub draft
assets, relaunches, and proves that state survived.

## 8. Privacy and accessibility

The updater sends ordinary unauthenticated HTTPS requests to GitHub. It sends no library contents,
media history, machine identifier, account identity, or usage telemetry.

Every state and action has a screen-reader name. Progress is communicated by text as well as a
visual bar. Notification meaning never depends on animation or color alone. Keyboard navigation,
focus restoration, Escape/back behavior, and reduced-motion behavior follow the rest of the
Colosseum shell.

## 9. Non-goals

- delta/binary patch updates;
- beta, nightly, or multiple release channels;
- forced security updates or version expiry;
- updates while Colosseum is closed;
- replacing NSIS or moving to the Qt Installer Framework;
- executing remote presentation code;
- self-updating source checkouts or developer builds by default; and
- automatic operating-system code-signing procurement.

Windows Authenticode signing remains desirable for publisher reputation and SmartScreen. It can be
added to the release pipeline when a certificate is available, but the signed manifest and hash
verification are mandatory independently.

## 10. Definition of done

This design is delivered when:

- the permanent taskbar icon uses the approved monochrome badge/pulse and remains meaningful with
  reduced motion;
- the Update page matches the approved living release gallery: real screenshot stage, monochrome
  treatment, Fraunces chapter typography, numbered chapter navigation, and no card/dashboard UI;
- Up to date keeps the latest chronicle plus **Everything is up to date** and **Check for updates**;
- Downloading keeps the gallery visible plus target version, byte counts, percentage, progress,
  and **Pause download** in the persistent bottom rail;
- a published stable GitHub Release becomes visible without blocking startup;
- the user explicitly starts the resumable download and explicitly starts installation;
- all executable and presentation assets are authenticated before use;
- the updater survives offline use, interruption, corruption, and installer failure honestly;
- a successful update reopens Colosseum on the new compiled version;
- user libraries, downloads, progress, settings, and cached release chronicle survive;
- the previous working payload is recoverable until the new shell reaches a successful boot;
- the full deterministic, QML, installer, and clean-machine release gates pass; and
- publishing the next stable release requires no hand-editing of version values or asset metadata.
