# Windows receiving: manual 1.1.8 → 1.1.9 and owned lifecycle

Status at authoring: **Windows execution NOT RUN. PowerShell 7.4.6 syntax parsing and five
normalizer helper checks, seven baseline-classifier checks, and three synthetic-process
recovery checks PASS on Linux.**
Python source-contract tests do not qualify Windows runtime behavior. Linux component results
are recorded separately. Only Preflight's actual Windows evidence can qualify
this receiving run.

## Boundaries and prerequisites

- Run in a **new disposable Windows account or VM**, with no personal libraries,
  account credentials, or pre-existing daily Colosseum profile. Take a VM snapshot.
  A custom installation directory alone is insufficient isolation: the real NSIS
  installer writes normal HKCU uninstall metadata and desktop/start-menu links.
- Have authentic 1.1.8 and assigned 1.1.9 installers, a reviewed candidate source/
  build receipt, Windows Lanista, Python 3.10+, ffmpeg, PowerShell 5.1+ and a real
  graphical session. Record Windows build, architecture, display size/DPI and GPU.
- Review `windows_receiving.ps1` before execution. It launches only the explicit
  executable, checks its expected hash and `--version`, uses a newly generated
  pipe, verifies ping PID and real get-state roots, and preserves a generated
  profile tag in a session checkpoint. It never invokes an installer/uninstaller.
- The baseline executable must support the selected Lanista selectors. Confirmed
  unsupported selectors or absent artifacts are prerequisites to resolve before
  the journey; never remove the decisive identity/page assertions. An unexplained
  missing UI element remains a failure requiring diagnosis.
- **Current stock-1.1.8 baseline prerequisite is BLOCKED:** the supplied
  `reader_comic_prepare.json` uses `readerCommand_layout`, which stock 1.1.8 does
  not expose. Before the migration flow, the receiving side must provide and
  verify a 1.1.8-compatible baseline scenario or perform documented manual comic
  preparation with a compatible read-only assertion/capture scenario. That
  prerequisite is not supplied or qualified here. Do not relabel a failed
  preparation as PASS or manually fabricate a passing session checkpoint.
- Source SHA fields are receipt information, not derivable from `--version`.
  Reconcile the app hash with the reviewed artifact/build record before passing
  `-ExpectedAppSha256`. Hashing an arbitrary executable and accepting its own hash
  does not authenticate the candidate.

Assigned 1.1.9 artifact:

- File: `Colosseum-1.1.9-setup.exe`
- Size: **328380744 bytes**
- SHA256: `a4f4fe1279335769d96b5e30f9917a21fcddd8fc87ce0c9d6f2538fd6aaa57e6`
- Packaged source: `70e0773b56fa4a35adc1c0e14a8520e4b680c27b`
- Compiled app source: `47e90b3f244d9ad76b7f5bbc7607629fe358288a`
- Assigned suite base: `a6104f7a8d9aab9766e71f031b7908beaeca85ad`
- Reported runtime: Qt 6.11.1 / MSVC 2022

The assignment describes the original compiled/packaged/base-source differences
as release tooling/compression/size policy. That description does **not** cover
the subsequently authored task-branch QML observations. Preserve source-diff and
build/runtime evidence for the compiled `47e90b3...`, packaged `70e0773...`, base
`a6104f7...` and actual candidate-QML relationship; their compatibility must not
be inferred from the common 1.1.9 version label. The installer is local to the receiving side, not a public
URL to fetch. A different build requires its own reviewed hashes, source receipt
and runtime values; do not relabel it as the known artifact.

Do not run the old release smoke script on a daily account. Its fixed 300 MiB
limit rejects the assigned 328380744-byte installer, and its cleanup force-kills
app/path-matched processes. The new runner uses exact artifact size and tracked
PID plus creation time. Leave the legacy script unchanged in this QA task.

## QML compatibility prerequisite

The assigned installer predates the task branch's **19 read-only QML observability
additions**. Do not assume its bundled QML has the new selectors or run the full
new scenario set against it as a compatible package. A missing matching QML/build
receipt is a prerequisite blocker.

