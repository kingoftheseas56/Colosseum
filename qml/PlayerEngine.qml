// PlayerEngine - the ONE object PlayerPage talks to (it is `id: mpv` at its instantiation site,
// PlayerPage.qml:2821). It hosts exactly one inner engine, chosen by the BOOT fact, and forwards
// a single fixed surface to it.
//
//   mpv boot      -> PlayerEngineMpv.qml (the real MpvItem), forwarded 1:1.
//   Player 2 boot -> PlayerEngineP2.qml, which adapts the D3D11 engine to this same surface.
//
// The mpv branch must be BEHAVIOURALLY INVISIBLE: PlayerPage on the mpv boot is the daily driver.
// Every choice in this file is made in favour of "identical" over "clever".
//
// The surface is pinned by tests/player2/player2_engine_facade_contract.ps1 - grow it there first.
import QtQuick

Item {
    id: engine

    // Which engine this PROCESS booted on. main.cpp always sets Player2Available (bootPlayer2 or
    // false), but the typeof guard keeps PlayerPage loadable in a bare QML harness with no context
    // property installed.
    readonly property bool p2: (typeof Player2Available !== "undefined") && Player2Available === true
    readonly property var inner: engineLoader.item

    Loader {
        id: engineLoader
        anchors.fill: parent
        source: engine.p2 ? "PlayerEngineP2.qml" : "PlayerEngineMpv.qml"
    }

    // ---- capability flags (Task 6 gates PlayerPage's capture/live rows on these) ----
    readonly property bool supportsCapture: !engine.p2   // screenshot / GIF / frame grab
    readonly property bool supportsLive: !engine.p2      // live guide / DVR panels

    // ---- state PlayerPage only READS - plain forwarding bindings ----
    // These stay `readonly` on purpose: a writable property's initialiser would be a binding that
    // the first assignment silently destroys, and the engine->UI direction would go stale.
    readonly property real duration: inner ? inner.duration : 0
    readonly property real position: inner ? inner.position : 0
    readonly property real cacheTime: inner ? inner.cacheTime : 0
    readonly property bool coreSeeking: inner ? inner.coreSeeking : false
    readonly property string mediaTitle: inner ? inner.mediaTitle : ""
    // `var`, not `string`: PlayerPage calls mpv.currentUrl.toString() and hands the raw value to
    // seekThumbs.request() (PlayerPage.qml:2808-2809), so the QUrl must survive the hop.
    readonly property var currentUrl: inner ? inner.currentUrl : ""
    readonly property var chapters: inner ? inner.chapters : []
    readonly property var audioTracks: inner ? inner.audioTracks : []
    readonly property var subtitleTracks: inner ? inner.subtitleTracks : []

    // ---- state PlayerPage ASSIGNS - two-way relays ----
    // Eleven members are written by PlayerPage (`grep -nE "mpv\.[a-zA-Z]+ =" qml/PlayerPage.qml`),
    // so they cannot be readonly forwards. Each one keeps a local value, pushes it down on change
    // and adopts the engine's value back when the engine reports one. The `!==` test on BOTH sides
    // is what stops the two directions fighting: a pull assigns the engine's exact value, so the
    // push that follows sees no difference and stops.
    //
    // INVARIANT THIS RELAY DEPENDS ON - read before writing PlayerEngineP2.qml:
    //   the engine must either ACCEPT a pushed value or EMIT a change signal.
    // The engine's change signal is the relay's ONLY re-sync. An engine that silently clamps a
    // pushed value onto the value it already holds emits nothing, so the facade keeps the number it
    // was given and the two diverge permanently (measured: facade=4, inner=3, no recovery). That is
    // currently UNREACHABLE through PlayerPage - every writer clamps inside mpv's accepted range
    // (speed 0.25..3 vs qBound(0.25,3), volume 0..100 vs qBound(0,600), the fill modes' panscan and
    // zoom inside qBound(0,1)/qBound(-2,2)) - so there is no live bug and nothing to fix here.
    // It is a constraint on the OTHER branch: Player 2 clamps on its own ranges, and if any of them
    // is tighter than what PlayerPage sends, that member needs an explicit re-read after the push.
    //
    // TYPES ARE THE ENGINE'S, NOT THE PLAN'S: audioTrack/subtitleTrack are STRINGS on MpvItem
    // ("" means off, and MpvItem maps "" -> aid/sid "no"), and volume is an INT.
    property bool pause: true
    property real speed: 1
    property int volume: 100
    property bool mute: false
    property string audioTrack: ""
    property string subtitleTrack: ""
    property real subDelay: 0
    property real audioDelay: 0
    property real videoZoom: 0
    property string videoAspect: ""
    property real panscan: 0

    // Nothing may be pushed into the engine until we have first adopted its real values.
    // Without this, the facade's own initial values would be written into a freshly built MpvItem
    // at startup - and MpvItem::setAudioTrack("") means `aid=no`, i.e. the app would boot silent.
    // MpvItem's delay/panscan/zoom/aspect setters have no no-op guard either, so those pushes
    // would be real mpv commands on a player that had asked for nothing.
    property bool __linked: false

    function __adopt() {
        if (engine.__linked || !engine.inner)
            return
        var e = engine.inner
        engine.pause = e.pause
        engine.speed = e.speed
        engine.volume = e.volume
        engine.mute = e.mute
        engine.audioTrack = e.audioTrack
        engine.subtitleTrack = e.subtitleTrack
        engine.subDelay = e.subDelay
        engine.audioDelay = e.audioDelay
        engine.videoZoom = e.videoZoom
        engine.videoAspect = e.videoAspect
        engine.panscan = e.panscan
        engine.__linked = true      // set LAST: the assignments above must not push back down
    }

    // Whichever of these lands first arms the relay. A Loader with a static source builds its item
    // during its own completion, so by the facade's Component.onCompleted `inner` is already set
    // and onInnerChanged may never fire - hence both, and __adopt() is idempotent.
    onInnerChanged: engine.__adopt()
    Component.onCompleted: engine.__adopt()

    // --- push: facade -> engine ---
    onPauseChanged: if (engine.__linked && engine.inner && engine.inner.pause !== engine.pause) engine.inner.pause = engine.pause
    onSpeedChanged: if (engine.__linked && engine.inner && engine.inner.speed !== engine.speed) engine.inner.speed = engine.speed
    onVolumeChanged: if (engine.__linked && engine.inner && engine.inner.volume !== engine.volume) engine.inner.volume = engine.volume
    onMuteChanged: if (engine.__linked && engine.inner && engine.inner.mute !== engine.mute) engine.inner.mute = engine.mute
    onAudioTrackChanged: if (engine.__linked && engine.inner && engine.inner.audioTrack !== engine.audioTrack) engine.inner.audioTrack = engine.audioTrack
    onSubtitleTrackChanged: if (engine.__linked && engine.inner && engine.inner.subtitleTrack !== engine.subtitleTrack) engine.inner.subtitleTrack = engine.subtitleTrack
    onSubDelayChanged: if (engine.__linked && engine.inner && engine.inner.subDelay !== engine.subDelay) engine.inner.subDelay = engine.subDelay
    onAudioDelayChanged: if (engine.__linked && engine.inner && engine.inner.audioDelay !== engine.audioDelay) engine.inner.audioDelay = engine.audioDelay
    onVideoZoomChanged: if (engine.__linked && engine.inner && engine.inner.videoZoom !== engine.videoZoom) engine.inner.videoZoom = engine.videoZoom
    onVideoAspectChanged: if (engine.__linked && engine.inner && engine.inner.videoAspect !== engine.videoAspect) engine.inner.videoAspect = engine.videoAspect
    onPanscanChanged: if (engine.__linked && engine.inner && engine.inner.panscan !== engine.panscan) engine.inner.panscan = engine.panscan

    // --- pull: engine -> facade ---
    // Deliberately NOT `ignoreUnknownSignals` - if a branch stops emitting one of these, Qt says so
    // out loud at runtime. A silently dead relay is the expensive failure here.
    Connections {
        target: engine.inner

        function onPauseChanged() { if (engine.pause !== engine.inner.pause) engine.pause = engine.inner.pause }
        function onSpeedChanged() { if (engine.speed !== engine.inner.speed) engine.speed = engine.inner.speed }
        function onVolumeChanged() { if (engine.volume !== engine.inner.volume) engine.volume = engine.inner.volume }
        function onMuteChanged() { if (engine.mute !== engine.inner.mute) engine.mute = engine.inner.mute }
        function onAudioTrackChanged() { if (engine.audioTrack !== engine.inner.audioTrack) engine.audioTrack = engine.inner.audioTrack }
        function onSubtitleTrackChanged() { if (engine.subtitleTrack !== engine.inner.subtitleTrack) engine.subtitleTrack = engine.inner.subtitleTrack }
        function onSubDelayChanged() { if (engine.subDelay !== engine.inner.subDelay) engine.subDelay = engine.inner.subDelay }
        function onAudioDelayChanged() { if (engine.audioDelay !== engine.inner.audioDelay) engine.audioDelay = engine.inner.audioDelay }
        // panscan, videoZoom and videoAspect all report through MpvItem's single videoFillChanged -
        // there is no panscanChanged/videoZoomChanged/videoAspectChanged to connect to.
        function onVideoFillChanged() {
            if (engine.panscan !== engine.inner.panscan) engine.panscan = engine.inner.panscan
            if (engine.videoZoom !== engine.inner.videoZoom) engine.videoZoom = engine.inner.videoZoom
            if (engine.videoAspect !== engine.inner.videoAspect) engine.videoAspect = engine.inner.videoAspect
        }

        // --- signal relay: engine -> PlayerPage ---
        // Only the signals with no facade property of their own live here; the rest
        // (pauseChanged, positionChanged, durationChanged, ...) come free with the properties above.
        function onFileStarted() { engine.fileStarted() }
        function onFileLoaded() { engine.fileLoaded() }
        function onPlaybackError(code, message) { engine.playbackError(code, message) }
        function onEndFile(reason) { engine.endFile(reason) }
        function onGifSaved(path) { engine.gifSaved(path) }
        function onGifFailed() { engine.gifFailed() }
        function onTrackListChanged() { engine.trackListChanged() }
    }

    signal fileStarted()
    signal fileLoaded()
    signal playbackError(string code, string message)
    signal endFile(string reason)
    signal gifSaved(string path)
    signal gifFailed()
    signal trackListChanged()

    // ---- commands - one forwarder per member PlayerPage (or SubStyleBar) calls ----
    // Arities and RETURN VALUES match the engine exactly. captureFrame returns the saved path and
    // startGifRecording returns a bool that PlayerPage branches on (`if (!mpv.startGifRecording())`),
    // so a void forwarder would report every successful recording as a failure.
    // The `=== undefined` normalisation preserves the C++ default arguments a JS hop would destroy.
    function loadFile(url) { if (inner) inner.loadFile(url) }
    function seekExact(sec) { if (inner) inner.seekExact(sec) }
    function seekStep(delta) { if (inner) inner.seekStep(delta) }
    function frameStep() { if (inner) inner.frameStep() }
    function frameBackStep() { if (inner) inner.frameBackStep() }
    function setAudioNormalization(mode) { if (inner) inner.setAudioNormalization(mode) }
    function addSubtitle(url, title, lang, select) {
        if (!inner)
            return
        inner.addSubtitle(url,
                          title === undefined ? "" : title,
                          lang === undefined ? "" : lang,
                          select === undefined ? true : select)
    }
    // Reached INDIRECTLY: SubStyleBar takes the engine as `player:` (PlayerPage.qml:2935) and calls
    // player.setSubOption() for all six subtitle style controls, so "mpv.setSubOption" never appears
    // in PlayerPage and the contract's mpv.* scan cannot see it. Pinned by name in the contract.
    function setSubOption(key, value) { if (inner && inner.setSubOption) inner.setSubOption(key, value) }
    function command(args) { if (inner && inner.command) inner.command(args) }
    function mpvProperty(name) { return inner && inner.mpvProperty ? inner.mpvProperty(name) : "" }
    function captureFrame(title, subtitle) {
        if (!inner || !inner.captureFrame)
            return ""
        return inner.captureFrame(title === undefined ? "" : title,
                                  subtitle === undefined ? "" : subtitle)
    }
    function startGifRecording() { return inner && inner.startGifRecording ? inner.startGifRecording() : false }
    function stopGifRecording(title, subtitle) {
        if (!inner || !inner.stopGifRecording)
            return
        inner.stopGifRecording(title === undefined ? "" : title,
                               subtitle === undefined ? "" : subtitle)
    }
    function abortGifRecording() { if (inner && inner.abortGifRecording) inner.abortGifRecording() }
    function revealCaptureFolder(path) { if (inner && inner.revealCaptureFolder) inner.revealCaptureFolder(path === undefined ? "" : path) }
}
