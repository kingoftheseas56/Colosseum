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
| Stremio service / streaming runtime | A separate external runtime and not covered by Colosseum's MIT License. Do not bundle or redistribute it unless its applicable terms explicitly permit that distribution. Colosseum may instead communicate with a separately installed official service or a replacement runtime whose license permits integration and redistribution. |
| External APIs, addons, indexers, scrapers, catalogs, and media | Independent services and content. They are not part of the MIT-licensed Colosseum source and are not relicensed by this repository. |

## Windows release gate

Before publishing an installer or portable package:

1. Confirm whether the included `libmpv` build is LGPL-enabled or GPL. A normal GPL mpv build can impose GPL distribution terms on the combined application; using an LGPL-only mpv build preserves the option to distribute Colosseum's original source under MIT.
2. Keep Qt dynamically linked and include the required Qt and third-party license notices, source offer or corresponding-source delivery, and replacement/relinking instructions for the exact libraries shipped.
3. Include the license texts and copyright notices for every bundled DLL, executable, JavaScript library, font, icon set, and dataset.
4. Do not package the Stremio streaming runtime until redistribution rights are confirmed in writing or in an applicable license.
5. Generate a release-specific software bill of materials so the installer describes what it actually contains rather than what the development machine happens to contain.

Adding or removing a dependency can change these obligations. Review this inventory whenever the packaged runtime changes.
