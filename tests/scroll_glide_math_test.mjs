// scroll_glide_math_test — locks the ScrollGlide velocity-accumulator math (2026-08-02).
//
// The new ScrollGlide replaces the old fixed 420ms NumberAnimation (restarted on every wheel
// event) with a frame-driven exponential drain over an accumulating target:
//
//   onWheel:  pending += deltaPx          (accumulate; bursts grow the target, never restart)
//             (clamp the running target to the scrollable range)
//   per frame @ ~16ms:
//             frames  = frameTime/16.67            (frame-rate independent)
//             step    = pending * (1 - decay^frames)
//             contentY += step;  pending -= step    (drain; when |pending|<0.5, snap to 0 & stop)
//
// This file reimplements that math as a pure JS simulator so we can assert the brief's
// behavioural requirements without a GUI: notch distance, settle time, burst accumulation,
// trackpad proportional handling, and boundary clamping. The QML component must match these
// constants exactly — drift here means the QML drifted from the spec.
//
// Run: node tests/scroll_glide_math_test.mjs
"use strict";

// ── the constants the QML must use (single source of truth; mirror in ScrollGlide.qml) ──
export const TUNING = {
    pxPerNotch: 168,      // a 120-unit wheel notch -> 1.4px/angle unit, matching Long Strip
    drainFraction: 0.38,  // fraction of remaining pending drained per 60fps-equivalent frame
    maxBurstPx: 6000,     // cap so an uncontrolled wheel burst stays controllable
    settleThresholdPx: 0.5, // |pending| below this flushes the residual & stops the drain
    msPerFrame: 1000 / 60
};
// derived: per-frame decay multiplier
export const DECAY = 1 - TUNING.drainFraction; // ≈ 0.66

// settleMs: how long a single isolated notch takes to drain to rest, given the tuning.
// Closed form for the exponential drain reaching the settle threshold: solve
// |pending0| * decay^(frames) = threshold  ->  frames = ln(threshold/|p0|) / ln(decay)
export function settleMs(px0, frameTimeMs = TUNING.msPerFrame) {
    if (px0 === 0) return 0;
    const frames = Math.log(TUNING.settleThresholdPx / Math.abs(px0)) / Math.log(DECAY);
    return frames * frameTimeMs;
}

// notchDistance: how far ONE standard wheel notch (angleDelta 120) should travel.
export function notchDistance(angleDeltaY) {
    return angleDeltaY * TUNING.pxPerNotch / 120;
}

// pushWheel: accumulate a wheel event into the controller state.
//   pixelDelta.y != 0  -> trackpad/precision: use the high-res delta directly (proportional).
//   else               -> mouse wheel: angleDelta -> px via pxPerNotch (quantized notches).
// State: { contentY, pending, lastWheelMs }. hmax is the max contentY (contentHeight-height).
export function pushWheel(state, e, hmax) {
    const dy = (e.pixelDeltaY !== 0) ? e.pixelDeltaY : notchDistance(e.angleDeltaY);
    // wheel up (positive y) scrolls content toward the top -> decrease contentY.
    let target = state.pending + (-dy);              // accumulate, do NOT reset
    target = Math.max(-TUNING.maxBurstPx, Math.min(TUNING.maxBurstPx, target));
    state.pending = target;
    state.lastWheelMs = e.ms;
    return state;
}

// stepFrame: advance the drain by one frame. Returns the new contentY delta applied.
export function stepFrame(state, frameTimeMs, hmax) {
    if (state.pending === 0) return 0;
    // below the settle threshold: flush the tiny residual to contentY (clamped) and stop.
    // This is what makes the destination reach the full target instead of dropping the last
    // sub-pixel slice — the visible "arrived exactly" at the end of a glide.
    if (Math.abs(state.pending) <= TUNING.settleThresholdPx) {
        let newY = state.contentY + state.pending;
        if (newY < 0) newY = 0;
        else if (newY > hmax) newY = hmax;
        state.contentY = newY;
        state.pending = 0;
        return 0;
    }
    const frames = Math.max(0.25, frameTimeMs / TUNING.msPerFrame); // frame-rate independent, min 0.25
    const step = state.pending * (1 - Math.pow(DECAY, frames));
    const newY = state.contentY + step;
    // boundary clamp — grounded, no overscroll. Return early so the trailing pending
    // adjustment never re-corrupts a clamped state (the original bug: clamp zeroed pending,
    // then `pending -= step` flipped its sign and motion resumed in the wrong direction).
    if (newY < 0) { state.contentY = 0; state.pending = 0; return step; }
    if (newY > hmax) { state.contentY = hmax; state.pending = 0; return step; }
    state.contentY = newY;
    state.pending -= step;
    return step;
}

