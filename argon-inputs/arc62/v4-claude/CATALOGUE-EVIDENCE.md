# Catalogue evidence — Artist V4 prototype

Machine-readable detail: `catalogue-evidence.json` (every call, release group, unmatched title and asset hash).
Fetched 2026-10-14 16:20–16:22 UTC. Scripts: `tools/yt_fetch.py`, `tools/mb_fetch.py`, `tools/build_data.py`.

## Sources

- **YouTube Music** (primary): ytmusicapi 1.12.3, unauthenticated, location IN. Calls: `get_artist`, `get_artist_albums` (albums and singles, `limit=None`) when the artist page returns a "more" handle, `get_playlist` on the Songs and Videos browse handles. All calls succeeded.
- **MusicBrainz** (identity): release-group browse for each artist MBID. Eminem's MBID `b95ce3ff-3d05-4e87-9e01-c97b66af13d4` is canonical. Lifafa (`217f190a-…`) and Easy Wanderlings (`db7571d3-…`) came from a name search with score 100 and should be confirmed before production use. No existing MBID mapping was found in the Vinyl worktree or Preflight arc files.
- **Not fetched:** per-album tracklists (`get_album`). The artist page does not show tracklists; the native album page owns them.

## Eminem

| | Provider entries | Shown after grouping |
|---|---|---|
| Albums shelf (`get_artist_albums`, complete continuation) | 21 | 11 studio albums, 2 compilations, 1 soundtrack, 1 other |
| Singles shelf (`get_artist_albums`, complete continuation) | 26 | 25 singles |
| Songs playlist | 150 tracks | top 10 (5 + Show more) |
| Videos playlist | 109 | 105 videos + 4 live performances |
| Related artists | 10 | 10 |

Edition grouping uses MusicBrainz release groups: The Marshall Mathers LP (3 YouTube entries), MMLP2, Music To Be Murdered By (with Side B), The Death of Slim Shady (with Expanded Mourner's Edition), STANS (2 entries) and Mockingbird (2 single entries) each show once with an edition count. The year shown is the release group's first release year (The Eminem Show reads 2002 though YouTube only carries the 2022 Expanded Edition).

**Coverage gaps (not invented, not shown):** Infinite (1996), Relapse: Refill, the 8 Mile and Southpaw soundtracks and Shady XV are not returned as separate releases in YouTube Music's IN catalogue response. Eminem Presents The Re-Up has no Eminem-credited MusicBrainz release group, so it sits in "Other releases" rather than being guessed into Compilations. Unmatched singles (Realest, 911, Killer (Remix), Chloraseptic (Remix)) stay in Singles from the provider shelf.

**Provider quirks kept as-is:** Lose Yourself is filed under the album "Just Lose It"; Till I Collapse under "The Eminem Show (Expanded Edition)". Ranks 1–5 are the artist page's Songs shelf (it matches the head of the Songs playlist exactly); ranks 6–10 are the playlist's continuation. Rank is provider order, not a Colosseum popularity score. Live performances are videos whose provider title contains the word "Live".

## Lifafa (sparse)

2 albums, 1 single (inline shelves, no continuation handle, so complete as returned), 18 songs, 4 videos, 10 related artists. Both albums match MusicBrainz via their Latin title (MusicBrainz stores "Jaago जागो"); the single Din Raat has no MusicBrainz group. The videos come from the artist page's inline shelf, which carries no durations, so those frames show none.

## Easy Wanderlings (wallpaper fallback)

1 album, 5 singles/EPs (2 EPs), 21 songs, 26 videos (3 live), 10 related artists. The largest artist image YouTube returns is 1080x450, below the prototype's hero rule (at least 1440 px wide and 1.6:1), so the page goes straight to the current wallpaper. The related-artist list is what YouTube returned and looks loosely matched.

## Images

251 files, each with source URL, byte size and SHA-256 in `catalogue-evidence.json`. All are provider reference art (YouTube Music thumbnails, `i.ytimg.com` video stills) and are **not rights-cleared**. Per `ARTIST-ARTWORK-SOURCE-STRATEGY`, a production hero needs a rights-qualified source (Fanart.tv, TheAudioDB, eligible Commons). The 1440 px rule is a prototype stand-in for that eligibility decision.

Wallpaper: `assets/wallpaper/current-home-pick.jpg` is the `homePick` in `Brotherhood/Colosseum/wallpapers.ini` (Wallhaven odw8e7). That is the dev checkout's setting; an installed Colosseum may hold a different pick. `cold-ripple.jpg` is kept as the default when no pick exists.
