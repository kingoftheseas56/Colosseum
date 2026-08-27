# Account Network and Authentication Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bound every desktop account request and remove the service-side username-enumeration timing shortcut while preserving existing rate limits and retry semantics.

**Architecture:** Carry an explicit timeout on each `AccountTransportRequest`; `AccountClient` selects ordinary versus long-poll deadlines and `AccountHttpTransport` enforces them using Qt's transfer timeout. On the service, syntactically valid unknown-account sign-ins perform the same Argon2 verification class as wrong-password sign-ins by checking a startup-generated dummy hash.

**Tech Stack:** Qt 6.11.1 networking/Qt Test, C++20, Go, Argon2id.

**Spec:** `docs/superpowers/specs/2026-08-28-account-system-hardening-design.md`

## Global Constraints

- Ordinary account requests use 15,000 ms.
- Approval long polls use `(waitSeconds * 1000) + 10,000 ms`, bounded to 10,000-35,000 ms.
- Timeout failures use the existing `networkError` reply path; do not invent a parallel controller state machine.
- Sign-in continues to return the same `invalid_credentials` response for unknown usernames and wrong passwords.
- Existing source/identity rate limits remain intact.
- Every change follows RED -> GREEN TDD and ends in a focused commit.

---

### Task 1: Put an explicit deadline on transport requests

**Files:**
- Modify: `native/account/AccountTransport.h:12-18`
- Modify: `native/account/AccountHttpTransport.cpp`
- Test: `tests/auto/account_identity/tst_account_identity.cpp`

**Interfaces:**
- Produces: `AccountTransportRequest::timeoutMs` with a positive millisecond deadline.
- Consumes: `timeoutMs` in `AccountHttpTransport::send()` and maps Qt timeout completion into `AccountTransportReply{networkError=true}`.

- [ ] **Step 1: Write a failing HTTP transport timeout test**

Add `httpTransportTimesOutStalledReply()` to `tst_account_identity.cpp`. Start a local `QTcpServer`, accept the connection, read the HTTP request, then deliberately send no HTTP response. Send:

```cpp
AccountTransportRequest request;
request.method = QByteArrayLiteral("GET");
request.path = QStringLiteral("/stall");
request.timeoutMs = 100;
transport.send(77, request);
```

Wait with `QSignalSpy` and assert:

```cpp
QVERIFY(finishedSpy.wait(2000));
const auto reply = qvariant_cast<AccountTransportReply>(finishedSpy.at(0).at(1));
QVERIFY(reply.networkError);
QCOMPARE(reply.statusCode, 0);
```

- [ ] **Step 2: Run the test and verify RED**

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe httpTransportTimesOutStalledReply
```

Expected: compile failure because `timeoutMs` does not exist, or runtime hang/failure before implementation.

- [ ] **Step 3: Add the request timeout field and enforce it**

In `AccountTransport.h`:

```cpp
struct AccountTransportRequest {
    QByteArray method;
    QString path;
    QJsonObject body;
    QByteArray bearerToken;
    int timeoutMs = 15000;
};
```

In `AccountHttpTransport::send()`, before dispatching the `QNetworkRequest`, apply:

```cpp
request.setTransferTimeout(qMax(1, accountRequest.timeoutMs));
```

Keep the existing `QNetworkReply` error mapping. Do not special-case `OperationCanceledError`; Qt transfer timeout should surface through the same network-error completion seam.

- [ ] **Step 4: Run the timeout and existing transport safety tests**

```powershell
.\native\build-msvc\tst_account_identity.exe httpTransportTimesOutStalledReply
.\native\build-msvc\tst_account_identity.exe httpTransportRejectsUnsafeBaseUrls
.\native\build-msvc\tst_account_identity.exe httpTransportDoesNotFollowRedirects
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add native/account/AccountTransport.h native/account/AccountHttpTransport.cpp tests/auto/account_identity/tst_account_identity.cpp
git commit -m "fix(account): bound transport requests"
```

---

### Task 2: Assign ordinary and long-poll deadlines in AccountClient

**Files:**
- Modify: `native/account/AccountClient.h`
- Modify: `native/account/AccountClient.cpp:322-410`
- Test: `tests/auto/account_identity/tst_account_identity.cpp`

**Interfaces:**
- Consumes: `AccountTransportRequest::timeoutMs` from Task 1.
- Produces: 15,000 ms for normal calls and 10,000-35,000 ms for `listApprovals(waitSeconds)`.

- [ ] **Step 1: Write request-capture tests**

Add `accountClientAssignsRequestDeadlines()` using the existing `CapturingTransport` at the top of `tst_account_identity.cpp`:

```cpp
CapturingTransport transport;
AccountClient client(&transport);

client.signIn(
    QStringLiteral("DeadlineOwner"),
    QStringLiteral("password-value"),
    QStringLiteral("install-1"),
    QStringLiteral("Test PC"),
    QStringLiteral("Windows"));
QCOMPARE(transport.lastRequest.timeoutMs, 15000);

client.listApprovals(0);
QCOMPARE(transport.lastRequest.timeoutMs, 10000);

client.listApprovals(25);
QCOMPARE(transport.lastRequest.timeoutMs, 35000);
```

`CapturingTransport::send()` already stores the exact `AccountTransportRequest`, so no new fixture API is required.

- [ ] **Step 2: Run targeted core tests and verify RED**

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe accountClientAssignsRequestDeadlines
```

