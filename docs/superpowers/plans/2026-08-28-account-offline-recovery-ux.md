# Account Offline and Recovery UX Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make remembered offline accounts render as accounts, align password recovery with the real password policy, and require explicit acknowledgement before one-time recovery keys can be dismissed.

**Architecture:** Separate "an account exists on this device" from "online mutations are currently allowed" in QML. Keep native account modes unchanged. Reuse the existing recovery-key acknowledgement interaction for all one-time key presentations, and make the recovery form use the same 8-128 Unicode code-point policy as create/change/server validation.

**Tech Stack:** Qt Quick/QML, Qt Test/QML harnesses, existing `AccountController` mode API.

**Spec:** `docs/superpowers/specs/2026-08-28-account-system-hardening-design.md`

## Global Constraints

- `offline` means a remembered account is present but network-dependent mutations are unavailable.
- Network-mutating controls must still require `mode === "signedIn"` unless their native operation explicitly supports offline mode.
- Do not change native mode names to repair presentation logic.
- Password recovery accepts 8-128 Unicode code points, matching create/change/server policy.
- A one-time recovery key cannot be dismissed without explicit saved acknowledgement.
- Every change follows RED -> GREEN TDD and ends in a focused commit.

---

### Task 1: Present offline remembered accounts correctly in the top bar and flyout

**Files:**
- Modify: `qml/TopBar.qml:10-24,237-302`
- Modify: `qml/account/AccountFlyout.qml:9-205`
- Create: `tests/account_offline_shell_identity_harness.qml`
- Create: `tests/test_account_offline_shell_identity.ps1`

**Interfaces:**
- Consumes: account controller `mode`, `username`, `syncState`, `pendingOutboxCount`.
- Produces: `accountPresent = signedIn || offline` presentation state in both shell surfaces; flyout offline action calls `logoutCurrent()`.

- [ ] **Step 1: Write the QML harness with a recording fake controller**

Create one fake:

```qml
QtObject {
    id: fakeController
    property string mode: "offline"
    property string username: "OfflineOwner"
    property string syncState: "retrying"
    property int pendingOutboxCount: 0
    property int logoutCalls: 0
    property int returnToSignInCalls: 0
    function logoutCurrent() { ++logoutCalls }
    function returnToSignIn() { ++returnToSignInCalls }
}
```

Instantiate `TopBar { accountController: fakeController }` and `AccountFlyout { controller: fakeController }`. Assert the top-bar account control's accessible name is `Account: OfflineOwner`, the flyout username text is `OfflineOwner`, the flyout session action text is `Sign out`, and clicking it increments `logoutCalls` only.

The production QML must expose these stable selectors:

```text
colosseumTopbarAccountButton   (already exists)
accountFlyoutUsername          (add in this task)
accountFlyoutSessionAction     (add in this task)
```

- [ ] **Step 2: Run the harness and verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_offline_shell_identity.ps1
```

Expected: FAIL because `TopBar.qml` and `AccountFlyout.qml` currently treat only `signedIn` as account-present.

- [ ] **Step 3: Make TopBar injectable and offline-aware**

At `TopBar` root add:

```qml
property var accountController:
    typeof AccountController !== "undefined" ? AccountController : null
readonly property bool accountPresent: accountController
    && (accountController.mode === "signedIn"
        || accountController.mode === "offline")
```

In the account medallion replace direct `AccountController` checks with `bar.accountController` / `bar.accountPresent`. The accessible name becomes:

```qml
Accessible.name: bar.accountPresent
    ? ("Account: " + bar.accountController.username)
    : "Account"
```

and the medallion uses the remembered username initial for both online and offline account-present states.

- [ ] **Step 4: Make AccountFlyout account-present aware**

Add:

```qml
readonly property bool accountPresent: controller
    && (controller.mode === "signedIn" || controller.mode === "offline")
readonly property bool onlineAccount: controller
    && controller.mode === "signedIn"
```

Use `accountPresent` for username, account/divider/navigation visibility and Sign out versus Sign in. Add:

```qml
objectName: "accountFlyoutUsername"
```

to the username/status `Text`, and:

```qml
objectName: "accountFlyoutSessionAction"
```

to the session button. Its click path is:

```qml
if (root.accountPresent)
    root.controller.logoutCurrent()
else
    root.controller.returnToSignIn()
