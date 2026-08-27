# Account End-to-End Lanista Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Permanently verify the real Welcome -> Create Account -> recovery-key -> shell journey and a fresh-profile Welcome -> Sign In -> shell journey against the account mock, while preventing the mock from drifting away from the production service contract.

**Architecture:** A PowerShell orchestration gate starts the in-memory mock service, exports its URL only to isolated tagged Colosseum/Lanista sessions, resets mock state, then runs two committed Lanista scenarios. The first scenario creates a fixed disposable account and acknowledges its recovery key; the second launches a different fresh tagged profile and signs into that same mock account. Static and service tests run before the live journeys so a mock-only green cannot hide production contract drift.

**Tech Stack:** Lanista named-pipe runner, Qt/QML production UI, PowerShell, Node.js mock service, Go account service tests.

**Spec:** `docs/superpowers/specs/2026-08-28-account-system-hardening-design.md`

**Prerequisites:** Integrate `2026-08-28-account-protocol-contract-repair.md` and `2026-08-28-account-offline-recovery-ux.md` first. This plan relies on canonical `builtin_avatar_id`, `accountFlyoutUsername`, and `accountRecoveryKeySaved`.

## Global Constraints

- Never use a production account or production service URL.
- Every run uses unique Lanista pipes and tagged AppData roots.
- The mock is started locally and killed in `finally`, even when a scenario fails.
- The full-success sentinel is printed only after both live journeys and all warning/contract gates pass.
- A static-only mode must use a different sentinel and cannot masquerade as live verification.
- Two new scenarios move the maintained Lanista scenario inventory from 53 to 55.
- Every change follows RED -> GREEN TDD and ends in a focused commit.

---

### Task 1: Pin the Lanista selector contract for auth surfaces

**Files:**
- Modify: `qml/account/AccountSignIn.qml:80-96`
- Create: `tests/test_account_lanista_selector_contract.ps1`
- Verify: `qml/account/AccountWelcome.qml`, `AccountCreate.qml`, `AccountRecoveryKey.qml`, `AccountFlyout.qml`, `qml/TopBar.qml`

**Interfaces:**
- Consumes selectors: `accountWelcomeCreateAccount`, `accountWelcomeSignIn`, `accountCreateUsername`, `accountCreatePassword`, `accountCreateConfirmPassword`, `accountCreateSubmit`, `accountSignInUsername`, `accountSignInPassword`, `accountSignInSubmit`, `accountRecoveryKeyValue`, `accountRecoveryKeySaved`, `colosseumTopbarAccountButton`, `accountFlyoutUsername`, `accountHost`.
- Produces: `accountSignInError` selector for the invalid-credentials negative-control step.

- [ ] **Step 1: Write a failing static selector contract**

Create a PowerShell gate that reads the production QML files and fails unless every selector above appears exactly once in its owning component. Include:

```powershell
Assert-Contains 'qml/account/AccountSignIn.qml' 'objectName: "accountSignInError"'
Assert-Contains 'qml/TopBar.qml' 'objectName: "colosseumTopbarAccountButton"'
Assert-Contains 'qml/account/AccountFlyout.qml' 'objectName: "accountFlyoutUsername"'
Assert-Contains 'qml/account/AccountRecoveryKey.qml' 'objectName: "accountRecoveryKeySaved"'
```