`windows_receiving.ps1` has **no `-Qml` parameter**: it deliberately launches the
installed executable without a QML override. Its migration/relaunch scenarios
must be independently verified against that untouched package. The example
`$InstalledRestartScenario` below is therefore a receiving prerequisite, not a
claim that the supplied `reader_comic_restart.json` works with the old bundle.

For the separate instrumented GUI run, the common runner's flag is `--qml`.
Use `<reviewed task-branch checkout>/qml/Main.qml` with the complete matching
checkout/assets, or `qml/Main.qml` from a separately built matching task-branch
package. Inspect exactly that tree and executable:

```powershell
python (Join-Path $Repo 'tests\acceptance_119\run.py') --inspect `
  --exe (Join-Path $Package 'native\build-msvc\colosseum.exe') `
  --qml (Join-Path $Repo 'qml\Main.qml')
```

The build owner must independently review `exe_sha256`, `qml_tree_sha256`, the
fixed-base `source_sha`, actual task-branch commit, native compiled source and
runtime compatibility, then supply the reviewed identity JSON to `run.py`.
`--inspect` measures bytes; it does not attest compatibility. Use the same exact
QML tree on restart. Do not automatically bless observed hashes after a mismatch.
Until this combined-runtime compatibility is established, leave the instrumented
run BLOCKED.

A run with `--qml` qualifies only that **executable + overridden QML** combination.
It does not qualify normal packaged startup, the unchanged signed installer, or
its bundled selectors. Keep its evidence separate from no-override installation/
relaunch evidence. Do not overwrite the installed QML or repackage the assigned
installer to obtain the probes. A new matching build has distinct artifact/source
identities and cannot inherit the known installer's hash/signature evidence.

## Short receiving commands

Set these values to verified local paths. Keep fixtures/evidence outside the
installation directory so a manual replacement cannot destroy the test inputs.
No workstation-specific path is baked into the scripts.

```powershell
$Repo = '<reviewed acceptance checkout>'
$Package = '<installed Colosseum root in disposable account>'
$Lanista = '<reviewed lanista.exe>'
$Installer119 = '<assigned Colosseum-1.1.9-setup.exe>'
$Fixtures = '<new empty fixture directory>'
$Evidence = '<new receiving evidence directory>'
$Hash118 = '<reviewed installed 1.1.8 executable SHA256>'
$Hash119 = '<reviewed installed 1.1.9 executable SHA256>'
$BaselineScenario = '<verified 1.1.8-compatible baseline/preparation scenario; currently missing>'
$InstalledRestartScenario = '<verified untouched-installer-compatible restore scenario; not yet qualified>'
$Runner = Join-Path $Repo 'tests\acceptance_119\windows_receiving.ps1'
$Scenarios = Join-Path $Repo 'tests\lanista_scenarios\acceptance_119'
$Session = Join-Path $Evidence 'comic-migration-session.json'
python (Join-Path $Repo 'tests\acceptance_119\fixtures.py') --root $Fixtures
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed' }
```

First manually install the authenticated 1.1.8 baseline in the disposable account.
Do not allow the installer's Finish page to launch an untagged app. Save installer
exit code, version, file hash, HKCU DisplayVersion/InstallLocation, shortcut targets
and an installation screenshot in the evidence packet.

```powershell
& $Runner -Phase Baseline118 -PackageRoot $Package -Lanista $Lanista `
  -Scenario $BaselineScenario `
  -FixtureRoot $Fixtures -SeedName comic -OutputDirectory $Evidence `
  -SessionFile $Session -ExpectedAppSha256 $Hash118 -ConfirmDisposableWindowsAccount
