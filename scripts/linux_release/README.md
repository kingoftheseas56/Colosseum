# Linux 1.1.7 candidate pipeline

No application binary is included in this change. Windows source and build
commands are unchanged. The two broad push workflows exclude only the dedicated
linux-117-package branch so it runs the isolated Linux pipeline. No public release
is performed by this workflow.

Application source is fixed at `36498faa7cbcf1c6f47bdea042806b1aa8135f7e`.
The workflow checks out its own controller scripts separately, recording that
commit too. It never overlays scripts or patches onto the application checkout.
Both Player2 options stay OFF. Qt remains 6.11.1 linux_gcc_64, ECM 6.15.0,
MpvQt 1.2.0 and clang-tidy 20.1.2, using the tagged desktop-ci Linux authority.
Ubuntu packages other than the enforced clang-tidy version are distribution
resolved, as in that authority; full installed versions are recorded. A later
Ubuntu clang-tidy package version will fail the version gate, not be silently
accepted. CMake/Ninja/compiler are not version-pinned by the original workflow.

## Reuse on an authorized Ubuntu 24.04 x86_64 machine

Use a clean checkout of the fixed application SHA as SOURCE and this patch's
scripts from a separate controller directory. Install the exact Qt modules and
native packages listed in the workflow first. Supply new, empty output paths:

```sh
export SOURCE=/absolute/path/to/pristine-v1.1.7
export BUILD=/absolute/path/to/build
export DEPS=/absolute/path/to/dependencies
export OUT=/absolute/path/to/output
export EVIDENCE=/absolute/path/to/evidence
export QT_ROOT_DIR=/absolute/path/to/Qt/6.11.1/gcc_64
bash controller/scripts/linux_release/build.sh
bash controller/scripts/linux_release/test.sh
python3 controller/scripts/linux_release/package.py \
  --source "$SOURCE" --build "$BUILD" --qt "$QT_ROOT_DIR" \
  --mpvqt "$DEPS/mpvqt-install" --out "$OUT"
bash controller/scripts/linux_release/clean.sh
```

For an existing independently built application, invoke package.py with its
actual Release build and MpvQt prefix. Do not rerun build.sh unnecessarily.
The package script checks source cleanliness, source SHA, Qt version, Player2
and updater-test flags, and binary/QML manifest equality. The build must have
been produced from that source; package.py cannot prove an arbitrary binary's
provenance without its build evidence.

clean.sh needs Docker, sudo for evidence-directory ownership, ffmpeg, Python
3.12 on the host, and permission to fetch the clean Ubuntu base/runtime packages.
The qualifier itself uses Python's standard library and can also run inside an
independently provisioned clean Ubuntu 24.04 environment under Xvfb. It must run
as a non-root user with writable evidence paths and a 64x64 video fixture:

```sh
xvfb-run -a python3 qualify.py --appdir /opt/colosseum \
  --evidence /writable/evidence --fixture /fixture.mp4
```

## Outputs and gates

- `Colosseum-1.1.7-linux-x86_64-candidate.tar.gz`, SHA256 sidecar, and `AppRun`.
- Every tracked release file is preserved to avoid dropping runtime resources.
  Qt QML, WebEngine helpers/resources/locales, selected plugins, ffmpeg/ffprobe/bsdtar,
  MpvQt and transitive linked libraries are staged. SQLite is the only SQL
  driver. Library search paths are rewritten; builder paths must not resolve.
- The host supplies glibc, X11, Mesa/OpenGL drivers, fonts and CA certificates.
  The clean container has no Qt, MpvQt, libmpv or SDK installed. It mounts the
  extracted package, qualifier, fixture and evidence only.
- CTests run with Windows tests excluded and X11 tests separated. Tagged
  dependency/platform Python tests and the clang-tidy gate also run.
- Runtime checks independently require ELF closure, a PID-matched application
  bridge, a visible normal-entrypoint window, fresh isolated AppData, no detected
  QML load errors, nonempty live movie-catalog shelf results, and actual 64x64
  decoded-frame readiness plus a screenshot through the production Recent UI.
- `qualification.json` records each runtime result. `verdict.json` also requires
  build, tests, clang-tidy and packaging success. Failed or unavailable checks
  never produce a qualified verdict. Logs and candidate bytes, when available,
  upload even if later checks fail. A package's internal `qualified:false` is
  intentional: the separate verdict qualifies its checksum without rewriting it.

The catalog headless test makes a real live movie-catalog production request;
it does not qualify visual catalog UI, offline data, or all providers. WebEngine
assets are bundled, but reader runtime remains unverified.

This does not qualify all catalogs/providers, streaming sources, DRM, audible
audio, hardware graphics, Wayland, DVR or Stremio-service routes. Software Mesa
rendering is explicit. The normal WebEngine sandbox is not disabled; a host
sandbox restriction is a reported failure, not automatically worked around.
Qt Quick tests can contain upstream skips; the assembled-app decoded-frame
check is additional evidence and must pass independently. Direct bridge evidence
is not a Harness `completionReady` receipt.

The candidate includes upstream license texts and package versions, but this
workflow does not certify redistribution/license-source obligations or publish
a release. Review bundled dependencies before any public distribution.

## Activation and access evidence

The parent selected this fallback and the user approved creating/pushing the
dedicated branch and running Actions. The narrow push trigger starts only for
linux-117-package and changes to this workflow, its scripts or its test. No
default-branch workflow installation is needed for this push trigger. A manual
workflow_dispatch route may require the workflow on the default branch; do not
modify master to enable it. Repository-specific rules and workflow-token
permissions still apply.

Read-only connector checks returned authenticated user `kingoftheseas56` and
repository admin/push permissions. The installation listing did not expose
workflow-write or Actions-write grants. The local `gh auth status` reported its
GH_TOKEN invalid. Workflow-file write and dispatch are therefore UNVERIFIED,
not proven denied. No token was displayed or created. Available connector
metadata exposed file/commit/ref writes and run reads/reruns, but no dispatch
action; absence of that action does not establish an account permission denial.

## Offline preparation checks

```sh
python3 -m unittest -v tests.test_linux_release_qualification \
  tests.test_linux_runtime_dependency_gate \
  tests.test_linux_sqlite_plugin_staging_contract tests.test_linux_ctest_platform_contract
bash -n scripts/linux_release/build.sh scripts/linux_release/test.sh scripts/linux_release/clean.sh
python3 -m py_compile scripts/linux_release/package.py scripts/linux_release/qualify.py
```

These checks validate script logic/syntax, not a Qt build, package or runtime.