- [ ] **Step 2: Run and verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_lanista_selector_contract.ps1
```

Expected: FAIL because the sign-in error `Text` has no object name yet.

- [ ] **Step 3: Add the error selector without changing behavior**

On the existing error `Text` in `AccountSignIn.qml`, add:

```qml
objectName: "accountSignInError"
```

Do not expose controller secrets or add new state.

- [ ] **Step 4: Run selector gate and QML load check**

Run the selector contract again, then load `qml/Main.qml` through the repository's normal QML/Lanista development path. Expected: selector gate PASS and no QML errors.

- [ ] **Step 5: Commit**

```powershell
git add qml/account/AccountSignIn.qml tests/test_account_lanista_selector_contract.ps1
git commit -m "test(account): pin auth Lanista selectors"
```

---

### Task 2: Commit the create-account Lanista journey

**Files:**
- Create: `tests/lanista_scenarios/account_create_happy_path.json`

**Interfaces:**
- Consumes: local mock account service at `COLOSSEUM_ACCOUNT_SERVICE_URL` and production onboarding QML selectors.
- Produces: a created disposable account `LanistaAccountOwner` with password `Lanista-Account-Pass-2026!` and an acknowledged one-time recovery key.

- [ ] **Step 1: Write the scenario with explicit assertions**

Use this sequence:

```json
[
  {"cmd":"ui-wait-for","payload":{"object":"accountHost","prop":"visible","value":true,"timeout_ms":15000}},
  {"cmd":"ui-click","payload":{"target":"accountWelcomeCreateAccount"}},
  {"cmd":"ui-text-input","payload":{"target":"accountCreateUsername","text":"LanistaAccountOwner"}},
  {"cmd":"ui-text-input","payload":{"target":"accountCreatePassword","text":"Lanista-Account-Pass-2026!"}},
  {"cmd":"ui-text-input","payload":{"target":"accountCreateConfirmPassword","text":"Lanista-Account-Pass-2026!"}},
  {"cmd":"ui-click","payload":{"target":"accountCreateSubmit"}},
  {"cmd":"ui-wait-for","payload":{"object":"accountRecoveryKeyValue","prop":"visible","value":true,"timeout_ms":15000}},
  {"cmd":"ui-click","payload":{"target":"accountRecoveryKeySaved"}},
  {"cmd":"ui-wait-for","payload":{"object":"accountHost","prop":"visible","value":false,"timeout_ms":15000}}
]
```

Wrap those steps in the repository's existing scenario JSON envelope (`name`, `description`, `steps`) exactly as used by neighboring files. The prerequisite UX plan supplies the `accountRecoveryKeySaved` selector.

- [ ] **Step 2: Run the scenario against no mock server and verify RED for the right reason**

Launch with a unique tag and an unreachable local account URL. Expected: account creation does not reach the recovery-key step and scenario exits nonzero.

- [ ] **Step 3: Start the mock and run GREEN**

```powershell
$env:COLOSSEUM_ACCOUNT_SERVICE_URL = 'http://127.0.0.1:18080'
node tests/mock-account-service/server.mjs --port 18080
```

In a second shell run the scenario through `lanista session run ... --drive` with current `colosseum.exe` and `qml/Main.qml`. Expected: every step passes. The scenario proves the one-time recovery-key page is visible but never reads the key text into Lanista output.

- [ ] **Step 4: Capture the scenario result manifest path**

Record the session manifest and screenshot/evidence path printed by Lanista. Do not copy the recovery-key value into logs or plan evidence.

- [ ] **Step 5: Commit**

```powershell
git add tests/lanista_scenarios/account_create_happy_path.json
git commit -m "test(account): add create account Lanista journey"
```

---

### Task 3: Commit the fresh-profile sign-in journey with bad-password negative control

**Files:**
- Create: `tests/lanista_scenarios/account_signin_happy_path.json`

**Interfaces:**
- Consumes: the account created in Task 2 while the same mock process remains alive.
- Produces: one scenario that proves generic bad-credential handling and then successful sign-in from a second isolated AppData profile.

- [ ] **Step 1: Write the scenario with the negative control first**

Use this sequence inside the normal scenario envelope:

```json
[
  {"cmd":"ui-wait-for","payload":{"object":"accountHost","prop":"visible","value":true,"timeout_ms":15000}},
  {"cmd":"ui-click","payload":{"target":"accountWelcomeSignIn"}},
  {"cmd":"ui-text-input","payload":{"target":"accountSignInUsername","text":"LanistaAccountOwner"}},
  {"cmd":"ui-text-input","payload":{"target":"accountSignInPassword","text":"Definitely-Wrong-Password!"}},
  {"cmd":"ui-click","payload":{"target":"accountSignInSubmit"}},
  {"cmd":"ui-wait-for","payload":{"object":"accountSignInError","prop":"visible","value":true,"timeout_ms":10000}},
  {"cmd":"qml-get","payload":{"object":"accountSignInError","props":["text","visible"]},"expect":[{"path":"props.visible","op":"==","value":"true"},{"path":"props.text","op":"contains","value":"username or password"}]},
  {"cmd":"ui-text-input","payload":{"target":"accountSignInPassword","text":"Lanista-Account-Pass-2026!"}},
  {"cmd":"ui-click","payload":{"target":"accountSignInSubmit"}},
  {"cmd":"ui-wait-for","payload":{"object":"accountHost","prop":"visible","value":false,"timeout_ms":15000}},
  {"cmd":"ui-click","payload":{"target":"colosseumTopbarAccountButton"}},
  {"cmd":"ui-wait-for","payload":{"object":"accountFlyoutUsername","prop":"visible","value":true,"timeout_ms":5000}},
  {"cmd":"qml-get","payload":{"object":"accountFlyoutUsername","props":["text"]},"expect":[{"path":"props.text","op":"==","value":"LanistaAccountOwner"}]}
]
```

The negative-control assertion deliberately uses generic copy. It must not distinguish "unknown username" from "wrong password".

- [ ] **Step 2: Run the scenario before the mock account exists and verify RED**

With a reset mock that has not run Task 2 create, execute the sign-in scenario. Expected: it never reaches the successful account-host-close step and exits nonzero.

- [ ] **Step 3: Run Task 2 create, then this sign-in scenario with a new tag**

Keep the same mock process alive. Use a different Lanista tag for sign-in. Expected: the wrong-password phase remains on the sign-in screen with generic error, the corrected password signs in, onboarding closes, and the flyout username equals `LanistaAccountOwner`.

- [ ] **Step 4: Verify AppData and pipe isolation**

Compare the two Lanista manifests and assert the create and sign-in runs have different tagged AppData roots and different named pipes.

- [ ] **Step 5: Commit**

```powershell
git add tests/lanista_scenarios/account_signin_happy_path.json
git commit -m "test(account): add fresh profile sign-in journey"
```

---

### Task 4: Build one hostile account-journey wrapper

**Files:**
- Create: `tests/test_account_auth_journeys.ps1`

**Interfaces:**
- Produces full success sentinel: `ACCOUNT_AUTH_JOURNEYS_OK` only after static/service gates, create journey, sign-in journey including its bad-credential negative control, and cleanup.
- Produces static-only sentinel: `ACCOUNT_AUTH_STATIC_OK` when invoked with `-SkipLive`.

- [ ] **Step 1: Write wrapper preflight checks**

The wrapper must verify these files exist:

```powershell
$required = @(
  'tests/mock-account-service/server.mjs',
  'tests/lanista_scenarios/account_create_happy_path.json',
  'tests/lanista_scenarios/account_signin_happy_path.json',
  'qml/account/AccountCreate.qml',
  'qml/account/AccountSignIn.qml'
)
```

It must also statically assert the canonical `builtin_avatar_id` contract exists in both the production Go handler and the mock, and fail if the mock still emits a wire `avatar_id` property.

- [ ] **Step 2: Add deterministic mock lifecycle management**

Reserve an ephemeral loopback port long enough to obtain its number, release it, then immediately launch the mock on that port:

```powershell
$runId = [guid]::NewGuid().ToString('N').Substring(0, 10)
$listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()
$mockOut = Join-Path $env:TEMP "colosseum-account-mock-$runId.out.log"
$mockErr = Join-Path $env:TEMP "colosseum-account-mock-$runId.err.log"
$mock = Start-Process node -ArgumentList @(
    'tests/mock-account-service/server.mjs',
    '--port', $port
) -PassThru -RedirectStandardOutput $mockOut -RedirectStandardError $mockErr
```

Poll `GET http://127.0.0.1:$port/healthz` until 200 or 10 seconds elapse. In `finally`, stop only `$mock.Id` if still alive.

