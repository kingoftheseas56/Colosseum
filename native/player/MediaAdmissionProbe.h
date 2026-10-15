#pragma once
// MediaAdmissionProbe — the gate that decides "Colosseum can actually PLAY this"
// before a Vault shelf or a launch session ever promises it (Slice 6). It admits
// only on DECODED-FRAME evidence (dwidth > 0), never on FILE_LOADED: a file that
// merely opens (audio-only, a truncated header, a codec we can't decode) is not
// a playable video, and admitting it would put a dead tile on a shelf. Standing
// doctrine: FILE_LOADED is not decode proof, and `vid=no` rejects everything —
// so the probe decodes for real.
//
// Uses its OWN headless libmpv handle (vo=null, muted, paused, software decode),
// never MpvItem or the playing instance — PlayerPage's property allowlist and
// playback path are untouched. Blocking + self-contained; the caller runs it off
// the GUI thread.

#include <QString>

class MediaAdmissionProbe
{
public:
    enum class Verdict {
        Admitted,        // a video frame decoded (dwidth > 0)
        RejectedNoVideo, // opened but no decodable video frame (audio-only, etc.)
        RejectedError,   // demux/decode error (corrupt, encrypted, unsupported)
        RejectedTimeout  // no verdict within the deadline
    };

    struct Result {
        Verdict verdict = Verdict::RejectedError;
        int width = 0;
        int height = 0;
        QString detail; // human-readable reason (mpv error string, etc.)
    };

    // Probe a local file. Admits only when a decoded frame's dwidth > 0 arrives
    // within timeoutMs. Never throws, never hangs past the deadline.
    static Result probe(const QString& path, int timeoutMs = 8000);

    static bool isAdmitted(Verdict v) { return v == Verdict::Admitted; }
};