// cancelGlide: a drag / scrollbar grab / touch-flick cancels pending wheel motion and
// rebases the controller to the live contentY.
export function cancelGlide(state, liveContentY) {
    state.pending = 0;
    state.contentY = liveContentY;
    return state;
}

// ── simulator: drain pending to rest, counting frames/time ──
function drainToRest(state, hmax, frameTimeMs = TUNING.msPerFrame) {
    let frames = 0;
    // hard safety cap so a bug can't infinite-loop the test
    while (state.pending !== 0 && frames < 1000) {
        stepFrame(state, frameTimeMs, hmax);
        frames++;
    }
    return { frames, ms: frames * frameTimeMs };
}

let fails = 0;
function check(cond, label) { if (!cond) { console.log("FAIL: " + label); fails++; } }
function approx(a, b, eps) { return Math.abs(a - b) <= eps; }

// ─────────────────────────── the assertions ───────────────────────────
// (guarded so importing this module for debugging does not run the suite)
function runSuite() {

// 1. A 120-unit mouse-wheel notch matches Long Strip's 1.4px/angle-unit intake (168px).
{
    const d = notchDistance(120);
    check(d === 168, "120-unit notch == 168px exactly (got " + d + ")");
    check(d === TUNING.pxPerNotch, "120-unit notch == pxPerNotch exactly (got " + d + ")");
}

// 2. One notch reaches the reader-style soft tail without the old fixed 420ms animation.
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax); // wheel DOWN from the top
    const { frames, ms } = drainToRest(state, hmax);
    check(ms >= 150 && ms <= 250, "single notch settles in 150–250ms (got " + ms.toFixed(0) + "ms, " + frames + " frames)");
    check(ms < 420, "single notch NEVER takes 420ms (got " + ms.toFixed(0) + "ms)");
    // and it travelled ~the full notch distance
    check(approx(state.contentY, 168, 1.0), "single notch travelled ~168px (got " + state.contentY.toFixed(2) + ")");
}

// 3. Three rapid notches accumulate movement: farther AND faster than three separately
//    restarted 420ms tails. Rapid = back-to-back, no drain between.
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
    // while accumulating, pending should be ~3x a single notch (the burst grew the target)
    check(approx(state.pending, 3 * 168, 1.0), "three rapid notches accumulate to ~504px target (got " + state.pending.toFixed(2) + ")");
    const { ms } = drainToRest(state, hmax);
    check(approx(state.contentY, 504, 1.0), "three rapid notches travel ~504px total (got " + state.contentY.toFixed(2) + ")");
    // The brief's "faster than three separately restarted 420ms tails": a burst must NOT cost
    // 3×420ms (1260ms). It may take a little longer than a single notch (more distance to
    // cover) but must stay well under the old per-notch 420ms × 3 regime.
    check(ms < 420, "burst settles in far less than three separate 420ms tails (got " + ms.toFixed(0) + "ms)");
}

// 3b. Three notches with realistic ~16ms gaps (a fast physical burst) still accumulate and
//     travel substantially farther than a single notch.
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
    stepFrame(state, 16.67, hmax);
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 16 }, hmax);
    stepFrame(state, 16.67, hmax);
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 33 }, hmax);
    const before = state.contentY + state.pending; // where it will end up
    drainToRest(state, hmax);
    check(state.contentY > 168 * 1.5, "spaced burst travels >1.5× a single notch (got " + state.contentY.toFixed(0) + ")");
    check(approx(state.contentY, before, 1.0), "spaced burst destination equals accumulated target (got " + state.contentY.toFixed(0) + " vs " + before.toFixed(0) + ")");
}

// 4. Trackpad-style pixelDelta remains proportional and unquantized (not snapped to notch px).
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    // a precision trackpad fires pixelDelta like 13px ticks — must NOT round to 168px notches.
    pushWheel(state, { pixelDeltaY: -13, angleDeltaY: -120, ms: 0 }, hmax); // scroll down 13px
    check(state.pending === 13, "trackpad pixelDelta used directly, proportional (pending=" + state.pending + ")");
    drainToRest(state, hmax);
    check(approx(state.contentY, 13, 0.5), "trackpad 13px delta -> 13px travel (got " + state.contentY.toFixed(2) + ")");
}
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: -7, angleDeltaY: 0, ms: 0 }, hmax);
    check(state.pending === 7, "small trackpad pixelDelta preserved exactly (pending=" + state.pending + ")");
}

// 4b. pixelDelta and angleDelta are not both applied for the same event (pixelDelta wins exclusively).
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: -20, angleDeltaY: -120, ms: 0 }, hmax); // both present
    check(state.pending === 20, "when pixelDelta present, angleDelta is ignored (no double-apply) (pending=" + state.pending + ")");
}

