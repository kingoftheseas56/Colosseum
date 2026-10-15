# Account Protocol Contract Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the desktop client, production Go account service, and test mock agree on account/profile fields and fail closed on malformed approval payloads.

**Architecture:** Treat the production Go API as the canonical wire contract. The C++ controller translates canonical wire fields into its existing safe QML properties, while the mock mirrors the same wire shape instead of papering over client drift. Protocol payloads that cannot be trusted remain errors and never silently become empty security state.

**Tech Stack:** Qt 6.11.1 C++/Qt Test, QJsonObject/QJsonArray, Go `net/http`, Node.js mock service.

**Spec:** `docs/superpowers/specs/2026-08-28-account-system-hardening-design.md`

## Global Constraints

- Accounts remain optional; local-only mode must keep working.
- Do not deploy or bake a production account-service URL.
- Preserve secure-store fail-closed behavior and stale-request generation guards.
- `builtin_avatar_id` is the canonical service field; `avatar_id` is compatibility-only on read.
- Malformed security payloads must fail closed without destroying the last known-good UI state.
- Every change follows RED -> GREEN TDD and ends in a focused commit.

---

### Task 1: Make the desktop consume the canonical avatar field

**Files:**
- Modify: `native/account/AccountController.cpp:1075-1126`
- Test: `tests/auto/account_identity/tst_account_identity.cpp`

**Interfaces:**
- Consumes: account-shaped reply bodies from `AccountClient`.
- Produces: unchanged QML property `AccountController::avatarId()` populated from canonical `builtin_avatar_id`, with legacy `avatar_id` read fallback.

- [ ] **Step 1: Write the failing native regression**

Add a Qt Test case named `profileUsesCanonicalBuiltinAvatarField()` that feeds the fixture transport a successful `GetProfile` reply containing only:

```cpp
QJsonObject{
    {QStringLiteral("id"), QStringLiteral("account-1")},
    {QStringLiteral("username"), QStringLiteral("AvatarOwner")},
    {QStringLiteral("builtin_avatar_id"), QStringLiteral("laurel")},
    {QStringLiteral("protect_new_device_signins"), true},
};
```

Call `fixture.controller->refreshProfile()`, complete the fixture request, and assert:

```cpp
QCOMPARE(fixture.controller->avatarId(), QStringLiteral("laurel"));
```

Also retain one compatibility assertion where `avatar_id` alone still populates `avatarId()`.

- [ ] **Step 2: Run the targeted test and verify RED**

Run:

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe profileUsesCanonicalBuiltinAvatarField
```

Expected: FAIL because current controller ignores `builtin_avatar_id`.

- [ ] **Step 3: Implement one canonical account-avatar reader**

In the anonymous namespace of `AccountController.cpp`, add:

```cpp
QString accountAvatarId(const QJsonObject &body) {
    const QString canonical = body
        .value(QStringLiteral("builtin_avatar_id"))
        .toString();
    if (!canonical.isEmpty())
        return canonical;
    return body.value(QStringLiteral("avatar_id")).toString();
}
```

Use this helper in the `GetProfile`, `RenameUsername`, and `SetBuiltinAvatar` success paths instead of directly reading `avatar_id`. Do not rename the public QML property `avatarId`.

- [ ] **Step 4: Run the targeted test and verify GREEN**

Run the same target and test method. Expected: PASS for canonical and legacy field cases.

- [ ] **Step 5: Commit**

```powershell
git add native/account/AccountController.cpp tests/auto/account_identity/tst_account_identity.cpp
git commit -m "fix(account): consume canonical avatar field"
```

---

### Task 2: Pin the production Go response contract

**Files:**
- Create: `server/account-service/internal/httpserver/account_contract_test.go`
- Verify: `server/account-service/internal/httpserver/account_handlers.go:82-95,568-581`

**Interfaces:**
- Consumes: `account.Account` / `account.Profile` values.
- Produces: JSON account/profile responses containing `builtin_avatar_id`, never `avatar_id`.

- [ ] **Step 1: Write the contract test**

Test `encodeProfile` and JSON marshaling, not just the Go struct fields:

```go
func TestAccountResponseUsesCanonicalBuiltinAvatarID(t *testing.T) {
    profile := account.Profile{Account: account.Account{
        ID: "account-1",
        DisplayUsername: "AvatarOwner",
        BuiltinAvatarID: "laurel",
    }}
    payload, err := json.Marshal(encodeProfile(profile))
    if err != nil { t.Fatal(err) }
    var got map[string]any
    if err := json.Unmarshal(payload, &got); err != nil { t.Fatal(err) }
    if got["builtin_avatar_id"] != "laurel" {
        t.Fatalf("builtin_avatar_id = %#v", got["builtin_avatar_id"])
    }
    if _, exists := got["avatar_id"]; exists {
        t.Fatal("stale avatar_id alias leaked into production API")
    }
}
```

- [ ] **Step 2: Run the focused Go package**

```powershell
Push-Location server/account-service
go test ./internal/httpserver -run TestAccountResponseUsesCanonicalBuiltinAvatarID -count=1
Pop-Location
```

Expected: PASS on current service. This is a characterization test that locks the canonical service contract before changing the mock.

- [ ] **Step 3: Run the full Go service tests**

```powershell
Push-Location server/account-service
go test ./... -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 4: Commit**

```powershell
git add server/account-service/internal/httpserver
git commit -m "test(account): pin avatar response contract"
```

