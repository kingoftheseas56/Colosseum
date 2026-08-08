# Colosseum promo clips — shot list (v1, 2026-08-08)

Each clip: 10–20s, 1920×1080, cursor hidden, app's own glide animations doing the work.
GitHub rules the formats: README-embedded mp4s must be ≤10MB (drag-drop upload), the one
autoplaying asset is the hero GIF committed in-repo.

| # | Clip | Route | Beats |
|---|---|---|---|
| 1 | hero | Home | top → slow scroll: Continue row → Tankoban bookshelf → Theatre strip → Biblio desk. Source for the README hero GIF. |
| 2 | theatre-discover | Theatre | marquee + Continue → scroll Discover rows (Top 10, IMDb shelves) |
| 3 | theatre-detail | Theatre | open a series detail → episode list scroll → back |
| 4 | theatre-catalogue | Theatre → Movies tab | deep IMDb catalogue shelves scroll |
| 5 | tankoban-discover | Tankoban | Discover wall scroll |
| 6 | tankoban-series | Tankoban | manga series page, Tankoban Mode volumes |
| 7 | comic-reader | reader | open downloaded chapter, page through, chrome reveal |
| 8 | biblio | Biblio | Top charts, genre browse scroll |
| 9 | reader2 | Biblio book | open book, page turns, typography panel |
| 10 | extensions | Extensions | Sources world chains → Browse |
| 11 | downloads | Downloads | unified vault scroll |
| 12 | wallpapers | Wallpapers | picker browse (NO switching — writes wallpapers.ini) |
| 13 | search | any world | query → results |
| 14 | player | Theatre | DEFERRED: playback writes real Progress (registry-shared). Shoot with Hemanth present. |

Tonight's drafts: 1, 2, 5.

Capture notes:
- Instance: `COLOSSEUM_APPDATA_TAG=cwprobe` (file stores = full copy of the real library),
  registry stores (Progress/Collection/settings) are SHARED with the real app — no resume
  clicks, no wallpaper switching, no settings edits during capture.
- Fresh launch takes foreground by itself; do not run captures with other windows raised.
- Driver: `shoot.py` (Lanista pipe choreography + ffmpeg ddagrab).

Field rules learned 2026-08-08 (first shoot):
- NEVER script-click 24x24 targets on Continue tiles — that is the hover-invisible remove
  button (opacity 0 but always active); it deletes a real Progress entry (registry-shared).
- World pills, marquee Read/Details, and browse toggles are TapHandlers — invisible to
  ui-snapshot, unclickable by handle. Boot per-world with COLOSSEUM_OPEN_WORLD instead.
- The featured marquee auto-rotates; timing a click on it is a lottery. Grab stills from
  stable surfaces.
- OS-level clicks require the app freshly launched into the foreground and the machine idle.