if ($LASTEXITCODE -ne 0) { throw 'Baseline failed; preserve evidence and stop migration' }
```

The verified baseline journey must create real synthetic reading progress by
navigating a real eight-page CBZ through actual controls. The seed supplies Open
Recent file identities, not saved reading progress. It must assert the exact file
identity and interior page. `reader_comic_prepare.json` documents the intended
candidate behavior but is not an executable stock-1.1.8 baseline contract.

If that unsupported candidate scenario is attempted on Baseline118 and the actual
Lanista response is `NO_SUCH_ITEM` for `readerCommand_layout`, the runner records
BLOCKED, the exact selector/protocol reply and native exit, then closes its owned
app normally and exits 2. Other missing selectors, value assertions, crashes,
timeouts and nonzero exits remain FAIL; the classification is deliberately narrow.
The runner saves its bound scenario, replies, roots,
process inventory, hashes, logs, screenshot and normal/forced shutdown outcome.

Now manually install the **hash-verified 1.1.9** into the **same installation
root**. Do not uninstall 1.1.8, clear the recorded tagged profile, reseed it, or
launch the finish-page shortcut. Capture the installer UI, completion/exit code,
updated HKCU version and shortcut destinations. Confirm the prior tagged profile
still exists before starting the candidate.

```powershell
& $Runner -Phase After119 -PackageRoot $Package -Lanista $Lanista `
  -Scenario $InstalledRestartScenario `
  -FixtureRoot $Fixtures -OutputDirectory $Evidence -SessionFile $Session `
  -ExpectedAppSha256 $Hash119 -InstallerPath $Installer119 -ConfirmDisposableWindowsAccount
if ($LASTEXITCODE -ne 0) { throw 'Migration/relaunch failed' }
& $Runner -Phase Restart119 -PackageRoot $Package -Lanista $Lanista `
  -Scenario $InstalledRestartScenario `
  -FixtureRoot $Fixtures -OutputDirectory $Evidence -SessionFile $Session `
  -ExpectedAppSha256 $Hash119 -InstallerPath $Installer119 -ConfirmDisposableWindowsAccount
if ($LASTEXITCODE -ne 0) { throw 'Second relaunch failed' }
```

The two candidate phases must load **the same recorded profile and cache roots**,
activate Continue in the UI, open the exact `comics/a/twin.cbz` identity and restore
its exact interior page. The sibling `comics/b/twin.cbz` is intentionally same-named
and must not substitute. A current-version check without this state transition is
not migration evidence. Do not pass `-SeedName` on migration/restart; it is rejected.

For a fresh full candidate GUI run, first satisfy the separate QML compatibility
prerequisite above, then use the common `run.py` runner and its reviewed
identity/manifest. `update_chronicle_ui.json` is a **fresh-profile** journey with
local onboarding, all six current release chapters, rendered frame readiness,
Escape and visible Back/reopen. Do not run its fresh onboarding leg in the
already-onboarded migration profile. Native updater checks run separately:

```powershell
python (Join-Path $Repo 'tests\acceptance_119\updater_checks.py') `
  --source-root $Repo --output (Join-Path $Evidence 'updater-components') `
  --native-bin-dir '<fresh Windows native test build directory>'