---

### Task 3: Stop the mock from masking production contract drift

**Files:**
- Modify: `tests/mock-account-service/server.mjs`
- Modify: `tests/mock-account-service/README.md:86-166`

**Interfaces:**
- Consumes: canonical production account/profile contract from Task 2.
- Produces: mock `/v1/profile`, rename, avatar, protection and session account objects using `builtin_avatar_id`.

- [ ] **Step 1: Make the mock self-test demand the canonical field**

In the self-test profile/avatar assertions, require:

```js
assert(profile.body.builtin_avatar_id === "laurel", `unexpected builtin_avatar_id ${profile.body.builtin_avatar_id}`)
assert(!Object.hasOwn(profile.body, "avatar_id"), "mock must not emit stale avatar_id")
```

Add the same canonical assertion after the builtin-avatar mutation.

- [ ] **Step 2: Run self-test and verify RED**

```powershell
node tests/mock-account-service/server.mjs --selftest
```

Expected: FAIL because the mock currently emits `avatar_id`.

- [ ] **Step 3: Change the mock account encoder**

Replace mock account/profile output shaped like:

```js
{ avatar_id: account.avatarId }
```

with:

```js
{ builtin_avatar_id: account.avatarId }
```

Do not emit both names. The native compatibility fallback exists for old servers, not to keep the mock ambiguous.

Update README endpoint tables and remove the old text that explicitly says the mock intentionally follows the C++ `avatar_id` assumption.

- [ ] **Step 4: Run self-test and native avatar regression**

```powershell
node tests/mock-account-service/server.mjs --selftest
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe profileUsesCanonicalBuiltinAvatarField
```

Expected: both PASS.

- [ ] **Step 5: Commit**

```powershell
git add tests/mock-account-service/server.mjs tests/mock-account-service/README.md
git commit -m "test(account): align mock with production avatar contract"
```

---

### Task 4: Reject malformed approval lists instead of showing zero approvals

**Files:**
- Modify: `native/account/AccountController.cpp:1190-1215`
- Test: `tests/auto/account_identity/tst_account_identity.cpp`

**Interfaces:**
- Consumes: `ListApprovals` reply body `{ "approvals": [...] }`.
- Produces: `approvalRequestsChanged(array)` only for a genuine JSON array; malformed 2xx response produces protocol error while preserving the previously emitted list.

- [ ] **Step 1: Write the malformed-payload regression**

Seed one known-good approval list through the fixture, then return HTTP 200 with:

```cpp
QJsonObject{
    {QStringLiteral("approvals"), QStringLiteral("not-an-array")},
};
```

Use `QSignalSpy` for `approvalRequestsChanged` and `accountError`. Assert:

```cpp
QCOMPARE(approvalSpy.count(), 1); // only the known-good emission
QCOMPARE(fixture.controller->errorCategory(), QStringLiteral("protocol"));
QCOMPARE(fixture.controller->lastErrorCode(), QStringLiteral("invalid_approvals_payload"));
QCOMPARE(errorSpy.count(), 1);
```

- [ ] **Step 2: Run the test and verify RED**

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe malformedApprovalListIsProtocolFailure
```

Expected: FAIL because `.toArray()` currently converts the malformed value to an empty array and emits it.

- [ ] **Step 3: Add explicit JSON type validation**

Change the `AccountOperation::ListApprovals` success path to:

```cpp
const QJsonValue approvalsValue =
    reply.body.value(QStringLiteral("approvals"));
if (!approvalsValue.isArray()) {
    setError(
        ErrorCategory::Protocol,
        QStringLiteral("invalid_approvals_payload"),
        QStringLiteral("The account service returned an invalid approval list."));
    scheduleApprovalPoll(kApprovalRetryMs);
    return;
}
emit approvalRequestsChanged(approvalsValue.toArray());
scheduleApprovalPoll();
```

Do not emit an empty replacement list on the malformed branch.

- [ ] **Step 4: Run targeted and adjacent device/protocol tests**

```powershell
.\native\build-msvc\tst_account_identity.exe malformedApprovalListIsProtocolFailure
.\native\build-msvc\tst_account_identity.exe malformedDeviceListIsProtocolFailureAndPreservesPriorList
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add native/account/AccountController.cpp tests/auto/account_identity/tst_account_identity.cpp
git commit -m "fix(account): reject malformed approval payloads"
```

---

### Task 5: Protocol repair integration gate

**Files:**
- Verify only: files changed in Tasks 1-4

**Interfaces:**
- Consumes: all protocol repairs above.
- Produces: one reviewer-ready protocol repair branch.

- [ ] **Step 1: Run native account identity suite**

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe
```

Expected: 0 failures.

- [ ] **Step 2: Run production service tests**

```powershell
Push-Location server/account-service
go test ./... -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 3: Run mock service self-test**

```powershell
node tests/mock-account-service/server.mjs --selftest
```

Expected: all self-test steps PASS.

- [ ] **Step 4: Check patch hygiene**

```powershell
git diff --check
git status --short
git log --oneline --decorate -5
```

Expected: clean diff check; only intended files changed; each prior task has its own commit.

- [ ] **Step 5: Record reviewer evidence**

In the PR/agent-room handoff, include the exact native test count, Go test result, mock self-test count, and the canonical JSON field decision: `builtin_avatar_id` wire -> `avatarId` QML property.
