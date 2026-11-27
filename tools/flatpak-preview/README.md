# Colosseum 1.5.0 Flatpak Preview

The standalone preview is distributed as a GitHub release asset. Its application
source remains the `v1.5.0` tag. It is independent of a Flathub application listing.

`promote.py` takes a verified installed Arc 93 candidate, collected license/source
material, and a **new** output directory. It copies the payload, adds the desktop
entry, icon, MetaInfo and distribution notices, removes packaged qualification
drivers, and checks that every original ELF and application QML file is unchanged.
It then exports branch `preview-1.5.0` and a bundle with the KDE runtime repository
reference. It does not rebuild Colosseum or its libraries.

The release source/provenance archive supplies the exact original application
source, library archives/source locations, BaseApp recipes and patches, collected
notices, this tool, and reproduction instructions. The original build uses
GPL-enabled mpv; do not describe this package as an LGPL-only mpv distribution.

The installed-runtime and desktop tests accept `COLOSSEUM_FLATPAK_REF`; the desktop
test also accepts `COLOSSEUM_FLATPAK_LAUNCHER_QUERY`. For a promoted bundle, place
the original RuntimeProof.qml outside the package in its app-data directory and
pass that path through `COLOSSEUM_FLATPAK_RUNTIME_PROOF`. Run qualification in an
isolated X11/D-Bus session with a normal `/run/user/$UID` runtime directory. Keep
qualification drivers out of the published application payload.
Use a new `COLOSSEUM_FLATPAK_TEST_TAG` for a fresh-profile qualification run;
the first-launch assertion intentionally rejects an already-onboarded profile.

The preview is qualified with software graphics/audio. Physical GPU/speakers and
live-provider workflows remain outside that qualification. DLNA sharing is not
compiled into this package. Updates to this standalone preview are installed by
downloading and installing the next Flatpak bundle.
