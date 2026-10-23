# Vinyl personalized Home presentation — received 2026-10-23

## Scope and ownership

Task 6 of the personalized Home plan, on `codex/vinyl-production-flow`, based on
`4654c53e16af84174392fbb900b8410fc1fe532f`. Existing QML, Collection, Preferences,
Activity, History, provider cache, routes and player remain the owners. Luna xhigh
implemented the bounded Home/shelf presentation and QML fixture coverage; root
implemented retained-history selection, data binding and receiving fixes, and
ran production Main journeys. This record does not close Task 7's wider matrix.

## Accepted behavior

- Home orders Continue listening, native personalized catalogue rows, selected
  artists and factual saved collections/playlists, then Rediscover and Less played.
- Rediscover and Less played require positive qualified retained play counts and
  a retained last-play time. Missing history is unknown. Equal positive counts
  do not produce a Less played row. No lifetime-unplayed claim is made.
- Less played selects counts below the eligible saved-library maximum, ordered
  by count then oldest retained play and stable identity. Those entries are
  reserved for that row; Rediscover orders the remaining entries by oldest
  retained play. Each row is capped at 12. This first-pass policy is versioned in
  the existing configuration hash, not presented as a calibrated recommender.
- Saved albums and resolved saved songs use their existing destinations. A
  retained song identity does not grant a new remote playback-match attestation.
  Explicit item/known-artist suppression is honored.
- Skip with no tastes or saved/local content uses the existing public YouTube
  Music discovery transport/cache. These rows are explicitly public Discovery,
  not personalized recommendations. Browsed metadata alone does not disable them.
- Cached cards remain usable during refresh/failure. Available factual rows stay
  visible. Row-local loading/cached/error text and icon retry avoid duplicate
  global status. An entirely empty Home has one concise loading/unavailable/empty
  state and Discover/Library destinations; unavailable state has one retry icon.
  Raw provider/process diagnostics stay out of visible status copy. Empty shelves
  do not expose See all. Existing top search remains the search entry.
- Personal and public See-all routes retain stable copies across browsing and
  playback/library changes. History disable/clear invalidates copied personal
  recommendation evidence immediately; profile clear removes all copied state.
  Public catalogue copies do not depend on listening privacy.

## Receiving fixes

One fresh read-only review found two important issues: copied personal See-all
survived a privacy reset, and public See-all vanished when playback/library
changes removed public Home eligibility. Both were reproduced by failing QML
regressions, fixed, and covered in production Main. Native privacy generations
now fence copied personal snapshots. Equal positive count coverage was added.

Actual no-cache rendering also exposed repetitive empty rows and implementation
diagnostics. A final bounded Luna pass replaced them with the central state and
safe shelf copy, covered by failing-then-passing QML tests. Empty See-all received
its own failing-then-passing regression. Root inspected the final diff/captures.

## Verification

- Production `colosseum`, native session bridge and recommendation service test
  built successfully. Final runtime-ready QML manifest:
  `d0f490e262d356318f63ac1e03420c5f58d3f5884f1ee318f87c7ccc57ae7cc2`.
- Final focused CTest selection: **20/20 passed, 37.06 seconds**. This covers
  recommendation service/history/contracts, existing BE contracts/derivation/
  ranker, feedback/music-action persistence, native Home/personalized Home,
  onboarding, playback/queue/actions/seek, library, discovery and catalogue pages.
  Service QtTest has 30 passing cases, Home 18 and personalized Home 26, including
  their init/cleanup cases. No full repository suite was run.
- `git diff --check` passed.

Actual production Main used the native stores/provider/player and the new
`tests/vinyl-live/PersonalizedHomePresentationFlow.qml` semantic driver:

| Journey | Result |
| --- | --- |
| Tagged local WAV import; qualified A once/B twice; save albums/song | PASS; actual player listening crossed the existing qualified gate |
| Rediscover/Less played content, order, See-all → Album/Song → Back | PASS; playback position and queue occurrence IDs preserved |
| Restart the same local profile | PASS; retained rows and saved-song route restored |
| History off/on and existing history clear while personal See-all is open | PASS; copied evidence and derived rows removed |
| Fresh Skip → real public YouTube Music shelf → real album/tracklist → Back | PASS; no synthetic catalogue response |
| Public See-all plus background library import/local playback → Back | PASS; retained public IDs unchanged |
| Provider unavailable without cache | PASS on final QML; no fabricated cards, one central state, actual Discover/Library actions work |
| Provider unavailable with prior real personalized cache | PASS on final QML; cached cards retained and rendered in the verified viewport |

Local audio was generated tagged PCM in disposable profiles; it is not evidence
of new remote playback or perceived audio quality. Provider-unavailable runs set
only that child process's Python helper to a nonexistent executable: this proves
transport-unavailable handling, not an operating-system network-offline test.
The first public-return driver incorrectly invoked the user-facing import
command, which intentionally navigates to Library. The corrected driver uses
the existing background session import seam for the retained-route test; no
production navigation was changed to accommodate the driver.

Desktop 1920×1080 and narrow 880×760 captures were inspected. Additional final
captures show the no-cache state and actual cached covers/status above the player.
QML tests cover pointer-focus neutrality, card geometry, viewport/player-bar
clearance and navigation state. Actual-app actions were semantic handlers and
rendered captures; physical pointer/keyboard and human acceptance are not claimed.
The public-return capture settled on the existing hero; the retained-list claim
comes from exact model IDs/routes, not that capture.

Evidence is local under `D:/Colosseum-Vinyl-Home-Finish-20261023` (phase logs,
RED/GREEN receipts, final CTest/build logs and captures). Build output, screenshots,
profiles, audio and machine-specific runners are not committed. A final responsive
inspection app uses the existing Ilaiyaraaja/SPB profile and production provider.

## Remaining qualification

Task 7 remains open for the consolidated cross-taste/same-name/provider-only
matrix, cold/warm timing measurements and broader receiving closure. Public
source completeness and subjective ranking quality remain separate from this
presentation acceptance. Existing Tankorent success/remote seek gates are
unchanged. This work lands on the feature branch; no main merge or release is
claimed.
