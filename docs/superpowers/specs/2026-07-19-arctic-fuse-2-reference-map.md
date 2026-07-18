# Arctic Fuse 2 — the media-surface design anchor (reference map)

**Date:** 2026-07-19 · **Ratified:** Hemanth supplied the skin himself ("well... there we go") after the KDE-Plasma reference post-mortem · **Reference lives at:** `C:\Users\Suprabha\Desktop\Kodi Reference\skin.arctic.fuse.2-omega` (parked out of Downloads) · **License:** the skin is a REFERENCE to read, not assets to lift — we adopt compositions and grammar, never files.

## Why this anchor (one sentence)
Arctic Fuse 2 (jurialmunkey, successor to Arctic Horizon) is a mature couch **media library** design — the same problem Colosseum is — and its grammar is already ours: near-white ink at fixed alphas over black glass on full-bleed art.

## Proof of kinship (read from the skin, 2026-07-19)
- `colors/defaults.xml`: ink = `#ededed` at an opacity ladder (100/90/70/50/30/12) over pure black — byte-for-byte our "ink ramp is white at alpha" reader doctrine.
- `fonts/`: ships **Inter** (a house face), plus Figtree/Heebo (geometric humanist, Switzer-adjacent).
- `Includes_Constants.xml`: 167 layout tokens on a 1080 grid — `view_pad=80` (page margin), `view_top=180` (header band), `view_row=510` (widget row), `view_osd_l/r=240` (player OSD gutters).
- `Includes_Constants_MouseTouch.xml`: explicit pointer/touch adaptation — the "Kodi is remote-first" caveat is solved inside the skin itself.
- Structure mirrors ours: per-medium HUB windows (`Custom_1101_Hub_Movies` / `1102_TVShows` / `1103_Music` ≈ our worlds), `OSD_NextOverlay` (≈ our F11 Up Next card), `OSD_Cast`, `OSD_InfoPanel`, `Dialog_PIP`, `Dialog_Plot`, aspect-ratio constant sheets per screen shape.

## The law that rides along
We adopt **composition, spacing, type scale, motion timing** — never the data pipeline. AF2 leans on TMDbHelper/fanart.tv for clearlogos and per-title art; both are banned here (no-login law). Our art comes from our own lanes (Cinemeta/AniList/MAL CDN/Wallhaven/native wallpapers). Any mapping below that assumed their art assets adapts to ours.

## Surface-by-surface map (the audit backlog)
Each row = one eyes-on pass: open AF2's screen (XML + screenshots), open ours, write the delta, land the pull-back. Owners per lane; multi-domain = A0 on Hemanth's order.

| AF2 surface (file) | Ours | What to steal |
|---|---|---|
| Home + hub windows (`Home.xml`, `Custom_110x_Hub_*`) | Home + world pages | Spotlight-over-widget-rows rhythm; `view_row` cadence; how the menu recedes |
| `DialogVideoInfo.xml` + `Dialog_Plot` | TheatreSeries / movie detail | Metadata block anatomy, action-row placement, plot-as-overlay instead of inline wall |
| Library views (`Includes_Constants_FlixArt_*`) | Genre pages / catalog walls | Card proportions per art shape; the "FlixArt" landscape-card scale ladder |
| `VideoOSD.xml` + `OSD_InfoPanel` (`view_osd_l/r=240`) | PlayerPage HUD | OSD gutter discipline, info-panel-over-video pattern |
| `OSD_NextOverlay` | F11 Up Next card (spec 2026-07-18) | Their card anatomy/timing — direct reference for A4's build |
| `OSD_Cast` | (unbuilt) cast row | Their cast overlay IS the missing-cast-row reference if we ever add one |
| `Custom_1185_Search` | SearchSurface | Query + results composition on glass |
| `Dialog_PIP` | WindowMode PiP | Corner-pip proportions |

## Standing rule
When a NEW media surface needs a reference, look at Arctic Fuse 2 FIRST; only reach past it (Jellyfin/PS5/etc.) when AF2 has no answer — that's the anti-quilt rule from Hemanth's 2026-07-19 coherence concern.
