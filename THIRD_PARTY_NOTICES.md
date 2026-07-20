# Third-Party Software and Distribution Notes

The top-level MIT License applies to original Colosseum source code and project-owned assets. It does not relicense third-party libraries, vendored code, data, artwork, services, or binaries. Those components remain under their respective upstream terms.

This file is a practical inventory, not legal advice and not yet a complete binary-distribution compliance bundle.

## Principal components

| Component | Licensing boundary |
|---|---|
| Qt 6, including Qt Quick, QML, WebEngine, WebChannel, SQL, Network, WebSockets, and Concurrent | Available under Qt's commercial or applicable open-source terms. Open-source distribution generally relies on LGPL components plus separately licensed third-party code. Preserve the license texts and satisfy the source, notice, replacement, and relinking obligations for the exact Qt build shipped. |
| MpvQt | LGPL-2.1-only OR LGPL-3.0-only. |
| mpv / libmpv | GPL-2.0-or-later by default. It is LGPL-2.1-or-later only when built with mpv's GPL-disabled build option. Every Colosseum release must identify which libmpv build it ships. |
| libtorrent-rasterbar | BSD-3-Clause. |
| Boost | Boost Software License 1.0. |
| OpenSSL | Governed by the license terms of the exact OpenSSL version distributed. |
| Vendored Foliate renderer | Derived from the MIT-licensed foliate-js project. The vendored package metadata currently also declares ISC; preserve all upstream notices and verify the provenance of local modifications before distribution. |
| PDF.js | Apache-2.0. |
| Lucide icons | ISC; portions inherited from Feather remain under MIT. The vendored Lucide license file must remain with distributions. |
| Official Stremio Service | GPL-2.0. It runs as a separate process and communicates with Colosseum over localhost, so its license does not by itself relicense Colosseum's original MIT code. If it is bundled, preserve the GPL license and notices and provide the corresponding source in the manner required by GPL-2.0. |
| Legacy `stremio-runtime.exe` + `server.js` pair | A separate external runtime and not covered by Colosseum's MIT License. Trace the exact files to their upstream release and license before redistributing them; do not assume that the Addon SDK's MIT license applies to the streaming runtime. |
| External APIs, addons, indexers, scrapers, catalogs, and media | Independent services and content. They are not part of the MIT-licensed Colosseum source and are not relicensed by this repository. |

## KDE Plasma wallpapers (picker "KDE Plasma" shelf)

These wallpapers ship with KDE Plasma and are used here unmodified, streamed from the
upstream repository at display time (resized via the wsrv.nl proxy over the jsDelivr CDN);
none are committed to this repository. Source: <https://github.com/KDE/plasma-workspace-wallpapers>.

Each is licensed **Creative Commons Attribution-ShareAlike 4.0 (CC-BY-SA-4.0)** or the
**GNU Lesser General Public License v3 (LGPLv3)**, as noted. Full licence texts:
- CC-BY-SA-4.0: <https://creativecommons.org/licenses/by-sa/4.0/>
- LGPL-3.0: <https://www.gnu.org/licenses/lgpl-3.0.html>

| Wallpaper | Artist | Licence | Modified |
|---|---|---|---|
| Cascade | Ken Vermette | LGPLv3 | No |
| Flow | Sandra Smukaste | CC-BY-SA-4.0 | No |
| Kay | ruvkr | CC-BY-SA-4.0 | No |
| Shell | Lucas Andrade | CC-BY-SA-4.0 | No |
| Volna | Nikita Babin | CC-BY-SA-4.0 | No |
| Nexus | Krystian Zajdel | CC-BY-SA-4.0 | No |
| Opal | Ken Vermette | LGPLv3 | No |
| Cold Ripple | Risto Saukonpää | LGPLv3 | No |
| Elarun | Nuno Pinheiro | LGPLv3 | No |
| Ice Cold | Santiago Cézar | CC-BY-SA-4.0 | No |
| Pastel Hills | Lionel | LGPLv3 | No |
| Honeywave | Ken Vermette | CC-BY-SA-4.0 | No |
| Kokkini | Ken Vermette | LGPLv3 | No |

"Modified: No" means Colosseum applies these as artwork without altering the images. If a
wallpaper is ever adapted, its adaptation must remain under the same licence (the ShareAlike
term applies to the image, not to Colosseum's own code).

## Ported QML wallpaper scenes (Colosseum → "Colosseum" shelf)

| Scene | Ported from | Upstream licence |
|---|---|---|
| AuroraFlow (`qml/wallpapers/AuroraFlow.qml`) | VicenteMcMahon/kde-plasma-gradient-wallpaper (<https://github.com/VicenteMcMahon/kde-plasma-gradient-wallpaper>) | LGPL-2.1-or-later |

These are QML wallpaper scenes adapted from KDE Plasma wallpaper plugins for a plain Qt6
build (KDE-specific APIs removed). As adaptations of LGPL-2.1 source, the adapted scene
files carry that licence; a per-file SPDX header records the provenance.

## Windows release gate

Before publishing an installer or portable package:

1. Confirm whether the included `libmpv` build is LGPL-enabled or GPL. A normal GPL mpv build can impose GPL distribution terms on the combined application; using an LGPL-only mpv build preserves the option to distribute Colosseum's original source under MIT.
2. Keep Qt dynamically linked and include the required Qt and third-party license notices, source offer or corresponding-source delivery, and replacement/relinking instructions for the exact libraries shipped.
3. Include the license texts and copyright notices for every bundled DLL, executable, JavaScript library, font, icon set, and dataset.
4. Colosseum may use a separately installed Official Stremio Service without changing its MIT license. If the installer bundles the GPL-2.0 service, include its GPL materials and corresponding source. Do not bundle the legacy `stremio-runtime.exe` + `server.js` pair until its exact provenance and redistribution terms are verified.
5. Generate a release-specific software bill of materials so the installer describes what it actually contains rather than what the development machine happens to contain.

Adding or removing a dependency can change these obligations. Review this inventory whenever the packaged runtime changes.