> Publication update: read `PUBLICATION-AUTHORITY.md` first. Earlier preparation-status and permission statements are historical; the current user starting prompt controls the Cloud session.

# Claude Code Cloud: environment and delivery setup

Prepared 2026-10-20. This is Anthropic-hosted Claude Code Cloud, not the local CLI, Remote Control, a self-hosted runner or a connection to Hemanth's laptops.

## Choose this configuration

- Select **Cloud > Colosseum** in Claude Desktop. The name is a label, not evidence of configuration. Edit its settings before the implementation session.
- Attach GitHub repository `kingoftheseas56/Colosseum` and select a dedicated Android candidate branch after the source publication gate below. Never start this assignment on `master` or overwrite an unrelated branch.
- Model: Opus 5.5. Work as one implementation owner; Codex owns receiving review.
- Custom network access: allow the actual dependency hosts required by the pinned setup scripts. Start with GitHub/source and release hosts, Google Android SDK/Maven, Qt archives, Boost archives, Maven/Gradle and Ubuntu/PyPI. Relevant hostnames include `github.com`, `api.github.com`, `raw.githubusercontent.com`, `codeload.github.com`, `release-assets.githubusercontent.com`, `objects.githubusercontent.com`, `dl.google.com`, `maven.google.com`, `repo.maven.apache.org`, `services.gradle.org`, `downloads.gradle.org`, `download.qt.io`, `archives.boost.io`, `pypi.org`, `files.pythonhosted.org`, `archive.ubuntu.com`, and `security.ubuntu.com`. Resolve an actual denied mirror/redirect precisely rather than guessing that this list is exhaustive or disabling platform guards.
- GitHub proxy authorization is separate from ordinary domain reachability. Attach `arvidn/libtorrent` and `openssl/openssl` if required for the existing pinned source downloads. The donor pristine pin is also supplied as a small Git bundle, so Nuvio source does not require laptop access. Preserve the exact response if the platform refuses repository access.
- No production secrets, original debug/release signing keys, SSH keys, BDC credentials, account tokens or user profiles are needed.
- Suggested environment variables: `COLOSSEUM_CLOUD_TASK=claude-android-cloud-013-20261020`, `BASH_DEFAULT_TIMEOUT_MS=3600000`, `BASH_MAX_TIMEOUT_MS=7200000`. Confirm the active harness honors these; do not mistake a backgrounded command for a completed build.
- Setup script field: use `cloud-environment-setup.sh`. It provisions only lightweight VM prerequisites and records the outcome. It must not perform a whole Qt/NDK/dependency/APK build in the startup hook. Claude installs and qualifies the heavyweight pinned Android toolchain in checkpoint C0 after the session begins, where logs and failures remain inspectable.

## Source publication gate (prepared, not performed)

The full recovery source archive is `ANDROID-SOURCE-BASELINE.zip`, SHA256 `920d3e72f4e6d641def87d683ed63e4ae9d82f0bfe66f9190cfe9274543205bf`, 219332211 bytes. It preserves 4495 app files and 149 donor files, including unpublished working changes and runtime assets. It excludes build outputs/caches, machine configuration, private continuity and credential files, with exact exclusions recorded in `SOURCE-MANIFEST.json`. The Cloud Git checkout has its own `CLOUD-SOURCE-MANIFEST.json`: 3383 unchanged app files differ from the Windows backup only by verified checkout line endings; dirty/new source remains exact. These distinct manifests prevent a silent normalization or a mass newline-only import diff.

For GitHub-based Cloud, use the smaller `GITHUB-BASELINE-DELTA.zip` prepared beside it. It contains the selected app working changes and `cloud-handoff/android-013/` with the donor snapshot, pristine pin bundle, exact manifests, instructions, fixtures and receiving evidence. It is intended to be applied only on an isolated checkout of upstream app commit `fdd23f09e8b631f5c165e84d969895c99f520a28`. Its publication procedure is in `PUBLISH-BASELINE.md`.

Raw Nokia captures/session logs and the unrelated emulator-smoke ZIP remain in the full local receiving packet; they are not proposed for public GitHub publication. The GitHub payload has the named independent receiving reviews and synthetic fixture. Private supplemental evidence can be provided only through an actually supported, approved Cloud artifact route if needed; Claude can reproduce fresh Cloud observations without treating an unavailable local path as a dependency.

**Neither baseline branch nor Cloud configuration has been published or executed by preparing this packet.** Earlier source/push restrictions have not been silently lifted. Codex must review the concrete publication payload and obtain Hemanth's authorization for the exact isolated GitHub source branch before sending it. This is source transport for review, not main integration. A locally existing ZIP is not a Cloud delivery receipt.

After authorized publication, record the actual remote URL, branch and SHA; start Cloud on that exact SHA. Run the included source verifier before any implementation. If the Cloud UI cannot select the needed branch, do not fall back to `master`: first establish the supported branch-selection/source-delivery route.

## Recovery and output transport qualification

Before changing product code, establish that a source checkpoint can leave the VM through a supported route. Qualify the APK return route as well: the previous APK was about 261 MB, so a tiny-text download does not establish that a large binary can be returned. Git branch push is usable only after explicit authorization of the worker branch; do not commit APKs to Git. Alternative downloadable artifacts count only when the platform actually exposes them; a VM path or Markdown filename alone is not a download. No automatic GitHub Release, Actions, external upload service or public source hosting is authorized here.

Codex/Hemanth must be able to retrieve the checkpoint and verify its SHA256 and restoration manifest. Until that transport round trip exists, C0 can inspect/provision/reproduce baseline but must not accumulate new unpublished implementation. Do not repeat Dots' publication-denial loop: retain exact operation/reason/partial outcome, stop that operation, and report the smallest missing authorization or supported route. An explicit denial is not permission to reroute the same action.

## Environment facts versus assumptions

No Claude cloud VM, Qt installation, emulator/KVM capability, repository link, package upload, GitHub push or output download has yet been qualified for this task. Any historical Dots emulator result belongs to a different cloud environment. Do not inherit its device, ABI, speed, source revision or accessibility-crash verdict.

Official references checked on 2026-10-20:
- https://code.claude.com/docs/en/cloud-environments
- https://code.claude.com/docs/en/claude-code-on-the-web

Default has no project setup script or environment variables. Project-specific Cloud configuration is appropriate here. Repository attachment, branch content and environment configuration are three separate checks. Setup scripts run before Claude starts, have roughly a five-minute cache window, and are not the place for a lengthy native build.

## Current cloud capacity qualification

Anthropic's documentation checked on 2026-10-20 lists approximate hosted limits of 4 vCPUs, 16 GB RAM and 30 GB disk, subject to change. C0 must measure actual free space/RAM/CPU and budget Qt host+target kits, SDK/NDK, native dependencies, build outputs, emulator image and checkpoint/APK storage before installing. Start builds with two jobs and retain one current candidate rather than every generated staging tree. Install prebuilt matching Qt kits, not a whole Qt source/WebEngine build. If a required ABI/runtime or disk budget is impossible in this VM, report the exact Cloud prerequisite; do not connect to a laptop or silently weaken the target. These documented limits are not a measurement of the new session.
