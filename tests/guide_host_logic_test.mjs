import { loadQmlJs } from "./guide_content_contract.mjs";

// Pure host-state contract for the immersive Guide overlay (Task 5). No QML, no host refs — just the
// resume decision a reader/player needs when Guide opens over it and then closes.
const mod = loadQmlJs("qml/guide/GuideHostLogic.js", ["capturePlayback", "shouldResume"]);

let failures = 0;
function check(condition, label) {
    if (!condition) { console.log("FAIL: " + label); failures++; }
}

// capturePlayback(wasPlaying) — the snapshot taken the instant Guide opens over a host.
check(mod.capturePlayback(true).resumeOnClose === true, "playing snapshot remembers resume intent");
check(mod.capturePlayback(false).resumeOnClose === false, "paused snapshot stays paused");
check(mod.capturePlayback(undefined).resumeOnClose === false, "unknown playback state does not fabricate resume intent");
check(mod.capturePlayback("yes").resumeOnClose === false, "only a real boolean true counts as playing (no truthy coercion)");

// shouldResume(snapshot, mediaStillPresent) — may closing Guide resume playback?
check(mod.shouldResume({ resumeOnClose: true }, true) === true, "resume only after a Guide-owned pause on still-present media");
check(mod.shouldResume({ resumeOnClose: true }, false) === false, "media closed while Guide was open never resumes");
check(mod.shouldResume({ resumeOnClose: false }, true) === false, "a pause the person already owned is never resumed by Guide");
check(mod.shouldResume(null, true) === false, "a missing snapshot never resumes");
check(mod.shouldResume({ resumeOnClose: true }, undefined) === false, "unknown media presence never resumes");

console.log(failures === 0 ? "guide_host_logic_test: ALL PASS" : ("guide_host_logic_test: " + failures + " FAIL"));
process.exit(failures === 0 ? 0 : 1);
