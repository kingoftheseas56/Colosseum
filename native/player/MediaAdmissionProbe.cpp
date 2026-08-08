#include "MediaAdmissionProbe.h"

#include <QByteArray>
#include <QElapsedTimer>

#include <mpv/client.h>

#include <cstring>

namespace {

// Negative-control hook (test-only): when COLOSSEUM_ADMISSION_MODE=fileloaded,
// the gate weakens to admit on FILE_LOADED instead of dwidth>0 — proving the
// probe's whole value, since an audio-only or truncated file then wrongly
// admits. Unset in production (the real dwidth gate).
bool fileLoadedModeForTest()
{
    return qgetenv("COLOSSEUM_ADMISSION_MODE") == QByteArrayLiteral("fileloaded");
}

void setOpt(mpv_handle* h, const char* name, const char* val)
{
    mpv_set_option_string(h, name, val);
}

} // namespace

MediaAdmissionProbe::Result MediaAdmissionProbe::probe(const QString& path, int timeoutMs)
{
    Result r;

    mpv_handle* h = mpv_create();
    if (!h) {
        r.verdict = Verdict::RejectedError;
        r.detail = QStringLiteral("mpv_create failed");
        return r;
    }

    // Headless, user-config-free, network-helper-free, quiet software decode.
    setOpt(h, "config", "no");
    setOpt(h, "terminal", "no");
    setOpt(h, "msg-level", "all=no");
    setOpt(h, "vo", "null");
    setOpt(h, "ao", "null");
    setOpt(h, "mute", "yes");
    setOpt(h, "pause", "yes");
    setOpt(h, "hwdec", "no");
    setOpt(h, "ytdl", "no");
    setOpt(h, "load-scripts", "no");
    setOpt(h, "osc", "no");

    if (mpv_initialize(h) < 0) {
        mpv_terminate_destroy(h);
        r.verdict = Verdict::RejectedError;
        r.detail = QStringLiteral("mpv_initialize failed");
        return r;
    }

    mpv_observe_property(h, 1, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(h, 2, "dheight", MPV_FORMAT_INT64);

    const QByteArray p = path.toUtf8();
    const char* cmd[] = {"loadfile", p.constData(), nullptr};
    if (mpv_command(h, cmd) < 0) {
        mpv_terminate_destroy(h);
        r.verdict = Verdict::RejectedError;
        r.detail = QStringLiteral("loadfile failed");
        return r;
    }

    const bool fileLoadedGate = fileLoadedModeForTest();
    QElapsedTimer timer;
    timer.start();
    int64_t dw = 0, dh = 0;
    bool decided = false;

    while (!decided) {
        const qint64 remainingMs = static_cast<qint64>(timeoutMs) - timer.elapsed();
        if (remainingMs <= 0) {
            r.verdict = Verdict::RejectedTimeout;
            r.detail = QStringLiteral("timeout");
            break;
        }
        mpv_event* ev = mpv_wait_event(h, remainingMs / 1000.0);
        switch (ev->event_id) {
        case MPV_EVENT_NONE:
            continue; // woke with nothing; loop re-checks the deadline
        case MPV_EVENT_SHUTDOWN:
            r.verdict = Verdict::RejectedError;
            r.detail = QStringLiteral("shutdown");
            decided = true;
            break;
        case MPV_EVENT_FILE_LOADED:
            if (fileLoadedGate) { // negative-control weakening only
                r.verdict = Verdict::Admitted;
                decided = true;
                break;
            }
            // Real gate: if the file has no video track at all, reject now rather
            // than wait for a dwidth that will never arrive (e.g. an audio-only
            // file that FILE_LOADs but decodes no frame). This is a track-EXISTS
            // check, not `vid=no` (which would reject everything).
            {
                int64_t nTracks = 0;
                if (mpv_get_property(h, "track-list/count", MPV_FORMAT_INT64, &nTracks) >= 0
                    && nTracks > 0) {
                    bool hasVideo = false;
                    for (int64_t i = 0; i < nTracks && !hasVideo; ++i) {
                        const QByteArray key =
                            "track-list/" + QByteArray::number(i) + "/type";
                        if (char* type = mpv_get_property_string(h, key.constData())) {
                            if (std::strcmp(type, "video") == 0)
                                hasVideo = true;
                            mpv_free(type);
                        }
                    }
                    if (!hasVideo) {
                        r.verdict = Verdict::RejectedNoVideo;
                        r.detail = QStringLiteral("no video track");
                        decided = true;
                    }
                }
            }
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto* pc = static_cast<mpv_event_property*>(ev->data);
            if (pc->format == MPV_FORMAT_INT64 && pc->data) {
                const int64_t val = *static_cast<int64_t*>(pc->data);
                if (std::strcmp(pc->name, "dwidth") == 0)
                    dw = val;
                else if (std::strcmp(pc->name, "dheight") == 0)
                    dh = val;
                if (dw > 0) { // a real decoded video frame's dimensions
                    r.verdict = Verdict::Admitted;
                    r.width = static_cast<int>(dw);
                    r.height = static_cast<int>(dh);
                    decided = true;
                }
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            auto* ef = static_cast<mpv_event_end_file*>(ev->data);
            if (dw > 0) {
                r.verdict = Verdict::Admitted;
                r.width = static_cast<int>(dw);
                r.height = static_cast<int>(dh);
            } else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                r.verdict = Verdict::RejectedError;
                r.detail = QString::fromUtf8(mpv_error_string(ef->error));
            } else {
                r.verdict = Verdict::RejectedNoVideo; // opened, no decodable frame
            }
            decided = true;
            break;
        }
        default:
            break;
        }
    }

    mpv_terminate_destroy(h);
    return r;
}