```

- [ ] **Step 5: Run offline and local-only harness phases**

After the offline assertions, set `fakeController.mode = "localOnly"`. Assert the top-bar accessible name becomes `Account`, the flyout says `Not signed in`, the action says `Sign in`, and clicking it increments only `returnToSignInCalls`.

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add qml/TopBar.qml qml/account/AccountFlyout.qml tests/account_offline_shell_identity_harness.qml tests/test_account_offline_shell_identity.ps1
git commit -m "fix(account): present offline account in shell"
```

---

### Task 2: Make Account Center identity offline-aware without enabling online mutations

**Files:**
- Modify: `qml/account/AccountCenter.qml:15-18,180-360`
- Verify: `qml/account/AccountProfilePage.qml`, `AccountDevicesPage.qml`, `AccountSecurityPage.qml`, `AccountRecoveryPage.qml`
- Create: `tests/account_offline_center_harness.qml`
- Create: `tests/test_account_offline_center.ps1`

**Interfaces:**
- Consumes: same controller state as Task 1.
- Produces: offline account identity/header and Sign out action while child-page mutation gates remain `mode === "signedIn"`.

- [ ] **Step 1: Write a failing Account Center harness**

Instantiate `AccountCenter` with `mode: "offline"`, `username: "OfflineOwner"`, and recording `logoutCurrent()` / `returnToSignIn()` methods. Open section `colosseum` and assert:

```text
header username = OfflineOwner
rail session action = Sign out
click -> logoutCurrent exactly once
returnToSignIn -> zero calls
```

Also inspect one network mutation control in Profile/Security/Devices and assert it remains disabled while offline.

- [ ] **Step 2: Run and verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_offline_center.ps1
```

Expected: FAIL because `AccountCenter.signedIn` is false for offline and the rail action becomes the dead Sign in path.

- [ ] **Step 3: Introduce account-present versus online-account properties**

At the host:

```qml
readonly property bool accountPresent: controller
    && (controller.mode === "signedIn" || controller.mode === "offline")
readonly property bool onlineAccount: controller
    && controller.mode === "signedIn"
```

Use `accountPresent` for the centre's identity and rail session action. Do not mechanically replace child-page `signedIn` checks: those controls intentionally stay disabled offline.

- [ ] **Step 4: Run harness plus QML load checks**

Run both new offline wrappers and any existing QML-load gate that imports `AccountCenter.qml`. Expected: PASS with no QML binding errors.

- [ ] **Step 5: Commit**

```powershell
git add qml/account/AccountCenter.qml tests/account_offline_center_harness.qml tests/test_account_offline_center.ps1
git commit -m "fix(account): keep offline account identity visible"
```

---

### Task 3: Align password recovery with the canonical password policy

**Files:**
- Modify: `qml/account/AccountRecovery.qml:27-61`
- Create: `tests/account_recovery_policy_harness.qml`
- Create: `tests/test_account_recovery_policy.ps1`
- Verify: `server/account-service/internal/account/password.go:99-123`
- Verify: `qml/account/AccountSecurityPage.qml:31-38,93-106,157-166`

**Interfaces:**
- Produces: recovery form acceptance rule of 8-128 Unicode code points.
- Consumes: existing native `controller.recoverPassword(username, recoveryKey, newPassword)` call.

- [ ] **Step 1: Write a failing 8-character recovery-password harness**

Use a fake controller that records `recoverPassword` arguments. Populate valid username/recovery-key fields with an exactly 8-code-point password such as `A9!b2@c3`, matching confirm text. Trigger submit and assert one native call with the exact password.

Also assert 7 code points are rejected and 129 code points are rejected without a native call.

- [ ] **Step 2: Run and verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_policy.ps1
```

Expected: 8-character case FAIL because current recovery UI requires at least 15.

- [ ] **Step 3: Change only the recovery minimum**

In `submit()` replace:

```qml
if (count < 15 || count > 128)
```

with:

```qml
if (count < 8 || count > 128)
```

and change the copy to:

```qml
validationMessage = "Use a password between 8 and 128 characters."
```

Do not duplicate server blocklist logic in QML. The service remains authoritative for contextual/blocked passwords.