- [ ] **Step 3: Run service/static gates before live UI**

Run and require zero exit code:

```powershell
node tests/mock-account-service/server.mjs --selftest
Push-Location server/account-service
go test ./... -count=1
Pop-Location
```

If `-SkipLive` is present, print only `ACCOUNT_AUTH_STATIC_OK` and exit 0 here.

- [ ] **Step 4: Run create and sign-in sessions with unique tags**

Set only for the child processes:

```powershell
$env:COLOSSEUM_ACCOUNT_SERVICE_URL = "http://127.0.0.1:$port"
```

Run create using tag `account-create-$runId`, then the sign-in scenario using tag `account-signin-$runId`. The sign-in scenario itself performs the bad-password negative control before correcting the password. Keep the same mock process alive so the created account persists in mock memory.

- [ ] **Step 5: Add warning and evidence gates**

Require both Lanista scenario invocations to exit 0. The sign-in scenario only exits 0 if its deliberate bad-credential phase stays on the sign-in screen and exposes generic credential-error copy before the corrected password succeeds. Run the repository's Lanista warning gate on positive session logs, allowing only already-documented benign platform warnings. Print the final sentinel only after all gates:

```powershell
Write-Host 'ACCOUNT_AUTH_JOURNEYS_OK'
```

- [ ] **Step 6: Run wrapper and verify GREEN**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_auth_journeys.ps1
```

Expected: create PASS, sign-in scenario PASS including its internal bad-password negative control, warning gate PASS, final `ACCOUNT_AUTH_JOURNEYS_OK`.

- [ ] **Step 7: Commit**

```powershell
git add tests/test_account_auth_journeys.ps1
git commit -m "test(account): gate auth journeys end to end"
```

---

### Task 5: Update Lanista capability ledger and final regression gate

**Files:**
- Modify: `docs/colosseum-lanista-verification.md`
- Verify: all files from Tasks 1-4

**Interfaces:**
- Produces: maintained scenario count 55 and reviewer-ready evidence for the complete account hardening program.

- [ ] **Step 1: Update the scenario inventory**

Change the maintained Lanista scenario count from 53 to 55 and add both filenames with one-line purposes:

```text
account_create_happy_path.json - real onboarding create + one-time recovery-key acknowledgement
account_signin_happy_path.json - fresh-profile sign-in to the account created by the mock-backed journey
```

The bad-password phase lives inside `account_signin_happy_path.json`, so the inventory remains exactly two new scenarios.

- [ ] **Step 2: Run the complete account native matrix**

```powershell
cmake --build native/build-msvc --target tst_account_core tst_account_identity tst_account_onboarding tst_account_adoption tst_account_shared_pc
.\native\build-msvc\tst_account_core.exe
.\native\build-msvc\tst_account_identity.exe
.\native\build-msvc\tst_account_onboarding.exe
.\native\build-msvc\tst_account_adoption.exe
.\native\build-msvc\tst_account_shared_pc.exe
```

Expected: 0 failures.

- [ ] **Step 3: Run full account service and mock tests**

```powershell
Push-Location server/account-service
go test ./... -count=1
Pop-Location
node tests/mock-account-service/server.mjs --selftest
```

Expected: PASS.

- [ ] **Step 4: Run the full live account journey gate**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_auth_journeys.ps1
```

Expected final line: `ACCOUNT_AUTH_JOURNEYS_OK`.

- [ ] **Step 5: Check repository hygiene**

```powershell
git diff --check
git status --short
git log --oneline --decorate -10
```

Expected: clean diff check; no mock process remains; no tagged AppData path is staged; only intended source/test/docs files are modified.

- [ ] **Step 6: Commit ledger update**

```powershell
git add docs/colosseum-lanista-verification.md
git commit -m "docs(lanista): register account auth journeys"
```

- [ ] **Step 7: Prepare integration evidence**

The handoff must include: native account test results, Go test result, mock self-test count, create scenario step count, bad-password negative-control result, sign-in scenario step count, warning-gate result, final sentinel, and `git diff --check` result. Never include the recovery-key plaintext.
