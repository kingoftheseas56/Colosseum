.pragma library

// Pure host-state logic for the immersive Guide overlay (Task 5). No QML, no host references — just
// the one decision a reader/player host needs: when Guide closes, may it resume playback, and only if
// GUIDE was the one that paused it. Kept pure so it is Node-testable and identical across the Theatre
// player and the readers. The overlay stays host-agnostic; the host feeds these two functions.

// The snapshot taken the instant Guide opens over a host. resumeOnClose is true ONLY when the media
// was genuinely playing (a strict boolean, no truthy coercion) — so a pause the person already owned
// is never later "resumed" just because Guide closed.
function capturePlayback(wasPlaying) {
    return { resumeOnClose: wasPlaying === true };
}

// Whether closing Guide should resume playback: true only when Guide owned the pause (resumeOnClose)
// AND the media is still present — never resurrect media the person closed while Guide was open.
function shouldResume(snapshot, mediaStillPresent) {
    return Boolean(snapshot && snapshot.resumeOnClose) && mediaStillPresent === true;
}
