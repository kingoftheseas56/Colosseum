// vault_admission_probe_harness — Slice 6. Drives the REAL MediaAdmissionProbe
// against real libmpv (its own headless handle) over ffmpeg-generated fixtures,
// proving the decoded-frame gate:
//   1. a valid tiny MP4 ADMITS with dwidth > 0;
//   2. plain non-video bytes named .mp4 are REJECTED (demux fails);
//   3. a truncated MP4 (header, no frame data) is REJECTED within the timeout;
//   4. an audio-only file is REJECTED — it FILE_LOADs but decodes no video frame,
//      the exact vacuity a FILE_LOADED-only gate would wave through.
//
// Negative control (run live, not in the default gate): with
// COLOSSEUM_ADMISSION_MODE=fileloaded the gate weakens to FILE_LOADED and the
// audio/truncated REJECT checks go red — proving the dwidth gate is load-bearing.
//
// House contract: prints VAULT_ADMISSION_PROBE_OK on success; "FAIL: <msg>" per
// failure + exit(1). Headless (raw mpv handle, no window); every wait is inside
// the probe's own event loop with a hard deadline — no sleeps.

#include "player/MediaAdmissionProbe.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstdlib>

namespace {

int g_fails = 0;

void check(bool ok, const char* msg)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}

const char* verdictName(MediaAdmissionProbe::Verdict v)
{
    switch (v) {
    case MediaAdmissionProbe::Verdict::Admitted: return "Admitted";
    case MediaAdmissionProbe::Verdict::RejectedNoVideo: return "RejectedNoVideo";
    case MediaAdmissionProbe::Verdict::RejectedError: return "RejectedError";
    case MediaAdmissionProbe::Verdict::RejectedTimeout: return "RejectedTimeout";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString dir = QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/");
    const int kTimeout = 15000;

    const auto tiny = MediaAdmissionProbe::probe(dir + QStringLiteral("tiny.mp4"), kTimeout);
    std::fprintf(stderr, "tiny.mp4 -> %s %dx%d\n", verdictName(tiny.verdict), tiny.width, tiny.height);
    check(tiny.verdict == MediaAdmissionProbe::Verdict::Admitted,
          "tiny.mp4 must ADMIT on a decoded frame");
    check(tiny.width > 0, "tiny.mp4 must report dwidth > 0");

    const auto notv = MediaAdmissionProbe::probe(dir + QStringLiteral("not-a-video.mp4"), kTimeout);
    std::fprintf(stderr, "not-a-video.mp4 -> %s\n", verdictName(notv.verdict));
    check(notv.verdict != MediaAdmissionProbe::Verdict::Admitted,
          "non-video bytes must be REJECTED");

    const auto trunc = MediaAdmissionProbe::probe(dir + QStringLiteral("truncated.mp4"), kTimeout);
    std::fprintf(stderr, "truncated.mp4 -> %s\n", verdictName(trunc.verdict));
    check(trunc.verdict != MediaAdmissionProbe::Verdict::Admitted,
          "truncated MP4 must be REJECTED (no decodable frame)");

    const auto audio = MediaAdmissionProbe::probe(dir + QStringLiteral("tiny-audio.m4a"), kTimeout);
    std::fprintf(stderr, "tiny-audio.m4a -> %s\n", verdictName(audio.verdict));
    check(audio.verdict != MediaAdmissionProbe::Verdict::Admitted,
          "audio-only must be REJECTED (FILE_LOADs but no video frame)");

    if (g_fails == 0) {
        std::printf("VAULT_ADMISSION_PROBE_OK\n");
        return 0;
    }
    std::fprintf(stderr, "VAULT_ADMISSION_PROBE FAILED (%d checks)\n", g_fails);
    return 1;
}
