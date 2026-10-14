import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    width: 1060; height: 760
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 1060; height: 760
            sourceUrl: Fixture.serviceUrl
            profilePath: Fixture.evidenceRoot + "/public-drm-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
            audioMuted: true
            userScripts.collection: [{
                name: "Feria public DRM observation",
                injectionPoint: WebEngineScript.DocumentCreation,
                worldId: WebEngineScript.MainWorld,
                runsOnSubFrames: false,
                sourceCode: `(function() {
                    const report = {encryptedEvents: 0, systems: [], sessions: 0,
                        messages: 0, statuses: [], errors: []};
                    window.__feriaPublicDrm = report;
                    const videos = new WeakMap();
                    const owners = new WeakMap();
                    let videoSerial = 0, keysSerial = 0;
                    function videoInfo(video) {
                        if (!videos.has(video)) videos.set(video, {id: ++videoSerial, encryptedEvents: 0});
                        return videos.get(video);
                    }
                    document.addEventListener('encrypted', function(event) {
                        report.encryptedEvents++;
                        videoInfo(event.target).encryptedEvents++;
                    }, true);
                    window.__feriaPublicDrmSnapshot = function() {
                        return Array.from(document.querySelectorAll('video')).filter(v => v.mediaKeys).map(v => {
                            const info = videoInfo(v), owner = owners.get(v.mediaKeys);
                            const quality = v.getVideoPlaybackQuality();
                            return {id: info.id, encryptedEvents: info.encryptedEvents,
                                keysId: owner ? owner.id : 0, system: owner ? owner.system : 'unknown',
                                sessions: owner ? owner.sessions.map(s => ({messages: s.messages,
                                    statuses: Array.from(s.native.keyStatuses.values())})) : [],
                                time: v.currentTime, ready: v.readyState, paused: v.paused,
                                decodedFrames: v.webkitDecodedFrameCount,
                                totalFrames: quality.totalVideoFrames, droppedFrames: quality.droppedVideoFrames,
                                width: v.videoWidth, height: v.videoHeight, error: v.error ? v.error.code : null};
                        });
                    };
                    if (!window.MediaKeys || !window.MediaKeySystemAccess) {
                        report.errors.push('EME unavailable'); return;
                    }
                    const createKeys = MediaKeySystemAccess.prototype.createMediaKeys;
                    MediaKeySystemAccess.prototype.createMediaKeys = function() {
                        const system = this.keySystem;
                        return Reflect.apply(createKeys, this, arguments).then(function(keys) {
                            owners.set(keys, {id: ++keysSerial, system, sessions: []});
                            report.systems.push(system); return keys;
                        });
                    };
                    const createSession = MediaKeys.prototype.createSession;
                    MediaKeys.prototype.createSession = function() {
                        const session = Reflect.apply(createSession, this, arguments);
                        const owner = owners.get(this);
                        const system = owner ? owner.system : 'unknown';
                        const state = {native: session, messages: 0};
                        if (owner) owner.sessions.push(state);
                        report.sessions++;
                        session.addEventListener('message', function() { report.messages++; state.messages++; });
                        session.addEventListener('keystatuseschange', function() {
                            const statuses = [];
                            session.keyStatuses.forEach(function(status) { statuses.push(status); });
                            report.statuses.push({system, statuses});
                        });
                        return session;
                    };
                })()`
            }]
        }
    }
    TestCase {
        name: "FeriaPublicDrm"
        when: windowShown
        property var browser: null
        function js(source, allowRetiredDocument, boundEpoch) {
            var done = false, result, epoch = browser.javascriptEpoch
            if (boundEpoch !== undefined)
                compare(epoch, boundEpoch, "Bound DRM document must not be replaced")
            var admitted = browser.evaluateJavaScript(source, function(value) { result = value; done = true })
            if (!admitted && allowRetiredDocument) return null
            verify(admitted, "JavaScript action must be admitted")
            var deadline = Date.now() + 5000
            while (!done && browser.javascriptEpoch === epoch && Date.now() < deadline) wait(20)
            // Read-only polls may race the public page's replacement navigation.
            // Feria intentionally retires those callbacks; retry the new document.
            if (!done && allowRetiredDocument) return null
            verify(done && browser.javascriptEpoch === epoch,
                "JavaScript action must complete in its original document")
            return result
        }
        function snapshot(allowRetiredDocument, boundEpoch) {
            var epoch = browser.javascriptEpoch
            var result = js(`(function() {
                const media = window.__feriaPublicDrmSnapshot ? window.__feriaPublicDrmSnapshot() : [];
                return {origin: location.origin, title: document.title,
                    drm: window.__feriaPublicDrm || null, videos: document.querySelectorAll('video').length,
                    mediaCount: media.length, media: media.length === 1 ? media[0] : null};
            })()`, allowRetiredDocument, boundEpoch)
            if (result) result.documentEpoch = epoch
            return result
        }
        function drmPlaybackReady(s) {
            return s && s.origin === "https://bitmovin.com" && s.media && s.media.encryptedEvents > 0
                && s.media.system === "com.widevine.alpha" && s.media.sessions.some(function(row) {
                    return row.messages > 0 && row.statuses.indexOf("usable") >= 0
                }) && s.media.ready >= 3 && !s.media.paused && s.media.time > 0
        }
        function cleanup() {
            if (browser) {
                try {
                    console.log("PUBLIC_DRM_FINAL " + JSON.stringify(snapshot(true)))
                } finally {
                    // A failed diagnostic read must still enter orderly teardown.
                    browser.prepareForClose(false)
                    tryCompare(browser, "pendingJavaScript", 0, 10000)
                    tryCompare(browser, "destructionNotified", true, 10000)
                    browser.destroy(); browser = null
                }
            }
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function test_encryptedPlayback() {
            if (!Fixture.serviceUrl.length) skip("Opt in with FERIA_SERVICE_URL=https://bitmovin.com/demos/drm")
            compare(Fixture.serviceUrl, "https://bitmovin.com/demos/drm")
            verify(Fixture.evidenceRoot.length > 0)
            Fixture.switchProfile("public-drm")
            browser = browserFactory.createObject(parent)
            verify(browser !== null)
            tryCompare(browser, "documentReady", true, 30000)
            tryVerify(function() { return js("location.origin", true) === "https://bitmovin.com" }, 15000)
            tryVerify(function() { return js("!!document.querySelector('video')", true) === true }, 15000)
            js("document.querySelectorAll('video').forEach(v => {v.muted=true;v.play().catch(()=>{});});true")
            tryVerify(function() {
                var s = snapshot(true)
                return drmPlaybackReady(s)
            }, 25000, "Require current usable Widevine keys belonging to the encrypted video")
            // Freeze the native document epoch once readiness has been established.
            // IDs are local to each document; replacement must fail, never rebase.
            var boundEpoch = browser.javascriptEpoch
            var before = snapshot(false, boundEpoch)
            verify(drmPlaybackReady(before))
            verify(before.drm.encryptedEvents > 0)
            verify(before.drm.messages > 0)
            verify(before.media.width > 0 && before.media.height > 0)
            console.log("PUBLIC_DRM_BASELINE " + JSON.stringify(before))
            // Seek beyond startup rather than qualifying only a possible clear lead-in.
            verify(js("(function(){const media=window.__feriaPublicDrmSnapshot();"
                + "if(media.length!==1||media[0].id!==" + before.media.id
                + "||media[0].keysId!==" + before.media.keysId + ")return false;"
                + "const v=Array.from(document.querySelectorAll('video')).find(v=>v.mediaKeys);"
                + "if(!v||!Number.isFinite(v.duration)||v.duration<20)return false;v.currentTime=12;return true})()",
                false, boundEpoch))
            tryVerify(function() {
                var s = snapshot(false, boundEpoch)
                return drmPlaybackReady(s) && s.documentEpoch === before.documentEpoch
                    && s.media.id === before.media.id && s.media.keysId === before.media.keysId
                    && s.media.time > 13 && s.media.decodedFrames > before.media.decodedFrames
            }, 15000, "Same document and DRM video must decode additional frames after seeking")
            verify(js("(function(){const media=window.__feriaPublicDrmSnapshot();"
                + "if(media.length!==1||media[0].id!==" + before.media.id
                + "||media[0].keysId!==" + before.media.keysId + ")return false;"
                + "Array.from(document.querySelectorAll('video')).filter(v=>v.mediaKeys).forEach(v=>v.pause());return true})()",
                false, boundEpoch))
            var paused = snapshot(false, boundEpoch)
            compare(paused.origin, "https://bitmovin.com")
            compare(paused.documentEpoch, before.documentEpoch)
            compare(paused.media.id, before.media.id)
            compare(paused.media.keysId, before.media.keysId)
            compare(paused.media.system, "com.widevine.alpha")
            compare(paused.media.paused, true)
            console.log("PUBLIC_DRM_PLAYBACK " + JSON.stringify(paused))
        }
    }
}