Expected: FAIL because `AccountClient::send()` does not yet assign the long-poll deadline.

- [ ] **Step 3: Add one timeout parameter to AccountClient::send**

Change the private helper to:

```cpp
quint64 send(
    AccountOperation operation,
    const QByteArray &method,
    const QString &path,
    const QJsonObject &body,
    bool authenticated,
    int timeoutMs = 15000);
```

Set:

```cpp
request.timeoutMs = qMax(1, timeoutMs);
```

For `listApprovals` compute:

```cpp
const int timeoutMs = qBound(
    10000,
    waitSeconds * 1000 + 10000,
    35000);
```

and pass it to `send(...)`. Leave every other call on 15,000 ms.

- [ ] **Step 4: Run core and identity suites**

```powershell
cmake --build native/build-msvc --target tst_account_identity
.\native\build-msvc\tst_account_identity.exe
```

Expected: 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add native/account/AccountClient.h native/account/AccountClient.cpp tests/auto/account_identity/tst_account_identity.cpp
git commit -m "fix(account): set operation request deadlines"
```

---

### Task 3: Equalize unknown-user and wrong-password verification work

**Files:**
- Modify: `server/account-service/internal/account/service.go:34-101`
- Modify: `server/account-service/internal/account/identity.go:214-270`
- Test: `server/account-service/internal/account/identity_integration_test.go`

**Interfaces:**
- Produces: one startup `dummyPasswordHash` and one package-private `passwordVerify` function seam.
- Consumes: `PasswordHasher.Hash` and `PasswordHasher.Verify` with the approved Argon2 parameters.

- [ ] **Step 1: Write a deterministic verification-count regression**

In `identity_integration_test.go`, create a normal service fixture, then replace a package-private verification function seam:

```go
calls := 0
fixture.service.passwordVerify = func(encoded, password string) (bool, error) {
    calls++
    return false, nil
}
```

Call `SignIn` with a syntactically valid username that does not exist. Assert:

```go
if !errors.Is(err, ErrInvalidCredentials) {
    t.Fatalf("error = %v, want ErrInvalidCredentials", err)
}
if calls != 1 {
    t.Fatalf("password verify calls = %d, want 1", calls)
}
```

Add a second case for an existing account with a wrong password and assert the same single verification count.

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
Push-Location server/account-service
go test ./internal/account -run TestSignInAlwaysPerformsPasswordVerification -count=1
Pop-Location
```

Expected: RED because the unknown-user branch returns before verification and the seam does not yet exist.

- [ ] **Step 3: Generate a dummy hash at service startup**

Add to `Service`:

```go
dummyPasswordHash string
passwordVerify    func(encoded, password string) (bool, error)
```

In `NewService`, before returning the service:

```go
dummyHash, err := dependencies.PasswordHasher.Hash(
    "colosseum-auth-dummy-password-not-a-user-secret")
if err != nil {
    return nil, fmt.Errorf("build dummy password hash: %w", err)
}
```

Initialize:

```go
dummyPasswordHash: dummyHash,
passwordVerify: dependencies.PasswordHasher.Verify,
```

The dummy plaintext is a fixed non-secret workload input. Only its random-salted Argon2 hash is retained.

- [ ] **Step 4: Make SignIn verify before returning unknown-user credentials failure**

Normalize the presented password before account lookup. On `ErrInvalidCredentials` from `loadAuthAccountByCanonical`, run:

```go
if _, verifyErr := s.passwordVerify(s.dummyPasswordHash, password); verifyErr != nil {
    return SignInResult{}, fmt.Errorf("verify dummy password hash: %w", verifyErr)
}
return SignInResult{}, ErrInvalidCredentials
```

For existing accounts replace the direct hasher call with:

```go
valid, err := s.passwordVerify(account.PasswordHash, password)
```

Do not change the public error code, status, or rate-limit ordering.

- [ ] **Step 5: Run security regression and full Go suite**

```powershell
Push-Location server/account-service
go test ./internal/account -run TestSignInAlwaysPerformsPasswordVerification -count=1
go test ./... -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add server/account-service/internal/account/service.go server/account-service/internal/account/identity.go server/account-service/internal/account/identity_integration_test.go
git commit -m "fix(account): equalize sign-in credential work"
```

---

### Task 4: Network/auth hardening integration gate

**Files:**
- Verify only: files from Tasks 1-3

**Interfaces:**
- Produces: bounded desktop requests and timing-hardened sign-in behavior.

- [ ] **Step 1: Run all native account core/identity tests**

```powershell
cmake --build native/build-msvc --target tst_account_core tst_account_identity
.\native\build-msvc\tst_account_core.exe
.\native\build-msvc\tst_account_identity.exe
```

Expected: 0 failures.

- [ ] **Step 2: Run full production account-service tests**

```powershell
Push-Location server/account-service
go test ./... -count=1
Pop-Location
```

Expected: PASS.

- [ ] **Step 3: Run mock self-test to catch client-side timeout regressions indirectly**

```powershell
node tests/mock-account-service/server.mjs --selftest
```

Expected: PASS.

- [ ] **Step 4: Check patch hygiene**

```powershell
git diff --check
git status --short
git log --oneline --decorate -5
```

Expected: clean diff check and only intended files.

- [ ] **Step 5: Record verification evidence**

Record the stalled-response timeout duration observed by the Qt test, the ordinary/long-poll timeout values, and the deterministic `passwordVerify` call-count results for unknown and existing usernames.