// 6. Targets clamp at the top and bottom — no overscroll, no snap-back fight.
{
    // bottom boundary: start near the end, one notch can't push past hmax
    const hmax = 1000;
    const state = { contentY: 950, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax); // wheel down
    drainToRest(state, hmax);
    check(state.contentY === 1000, "bottom clamp: contentY stops exactly at hmax (got " + state.contentY + ")");
    check(state.pending === 0, "bottom clamp: pending cleared, no residual fight (got " + state.pending + ")");
}
{
    // top boundary: start near the top, wheel up can't go below 0
    const hmax = 1000;
    const state = { contentY: 50, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: 120, ms: 0 }, hmax); // wheel up
    drainToRest(state, hmax);
    check(state.contentY === 0, "top clamp: contentY stops exactly at 0 (got " + state.contentY + ")");
    check(state.pending === 0, "top clamp: pending cleared (got " + state.pending + ")");
}

// 7. Scrollbar/drag cancel: cancelGlide rebases to live contentY and zeroes pending.
{
    const hmax = 1000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax); // wheel down, pending ~168
    check(state.pending > 100, "before cancel, pending is large (got " + state.pending + ")");
    // user grabs scrollbar mid-glide and drags to 600
    cancelGlide(state, 600);
    check(state.pending === 0, "cancel zeroes pending (got " + state.pending + ")");
    check(state.contentY === 600, "cancel rebases contentY to live value (got " + state.contentY + ")");
    // a subsequent frame must NOT resume the obsolete target
    const step = stepFrame(state, 16.67, hmax);
    check(step === 0, "after cancel, next frame does not resume motion (step=" + step + ")");
    check(state.contentY === 600, "contentY stays at 600 after cancel + frame (got " + state.contentY + ")");
}

// 8. Touch/flick input cancels pending wheel movement (same path as drag cancel).
{
    const hmax = 1000;
    const state = { contentY: 100, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
    cancelGlide(state, 320); // touch dragged to 320
    check(state.pending === 0 && state.contentY === 320, "touch drag cancels pending wheel motion");
}

// gentle single notch still allows precise navigation: small pixelDelta drifts by exactly that.
{
    const hmax = 100000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    pushWheel(state, { pixelDeltaY: -2, angleDeltaY: 0, ms: 0 }, hmax); // a whisper of trackpad down
    drainToRest(state, hmax);
    check(approx(state.contentY, 2, 0.5), "gentle 2px trackpad drift -> 2px (precise) (got " + state.contentY.toFixed(2) + ")");
}

// frame-rate independence: 144Hz (6.94ms) and 30Hz (33.3ms) reach ~the same place in ~the same time.
{
    const run = (ft) => {
        const hmax = 100000;
        const s = { contentY: 0, pending: 0, lastWheelMs: 0 };
        pushWheel(s, { pixelDeltaY: 0, angleDeltaY: -120, ms: 0 }, hmax);
        const { ms } = drainToRest(s, hmax, ft);
        return { finalY: s.contentY, ms };
    };
    const r60 = run(16.67), r144 = run(6.94), r30 = run(33.33);
    check(approx(r60.finalY, 168, 1) && approx(r144.finalY, 168, 1) && approx(r30.finalY, 168, 1),
        "frame-rate independent destination (60=" + r60.finalY.toFixed(1) + " 144=" + r144.finalY.toFixed(1) + " 30=" + r30.finalY.toFixed(1) + ")");
    // settle time should be comparable across frame rates (within ~1.5x)
    const ratio = Math.max(r60.ms, r144.ms, r30.ms) / Math.max(1, Math.min(r60.ms, r144.ms, r30.ms));
    check(ratio <= 1.5, "settle time comparable across frame rates (ratio " + ratio.toFixed(2) + ")");
}

// velocity cap: an absurd wheel burst is capped (controllable, never runs away).
{
    const hmax = 1000000;
    const state = { contentY: 0, pending: 0, lastWheelMs: 0 };
    for (let i = 0; i < 50; i++) pushWheel(state, { pixelDeltaY: 0, angleDeltaY: -120, ms: i }, hmax);
    check(state.pending <= TUNING.maxBurstPx, "burst capped at maxBurstPx (pending=" + state.pending + ")");
}

} // end runSuite

// guarded: importing for debugging must not run the suite or exit the process.
const invokedDirectly = (() => {
    const a1 = process.argv[1];
    if (!a1) return false;
    return a1.replace(/\\/g, "/").endsWith("scroll_glide_math_test.mjs");
})();
if (invokedDirectly) {
    runSuite();
    console.log(fails ? ("FAILS: " + fails) : "scroll_glide_math_test: ALL PASS");
    process.exit(fails ? 1 : 0);
}
