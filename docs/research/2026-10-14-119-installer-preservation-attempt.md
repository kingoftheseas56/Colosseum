# 1.1.9 installer preservation attempt

Date: 2026-10-14. Candidate: `1e3b65d414e340224c916d4c37140e2ffa26edde`, branch `codex/d1-119-no-account`. This records source review and compiled disposable fixtures only. **Installer upgrade preservation remains unverified.** No application/test code was changed and no full application was built.

## Artifact provenance

| Artifact | Bytes | Verified SHA256 | Meaning |
| --- | --- | --- | --- |
| Existing published `Colosseum-1.1.8-setup.exe` | 255013376 | `34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e` | Available older installer; not executed |
| Existing qualification `Colosseum-1.1.9-setup.exe` | 327212555 | `15f4978b8ecc75cd9c4e3c804bf00a6c6dca41f022be67444da361c774e0c8b9` | Receipt source `d7f86f4411300f5df840377eaca7ff5859ae2879`; verified hash matches receipt; not this candidate's payload |
| Candidate `scripts/installer/colosseum.nsi` | 12134 | `4c5be024fbf4fc8a113167b3405611f0845b860b4a4d45aa708869c2c1f742a4` | Unchanged from that release base |
| Compiled `fixture-1.1.8.exe` | 112834 | `4170b302f6a002323e2d415146dfa91e06ca665531dfefe13434a1a4994ef22a` | Candidate script with versioned sentinel payload, not the published installer |
| Compiled `fixture-1.1.9.exe` | 112883 | `e5c6a1e475a98c0920caabf881b0971222cb157707054b76b293b04b3e266e33` | Candidate script with versioned sentinel payload, not a shipping candidate |
| Temporary QtCore `state_reader.exe` | 26624 | `888505ea1bfe12f8555e73a1441b360f2b6eee75af5863c1fba90b279bdaf76b` | Compiled explicit-path QSettings seed/read helper; not executed |

No built application or full installer for the current candidate was found in its checkout. Windows Sandbox was unavailable. Running the existing shipping installer directly would modify real per-user uninstall metadata and shortcuts, so it was not attempted.

## Isolation review and compilation

The selected intended path was manual `/S`, matching the existing manual migration route. NSIS lines `28-44` expose test install-root, registry-key and unique shortcut-prefix overrides. All installer registry reads/writes/deletes use `COLOSSEUM_REGKEY`; all shortcuts use the overridden names. Manual refresh at `133-143` removes only the isolated installation's managed QML tree. Uninstall at `324-333` uses the same isolated install root, shortcut names and registry key. There are no scheduled tasks or process-kill operations. The synchronization-only process wait and default update log are confined to `/UPDATE`; no `/UPDATE` behavior is claimed here. The test-mode define also suppresses updater executable relaunch. Silent install skips the interactive finish-page launch.

Both fixture compiles exited 0 with the unchanged candidate script: `makensis /DSTAGE=<sentinel-stage> /DVERSION=<1.1.8 or 1.1.9> /DOUTFILE=<fixture-exe> /DCOLOSSEUM_UPDATE_TEST_ROOT=<disposable-root> /DCOLOSSEUM_UPDATE_TEST_REGKEY=<unique-test-key> /DCOLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX=<unique-test-prefix> /DCOLOSSEUM_UPDATE_TEST_MODE=1 scripts/installer/colosseum.nsi`. The temporary QtCore helper configure/build exited 0 using MSVC 2022 / Qt 6.11.1; invocation: `cmake --build <temporary-build> --config Release --target state_reader`.

## Execution blocked before launch

The attempted PowerShell command would run `Start-Process <fixture-1.1.8.exe> /S -Wait -PassThru -WindowStyle Hidden`, seed explicit fixture INI/isolated-registry Progress/Collection/History and reader JSON, run the manual 1.1.9 fixture upgrade, compare file hashes and parsed QSettings/reader values, then remove only its own test registry key and uniquely named shortcuts. Automatic approval review rejected creation of that PowerShell process with the exact reason **"blocked by policy"**. No further reason was provided. The launch path was stopped without retry or bypass.

No installer, seed helper, upgrade or post-upgrade readback executed. The planned fixture install root and before-state output were confirmed absent. There are therefore **no before/after data values, preservation hashes or application readability results**. The compiled fixtures, helper source/build and compile logs were retained as temporary evidence outside the repository. Biblio account carry-over plumbing remains stopped; even a later on-device file preservation result would not promise Biblio account migration.

Remaining installer gates: a full current-candidate payload/package with provenance, an authorized isolated execution environment, real installed-to-candidate upgrade, before/after canonical local data checks, application readback and final installer/updater receiving qualification. Existing core adoption component evidence is separate and remains accepted. This attempt did not access Neon or activate, sign, merge, tag, publish or release anything.

Astra personally reviewed this note and the exact NSIS isolation, manual-refresh and uninstall source excerpts and accepts it as accurate blocked-attempt evidence only; installer preservation remains unverified.
