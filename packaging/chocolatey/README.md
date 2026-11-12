# Colosseum — Chocolatey Community Repository package

This directory is the source for the `colosseum` Chocolatey package. It does **not** embed the
installer; it downloads the official GitHub release asset at install time and verifies it by
SHA-256 checksum.

## Package facts (v1.3.0)

| Field | Value |
|---|---|
| Package ID | `colosseum` |
| Version | `1.3.0` |
| Installer URL | `https://github.com/kingoftheseas56/Colosseum/releases/download/v1.3.0/Colosseum-1.3.0-setup.exe` |
| Size | 231,444,357 bytes |
| SHA-256 | `665fdcc923f6ab70f32377a77082a2821af2ef5bd0d8c055c529df9b69675013` |
| Installer type | NSIS (MUI2) |
| Silent install | `/S` |
| Silent uninstall | `/S` (via `uninstall.exe`) |
| Install scope | Per-user, `%LOCALAPPDATA%\Programs\Colosseum`, **no admin** (`RequestExecutionLevel user`) |
| Uninstall registry | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum` |
| License | MIT |
| Icon | jsDelivr CDN, pinned to tag `v1.3.0` → `assets/icons/colosseum.svg` |

## Files

- `colosseum.nuspec` — package metadata.
- `tools/chocolateyinstall.ps1` — downloads + verifies + silently installs.
- `tools/chocolateyuninstall.ps1` — discovers the per-user uninstall entry and runs it silently.
- `tools/VERIFICATION.txt` — how a moderator/user verifies the downloaded binary.
- `tools/LICENSE.txt` — MIT license text (mirrors the repo `LICENSE`).

## Per-user install caveat (read before submitting)

Colosseum installs **per user** into `%LOCALAPPDATA%\Programs\Colosseum`. Chocolatey usually
runs elevated, so `$LOCALAPPDATA` resolves to the **elevated account's** profile, and shortcuts +
the `HKCU` uninstall entry are written there. This is expected for per-user installers and is called
out in the package description. A user who wants the app under their own profile should run
`choco install colosseum` from a non-elevated shell (Chocolatey supports non-admin installs).

Uninstalling removes the application's install folder, including any media stored inside it.

## Validation performed

- v1.1.3 (first package): `choco pack`, an isolated silent install (`/S`, `/D=`) and silent uninstall
  into a scratch directory, and `Get-UninstallRegistryKey -SoftwareName 'Colosseum*'` resolving to
  exactly one entry.
- v1.1.7 bump changes only version, URL, size, checksum and tag pins. The installer passed the
  release workflow's fresh-install, boot and uninstall smoke on a clean Windows runner, and the
  published asset's SHA-256 was re-checked against the 254,834,672-byte download. `choco pack` has
  not been re-run for 1.1.7 (Chocolatey is not installed on the maintainer machine).
- v1.3.0: version, URL, size, SHA-256 and tag pins updated against the canonical
  231,444,357-byte CI installer. Release CI run 43221859124 passed fresh install, boot, uninstall,
  and the 1.2.0-to-1.3.0 upgrade with preserved state. Package source was inspected and parsed;
  `choco pack` and Chocolatey install/submission were not run (Chocolatey is not installed here).

## How to build the package

```powershell
cd packaging\chocolatey
choco pack
```

Produces `colosseum.1.3.0.nupkg` in this directory.

## How to submit to the Chocolatey Community Repository (Hemanth)

1. Create/sign in to an account at https://community.chocolatey.org and get your API key from
   https://community.chocolatey.org/account.
2. Set `<owners>` in `colosseum.nuspec` to your chocolatey.org account name if it differs from
   `kingoftheseas56`.
3. Register the key locally (one time):
   ```powershell
   choco apikey --key <YOUR_API_KEY> --source https://push.chocolatey.org/
   ```
4. Push (this is the publish step — do not run until you intend to submit):
   ```powershell
   choco push colosseum.1.3.0.nupkg --source https://push.chocolatey.org/
   ```
5. The package then enters Chocolatey moderation. Expect automated validation/verification checks;
   the `VERIFICATION.txt` and pinned checksum are there to satisfy them.

## Updating for a future release

1. Bump `<version>` in `colosseum.nuspec`.
2. Update the URL, size, and SHA-256 in `tools/chocolateyinstall.ps1` and `tools/VERIFICATION.txt`.
3. Update the `@v<tag>` pin in the `iconUrl` and `licenseUrl`.
4. `choco pack`, re-test, then `choco push`.
