# M3 remote playback

Base: f3674c0f3 on codex/vinyl-production-flow. User explicitly requested actual-app torrent playback of the intended song and separately demonstrated YouTube fallback. Custom search API remains Hemanth's work; the standalone Google CSE web button is not a playback dependency.

1. Extend the current shared StreamServer with bounded, cancellable request-scoped manifests and exact file selection. Keep legacy stream/fetch events separate and preserve adopted-service ownership.
2. Repair music routes and conservative real-world release-folder matching, preserving recording/edition/disc/track identity. Reject unsupported whole-album images and ambiguities; never play a whole album as a guessed song.
3. Connect local-first, torrent-preferred, YouTube-fallback resolution through VinylCatalogueSource and the existing VinylBackendSession/MpvItem. Preserve queue occurrence, position and pause on failure; fence cancellation/profile/shutdown and bound retries.
4. Run focused contracts and real Main.qml app journeys, including public discovery, actual torrent bytes, YouTube resolution/output, seek, failure, supersession and shutdown. Record exact evidence and remaining limitations. Fresh review, commit/push only this branch.

Ruling: scoped cancellation retires only its network operation and admitted manifest. The existing runtime owns shared swarm/disk cache; scoped consumers must not delete engines that Theatre, downloads or an adopted external service may use.

Progress: existing sources were probed through actual Qt networking and returned Nas releases. The blocked donor contracts fail to compile against the old StreamServer API. Scoped metadata/selection contracts now pass against an isolated ephemeral loopback fixture, without legacy playback signal leaks. Matcher/routing worker observed RED then GREEN and retained unsupported single-image CUE rejection.

Receiving review and regression results:

- Search summaries retain full queued release context. Explicit browse identity selects the original release even when another edition contains the same recording.
- Torrent decode, startup, stall and seek failures enter one bounded YouTube recovery, preserving occurrence, target position and pause intent. A failed fallback does not loop.
- Supplied audio tags and duration are checked against the canonical queue before admitting torrent playback. Contradictory title, artist or album tags trigger fallback.
- Native tests caught Qt 6 generic invocation argument mixing and accidental remote-context forwarding to the local source's pagination parameter. Both original local playback checks and the added remote native decoder checks pass after correction (21 cases including setup/cleanup, no failures).
- Profile/queue retirement clears transient source provenance. The real-app driver verifies provider and occurrence again after seek/pause/resume, so recovery cannot disguise a failed torrent journey.

Actual Main.qml receiving passed with qualification/fixture flags OFF:

- Nas, Illmatic, N.Y. State of Mind: public Tankorent discovery selected torrent file 1, matching title/album/artist tags, MP3 stereo decoding and WASAPI output. Seek/pause/resume and natural EOF advancement to track 3 passed without switching to YouTube.
- Eminem, STANS, Stan (feat. Dido): no exact torrent file was found. The same canonical occurrence used the selected YouTube video, with Opus stereo decoding and WASAPI output. Seek/pause/resume and EOF advancement to track 2 passed.
- Both test phases cleared the profile, exited 0 and stopped their owned streaming runtimes. A third ordinary app phase remains open and responsive on Illmatic, paused after the same torrent seek/resume check.
- All six focused CTest targets passed, including 21 native session and 44 world cases. See the M3 receiving report for exact binary, QML and evidence identity.

This closes the requested individual-file torrent and real YouTube playback validation. Whole-album CUE span playback remains unsupported by this promoted adapter and must still be completed against the authored backend; these candidates safely fall back. Main-branch landing and release qualification are separate.
