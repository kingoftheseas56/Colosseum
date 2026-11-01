# Account secure storage checks

The production runtime selects Windows Credential Manager, Linux libsecret
(Secret Service), or Android Keystore. The shared store retains the existing
Windows credential targets and session encoding. There is no plaintext fallback.
Linux requires libsecret 0.19 or newer and a running desktop Secret Service;
a locked/unavailable service fails closed. Each libsecret operation is cancelled
after five seconds to bound the existing synchronous account API.

Android uses a non-exportable AES-256 key from Android Keystore and AES-GCM with
a fresh provider-generated IV for each write. Records are bound to their hashed
file names as authenticated data, written atomically, and kept in the app's
no-backup directory. A missing key with existing ciphertext fails closed rather
than replacing the key. Android API 28 or newer matches the current Qt kits.
The current platform factory remains the runtime owner. Earlier Android active
sessions and revocation queues remain readable from their existing encrypted
vault; successful writes retire the corresponding old entry. Corrupt new records
never fall back to an older session, and deletion clears both locations.

Build the focused desktop tests with a Qt 6 Core/Network/Test development kit:

```sh
cmake -S tests/account_secure_storage -B build/account-storage -G Ninja
cmake --build build/account-storage
ctest --test-dir build/account-storage --output-on-failure
```

The platform test uses a generated `COLOSSEUM_APPDATA_TAG`, writes fixture
credentials, reads them in a second process, removes them in a third process,
and checks absence in a fourth. It covers active sessions, pending revocations,
pending deletion capabilities, and profile/account-bound Stremio credentials.
It does not sign in to the production service or touch daily credentials.

On headless Linux install Qt 6 development packages, `libsecret-1-dev`,
`gnome-keyring`, and `dbus-daemon`, then run:

```sh
sh tests/account_secure_storage/run-linux-tests.sh build/account-storage
```

This starts a private D-Bus session with a disposable test keyring. The suite
also checks failure when the session bus is unavailable and runs the existing
account identity/controller fixture tests. The Linux script additionally locks
the test keyring, verifies that deletion reports failure and all four locked
entries remain, and discards the private keyring. The shared contract test is also
registered in the main repository CTest graph.

For Android, configure this same focused CMake project with the Qt Android
toolchain, host Qt, SDK and NDK, then build
`tst_account_secure_storage_make_apk`. A debug APK has the isolated package
`org.brotherhood.colosseum.accountstoragetest`. Run the contract suite normally,
then launch separate app processes with `--seed`, `--verify`, `--tamper`,
`--verify`, `--key-loss`, `--clear`, and `--empty` via Qt's debug
`extraappparams` intent extra (base64 encoded). Each step reports
`ACCOUNT_STORAGE_MODE <mode> PASS` or `FAIL` in logcat. These modes are test-only;
the key-loss check deletes the test package's key, never the product app's key.
The complete restart/tamper/key-loss sequence can be run with:

```sh
python tests/account_secure_storage/run-android-tests.py --serial emulator-5554 --apk path/to/debug-test.apk
```

These checks qualify the storage component and deterministic controller flows.
Full app packaging, UI behavior, and live server sign-in remain separate checks.

Verification on 2026-10-21:

- Windows: 3/3 focused CTest registrations passed, including real tagged
  Credential Manager persistence. The shared contract also passed through the
  main application CMake/CTest graph.
- Debian Linux: 4/4 focused CTest registrations passed against a private real
  Secret Service, plus the locked-deletion regression.
- Both desktop controller suites: 58 passed, zero failures, one intentional
  live refresh-response-loss probe skipped.
- Android x86_64 emulator: twelve contract checks and twelve native-storage app
  launches passed, including process restarts, tampering, key loss and cleanup.
  The contract includes rotation from the earlier real Android Java vault and
  failure to retire an old entry; native launches use the production factory.
  The ARM64 production storage library also cross-compiled successfully.
- No full product build, physical Android device test, or live production
  account sign-in was performed for this change.
