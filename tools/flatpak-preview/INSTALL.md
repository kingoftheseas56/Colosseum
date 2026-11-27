# Colosseum 1.5.0 — Flatpak Preview

Linux x86-64 preview, distributed directly by Colosseum through GitHub. Install
Flatpak using your Linux distribution's instructions first. A graphical desktop
session and a working desktop file portal are required for the normal app and
system file picker.

## Install

Download `Colosseum-1.5.0-x86_64-preview.flatpak` and `SHA256SUMS-Flatpak` from the
[Colosseum 1.5.0 release](https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.5.0).
In the directory containing those files:

```sh
sha256sum --check SHA256SUMS-Flatpak
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.11
flatpak install --user ./Colosseum-1.5.0-x86_64-preview.flatpak
```

Flathub supplies the shared KDE runtime. The Colosseum application itself is the
file downloaded from GitHub. These instructions install it for your user account.

Launch **Colosseum (Flatpak Preview)** from your application menu, or run:

```sh
flatpak run io.github.kingoftheseas56.Colosseum//preview-1.5.0
```

Use **Open Media** or **Ctrl+O** to select a local file through the system picker.
The package has normal Downloads-folder access; the picker grants access to
selected files elsewhere.

## Preview coverage and limits

- Installed Home, sandboxed WebEngine, profile retention after restart, Player 1
  video/audio/seeking, desktop-entry launch and file-portal playback passed on
  Debian 13 with a virtual display and software audio output.
- Physical GPU/speakers, other distributions' desktop environments and live
  media-provider workflows remain unqualified.
- Optional DLNA sharing is unavailable in this package.
- This is a standalone bundle. To update this preview, download the next Flatpak
  bundle and install it using the same user account. Keep the application installed
  to retain its data. Use `flatpak install --user --reinstall ./NEW-FILE.flatpak`
  with the newly downloaded filename for preview updates.

## Remove

```sh
flatpak uninstall --user io.github.kingoftheseas56.Colosseum//preview-1.5.0
```

Do not request deletion of app data if you want to retain your profile for later.

## Source and licenses

`Colosseum-1.5.0-Flatpak-source-provenance.zip` and the Flatpak provenance JSON
are alongside this download. Original Colosseum code retains MIT terms. The
distribution includes GPL-enabled mpv and its corresponding distribution/source
materials; Qt, BaseApp and other third-party notices are retained. Installed
notices are under `/app/share/licenses/io.github.kingoftheseas56.Colosseum/` inside
the sandbox. No application source or compiled library was rebuilt during the
preview packaging promotion.