- [ ] **Step 4: Run recovery, security-page, and Go password tests**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_policy.ps1
Push-Location server/account-service
go test ./internal/account -run Password -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add qml/account/AccountRecovery.qml tests/account_recovery_policy_harness.qml tests/test_account_recovery_policy.ps1
git commit -m "fix(account): align recovery password policy"
```

---

### Task 4: Require explicit acknowledgement for every newly issued recovery key

**Files:**
- Modify: `qml/account/AccountRecoveryKey.qml:6-294`
- Modify: `qml/account/AccountOnboarding.qml` only if route completion currently depends on the old generic Continue action
- Create: `tests/account_recovery_key_ack_harness.qml`
- Create: `tests/test_account_recovery_key_ack.ps1`
- Test: `tests/auto/account_onboarding/tst_account_onboarding.cpp` for presenter lifetime/dismissal invariants

**Interfaces:**
- Consumes: existing presenter `purpose`, `recoveryKey`, `dismiss()`, and `copyRecoveryKey()`.
- Produces: explicit `I saved it` completion for `accountCreated`, `passwordRecovered`, `deviceChallengeRecovered`, and `manualReplacement` purposes.

- [ ] **Step 1: Write a failing QML acknowledgement regression**

For each purpose:

```text
accountCreated
passwordRecovered
deviceChallengeRecovered
manualReplacement
```

instantiate the recovery-key page with a fake presenter and assert:

- recovery key is visible
- the finishing primary action reads `I saved it`
- the old `Continue to Colosseum` / `Continue to sign in` completion button is absent or not visible
- presenter `dismiss()` is called only after clicking `I saved it`
- copy action alone does not dismiss

- [ ] **Step 2: Run and verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_key_ack.ps1
```

Expected: FAIL for non-manual purposes because they expose the generic Continue button.

- [ ] **Step 3: Unify the saved acknowledgement action**

Remove the manual-only two-button `Row` and the non-manual `accountRecoveryKeyContinue` button. Use two full-width stacked actions for every purpose so there is exactly one copy selector and exactly one save selector in the object tree:

```qml
AccountButton {
    objectName: "accountRecoveryKeyCopy"
    width: parent.width
    text: "Copy recovery key"
    onClicked: root.copyPresentedKey()
}

Item { width: 1; height: 10 }

AccountButton {
    objectName: "accountRecoveryKeySaved"
    width: parent.width
    text: "I saved it"
    variant: "primary"
    onClicked: root.finishPresentedKey()
}
```

Delete `accountRecoveryKeyCopyManual`, `accountRecoveryKeySavedManual`, and `accountRecoveryKeyContinue`. Preserve the `finished(purpose)` signal so `AccountOnboarding` still chooses the correct next route after acknowledgement. Copying alone must never call `finishPresentedKey()`.

- [ ] **Step 4: Run QML and presenter regressions**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_key_ack.ps1
cmake --build native/build-msvc --target tst_account_onboarding
.\native\build-msvc\tst_account_onboarding.exe
```

Expected: PASS; presenter one-time secret handling remains unchanged.

- [ ] **Step 5: Commit**

```powershell
git add qml/account/AccountRecoveryKey.qml qml/account/AccountOnboarding.qml tests/account_recovery_key_ack_harness.qml tests/test_account_recovery_key_ack.ps1 tests/auto/account_onboarding/tst_account_onboarding.cpp
git commit -m "fix(account): require recovery key acknowledgement"
```

---

### Task 5: Offline/recovery UX integration gate

**Files:**
- Verify only: files from Tasks 1-4

**Interfaces:**
- Produces: reviewer-ready offline/recovery UX repair branch.

- [ ] **Step 1: Run all new QML regressions**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_offline_shell_identity.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_offline_center.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_policy.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_account_recovery_key_ack.ps1
```

Expected: all PASS.

- [ ] **Step 2: Run native onboarding/identity suites**

```powershell
cmake --build native/build-msvc --target tst_account_onboarding tst_account_identity
.\native\build-msvc\tst_account_onboarding.exe
.\native\build-msvc\tst_account_identity.exe
```

Expected: 0 failures.

- [ ] **Step 3: Run production password tests**

```powershell
Push-Location server/account-service
go test ./internal/account -run 'Password|Recovery' -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 4: Check patch hygiene**

```powershell
git diff --check
git status --short
git log --oneline --decorate -6
```

Expected: clean diff check and focused commits.

- [ ] **Step 5: Record reviewer evidence**

Record screenshots or harness output proving offline username + Sign out behavior, the 8/7/129 code-point boundary results, and all four recovery-key purposes requiring explicit acknowledgement.
