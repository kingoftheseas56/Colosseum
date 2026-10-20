# Colosseum website source for Argon

This folder is a **source-reading sidecar**, copied into `kingoftheseas56/Colosseum` branch `argon-source` for Argon's GitHub importer. It is **not** the production website repository, an app WebUI conversion or a ready-to-deploy website package.

**Canonical website:** `kingoftheseas56/Brotherhood` → `agents/colosseum-site/` on `codex/tributes-site`, pinned source commit `4f33a13df11bbd4d54ad295dc5669c9282fb6908` (2026-10-20).

**Canonical app:** `kingoftheseas56/Colosseum`. The other folders of this Argon branch are an older, trimmed app source-reading snapshot. Do not assume this branch is current master, buildable or safe to merge.

## What is present
- All 422 source/text/font/vector files from the website, including HTML, CSS, JS, current UI mock and existing Tribute code.
- 368 image/media files at or below 100,000 bytes.
- Original relative directory layout and unmodified copied file bytes.
- `ARGON_SITE_MANIFEST.json` listing each of the 162 larger **omitted media** files, its pinned source path and corresponding live website URL.

The omissions are deliberate to keep the complete Argon repository below the Gemini 100 MB / 5,000-file import bounds. Do not interpret missing local artwork as a request to invent or replace it.

## Preview on a machine with Node.js
Run `node web/colosseum-site/argon-preview-server.mjs` from the repository root, then open `http://127.0.0.1:4173/`. The server serves local source files and fetches manifest-listed larger artwork from the current public `getcolosseum.world` URLs. The source commit is pinned, but live artwork may change; the response indicates whether its byte size matches the pinned source. Artifacts not deployed to the public website may remain unavailable. Internet access is needed for missing images; no login or API key is required.
If Node.js is unavailable, the ordinary local HTML files are still readable, but omitted artwork will not render correctly without the proxy. The preview server is development-only and binds to localhost. It does not publish files or change the Cloudflare site.

## Task and safety boundaries
Read `ARGON_WEBSITE_TASK.md` for the current website UI redesign brief. Keep all approved 1.1.9 compositions intact and distinguish released features from Coming Up. Preserve the first-party download redirect paths, the existing Tribute links and the current demonstration content. Do not move app QML, alter installers, invent media rights, or deploy production changes.

Argon should produce a reviewable **website-only patch or isolated candidate**. Apply accepted changes back to the canonical Brotherhood website source and verify there, never merge this derived `argon-source` branch into app master.