```

Component script exit 0 means the selected subset passed; exit 1 means failure;
exit 2 means incomplete coverage. Full default selection intentionally retains
Windows migration NOT RUN and automatic handoff BLOCKED. A receiver should append
an independently reviewed Windows verdict instead of replacing those historical
component results.

The pure runner normalizer can be checked without launching the app:

```powershell
python (Join-Path $Repo 'tests\acceptance_119\test_updater_checks.py') --powershell '<pwsh executable>'
```

This parses the real script and tests final quit/drain removal, retaining an
unrelated pause, and rejection of early/empty quits. Seven protocol-fixture cases
check that only the known baseline selector gap becomes BLOCKED. It also starts two temporary
PowerShell children to verify recovery after a synthetic CIM exception, isolation
from the unrelated sibling, and handling of an already-exited child. These are
helper/synthetic-process checks, not Windows app lifecycle qualification.
The cloud parser used the [official PowerShell 7.4.6 archive](https://github.com/PowerShell/PowerShell/releases/tag/v7.4.6),
SHA256 `6f6015203c47806c5cc444c19d8ed019695e610fbd948154264bf9ca8e157561`.

## Receiving evidence checklist

Complete each row in a separate `manual-migration-result.json` with `case_id`,
`status` (PASS/FAIL/BLOCKED/NOT RUN), `observed`, `expected`, `evidence`, timestamp
and reviewer. A phase PASS in `receiving-result.json` is not an aggregate release
qualification; it covers that supplied journey and observed owned-process cleanup.

1. `WIN-ARTIFACT`: exact installer hash/size, executable hash/version, reviewed
   source relationship and runtime. Include the authentic 1.1.8 baseline identity.
2. `WIN-BASELINE-STATE`: Baseline118 result and screenshots establish local-only
   operation, CBZ full identity and exact page before closing normally.
3. `WIN-MANUAL-INSTALL`: installation UI and exit status prove actual 1.1.8 → 1.1.9
   replacement. Include before/after registry metadata and shortcut destinations.
4. `WIN-STATE-RESTORE`: After119 and Restart119 results prove UI Continue, exact
   file identity/page and identical recorded roots. Include the same-name negative
   control from the candidate reading suite and a missing-file case separately.
5. `WIN-CHRONICLE`: shipping candidate GUI shows 1.1.9 and the six current chapters.
   Cross-check the production-signed chronicle/public-key result; a 1.1.0 test-key
   native fixture is not evidence of this installed screen.
6. `WIN-HELPERS`: normal app exit code, duration and observed child PID/creation-time
   inventory. No live observed helper may remain after the bounded wait. Forced
   recovery is FAIL, with recovered PID and post-recovery status preserved. If CIM
   fails, the retained exact app Process/SafeHandle is recovered with bounded waits;
   unverified helper cleanup is explicitly BLOCKED and no name/PID sweep occurs. Sampling
   cannot prove the absence of an unobserved short-lived helper.
7. `WIN-WATCH-PARTY`: specifically enter/exercise Watch Party before normal exit,
   then verify shutdown and relaunch. A generic app close cannot qualify this row.
8. `WIN-OTHER-STATE`: repeat the migration with another **fresh session checkpoint**
   and appropriate synthetic Vault/settings/extension state if broad preservation
   is claimed. One comic fixture proves only the tested comic/local state. Never
   infer all settings, accounts or libraries were migrated from a marker file.
9. `WIN-NEGATIVE-CONTROLS`: an intentionally wrong expected app/installer hash must
   fail before launch; an intentionally wrong page/identity assertion must fail the
   journey. Keep the failing JSON/log and screenshot. Never bless failing goldens.

Keep the disposable profile and screenshots until inspection is complete. Cleanup
is a manual disposable-account/VM operation, not a wildcard process/file deletion
in this script. No daily profile, credentials or production signing secrets belong
in the evidence packet.

## Automatic updating remains BLOCKED

Required but not supplied: an authentic **higher-version** production-signed
fixture with matching installer bytes and a **supported private staging route**.
Do not request/export the private signing key, activate the public stable feed,
patch the production discovery host for this QA run, or treat a same-version check,
synthetic state injection, fake installer file, or test-key harness as a handoff.

When authorized fixtures and routing exist, a separate receiving test must prove
available → actual download → verified ready → updater/install process handoff →
old PID exit → new executable/version/hash → real ping/get-state → preserved
synthetic state → normal second relaunch. Track installer/helper ownership and
rollback/backup outcomes from actual processes. Preserve a transient 503 and
interrupted transfer recovery run at that supported route, alongside native
wrong-signature/hash/size/URL rejection evidence. Until then report BLOCKED.

## What the cloud checks actually establish

- Actual checked-in 1.1.9 production signature and DER public-key fingerprint
  continuity, plus decisive tampered manifest/signature/key negative controls.
- Exact first-party URL assignment is **static source** evidence.
- Existing native trust/discovery/download targets exercise real components with
  local HTTP fixtures, signed legacy/test-key compatibility, unsafe input, 503,
  timeout, cancellation, Range/ETag resume, wrong hashes and wrong sizes.
- Service tests use synthetic client seams. Feed publisher tests use fake GitHub
  calls; routing tests use mocked fetch. These labels are preserved in JSON.
- The Linux install bridge proves Windows automatic installation is refused on
  Linux. The installed-chronicle Qt Test uses signed **1.1.0 test-key** bytes.
- None of these component results executes the assigned Windows installer or
  proves the assembled GUI survives a genuine update handoff.
