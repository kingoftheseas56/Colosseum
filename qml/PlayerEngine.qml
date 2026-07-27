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
    // INVARIANT THIS RELAY DEPENDS ON - read before writing another branch:
    //   the engine must either ACCEPT a pushed value or EMIT a change signal.
    // The engine's change signal is the relay's ONLY re-sync. An engine that silently CLAMPS, ROUNDS
    // or REMAPS a pushed value onto the value it already holds emits nothing, so the facade keeps
    // the number it was given and the two diverge permanently (measured: facade=4, inner=3, no
    // recovery). All three shapes are live on MpvItem - it clamps (setPanscan qBound(0,1)), rounds
    // (setSpeed and the delays to 2dp, mpvitem.cpp:583-591 / :633-645) and remaps (setVideoAspect
    // "" -> "-1", :672-675) - so do not check the range alone.
    // On THIS branch it is unreachable: every PlayerPage writer already goes through root.round2 and
    // stays inside mpv's accepted range (speed 0.25..3 vs qBound(0.25,3), volume 0..100 vs
    // qBound(0,600)), and videoAspect is write-only with the fill modes passing the literal "-1".
    // On the OTHER branch it is REAL and Task 3 measured it: Player2Session clamps speed to 0.5..2.0
    // against PlayerPage's 0.25..3 and returns silently when the clamp lands on the value it already
    // holds, so PlayerEngineP2 re-reads the session after every speed/volume push. Any branch whose
    // accepted range, rounding or remapping is tighter than what PlayerPage sends must do the same.
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

    // The relayed members, in one place. _adopt walks this list, and the contract's P2 section
    // checks the same names against the other branch.
    readonly property var relayedMembers: ["pause", "speed", "volume", "mute", "audioTrack",
                                           "subtitleTrack", "subDelay", "audioDelay", "videoZoom",
                                           "videoAspect", "panscan"]

    // ONE implementation of each direction, keyed by name. The explicit form this replaces named
    // each member four times across 22 near-identical lines, so a transposition
    // (`inner.subDelay !== engine.audioDelay`) was valid QML that compiled and half-worked, and
    // nothing checked it - the contract verifies declaration, never wiring. Bracket access is not
    // an assumption: it was probed against the real C++ MpvItem for all 11 relayed members plus the
    // 9 readonly forwards, read and write, before the collapse.
    function _push(k) { if (engine._linked && engine.inner && engine.inner[k] !== engine[k]) engine.inner[k] = engine[k] }
    function _pull(k) { if (engine.inner && engine[k] !== engine.inner[k]) engine[k] = engine.inner[k] }

    // Nothing may be pushed into the engine until we have first adopted its real values.
    // The null-`inner` test in _push already blocks the startup push on its own (Loader.item is null
    // until the Loader's componentComplete, which runs after these initialisers), so this is defence
    // in depth rather than the only guard - but it is cheap and it states the ordering the relay
    // needs: MpvItem::setAudioTrack("") means `aid=no`, and its delay/panscan/zoom/aspect setters
    // have no no-op early-return, so a push of the facade's own initial values would be real mpv
    // commands on a player that had asked for nothing.
    property bool _linked: false

    function _adopt() {
        if (engine._linked || !engine.inner)
            return
        for (var i = 0; i < engine.relayedMembers.length; i++) {
            var k = engine.relayedMembers[i]
            engine[k] = engine.inner[k]
        }
        engine._linked = true      // set LAST: the assignments above must not push back down
    }

    // Whichever of these lands first arms the relay. A Loader with a static source builds its item
    // during its own completion, so by the facade's Component.onCompleted `inner` is already set and
    // onInnerChanged may never fire - hence both. The `_linked = false` is what makes a REPLACED
    // inner get adopted instead of pushed into: the early return in _adopt() is a re-entry guard,
    // not an "already done forever" flag. Unreachable today (`source` keys off a boot constant), but
    // a reader must not be reassured by the very thing that would break under a Loader reload.
    onInnerChanged: { engine._linked = false; engine._adopt() }
    Component.onCompleted: engine._adopt()

    // --- push: facade -> engine ---
    onPauseChanged: engine._push("pause")
    onSpeedChanged: engine._push("speed")
    onVolumeChanged: engine._push("volume")
    onMuteChanged: engine._push("mute")
    onAudioTrackChanged: engine._push("audioTrack")
    onSubtitleTrackChanged: engine._push("subtitleTrack")
    onSubDelayChanged: engine._push("subDelay")
    onAudioDelayChanged: engine._push("audioDelay")
    onVideoZoomChanged: engine._push("videoZoom")
    onVideoAspectChanged: engine._push("videoAspect")
    onPanscanChanged: engine._push("panscan")

    signal fileStarted()
    signal fileLoaded()
    signal playbackError(string code, string message)
    signal endFile(string reason)
    signal gifSaved(string path)
    signal gifFailed()
    signal trackListChanged()

    // --- pull: engine -> facade ---
    // Deliberately NOT `ignoreUnknownSignals` - if a branch stops emitting one of these, Qt says so
    // out loud at runtime. A silently dead relay is the expensive failure here.
    Connections {
        target: engine.inner

        function onPauseChanged() { engine._pull("pause") }
        function onSpeedChanged() { engine._pull("speed") }
        function onVolumeChanged() { engine._pull("volume") }
        function onMuteChanged() { engine._pull("mute") }
        function onAudioTrackChanged() { engine._pull("audioTrack") }
        function onSubtitleTrackChanged() { engine._pull("subtitleTrack") }
        function onSubDelayChanged() { engine._pull("subDelay") }
        function onAudioDelayChanged() { engine._pull("audioDelay") }
        // panscan, videoZoom and videoAspect all report through ONE videoFillChanged - on MpvItem
        // because that is its NOTIFY for all three, and on PlayerEngineP2 because it declares the
        // same signal to match. There is no panscanChanged/videoZoomChanged/videoAspectChanged on
        // either branch to connect to.
        function onVideoFillChanged() {
            engine._pull("panscan")
            engine._pull("videoZoom")
            engine._pull("videoAspect")
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
