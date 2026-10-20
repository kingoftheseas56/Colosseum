# Verified browser results

Final frozen registry: 95 routes, 340 declared inspection states. Full route sweeps tested 95/95 route defaults in each browser; this is not a claim that every state and every backend was tested.

- Chromium Headless Shell 151.0.7922.34: 95 passed; 0 runtime/resource issues; 0 warnings; 0 external requests.
- Firefox 153.0: 95 passed; 0 runtime/resource issues; 168 source-font metadata warnings (STAT/nameID diagnostics); no failed font faces. These warnings do not represent failed routes.
- Environment: Debian 13.6 x64, Playwright 1.62.1. Windows and compiled QML/native engines were not tested.
- Review map: 95 route links + 340 declared state links parsed; representative default, Highlights state and Vinyl deep links passed over file:// in both browsers.
- Screenshots were visually inspected, including the final Chromium Home image. Representative reviewed contact sheets are included under verification/selected-screenshots. The full screenshot corpus is intentionally omitted.

Full final route facts, issues and warnings are retained in verification/final-chromium/results.json and verification/final-firefox/results.json, with local absolute paths and ephemeral host tokens removed. Vault sheet/chrome regression checks: 17/17 passed in each browser after the centered sheet and scoped window chrome repairs. Feria apps starts at the source 112px inset. Corrected Vault and Feria full-resolution screenshots were inspected in both browsers.

Cross-browser presentation limitation: Firefox shows sharper backgrounds through some frosted surfaces (including Home Continue cards and the Extensions tray), while Chromium renders the frosted blur. No cause or runtime failure is claimed.

Screenshot paths inside those reports identify the original verification captures; only the selected sheets are included in this compact archive.

Runtime files have been SHA256-compared byte-for-byte against the frozen staged source. See verification/runtime-sha256.json. ZIP integrity was checked before Library upload.
