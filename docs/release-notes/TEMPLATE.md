# Colosseum <version>

<!-- Fill from the reviewed release diff. Do not publish candidate claims before qualification. -->

## Changes

<Describe the user-visible changes in this release.>

## Linux

Choose the **AppImage** for supported x86_64 Linux distributions, including Fedora, Arch, openSUSE and Debian. It needs glibc 2.35 or newer and a working desktop graphics stack. Download the file, then run:

```sh
chmod +x ./Colosseum-*-x86_64.AppImage
./Colosseum-<version>-x86_64.AppImage
```

Replace `<version>` with the downloaded filename's version. When FUSE is unavailable, add `--appimage-extract-and-run` to the launch command. Run as your normal user, not root.

The **.deb** remains the installer option for supported Ubuntu, Mint and Debian versions meeting its package dependencies. This build retains the Ubuntu 24.04/Mint 22 baseline for the installer; older Ubuntu systems should use the qualified AppImage instead. Updates are manual.

<!-- Before publishing, link the exact green distro matrix and real-desktop checks for this release. Do not claim all distros or GPU/playback qualification from offscreen startup alone. -->

## Known limitations

<List the verified remaining limitations; remove placeholders before publishing.>
