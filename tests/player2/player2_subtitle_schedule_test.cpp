// Pins the cue-scheduling selection that syncs subtitles to the playback clock. Cues decode seconds
// ahead of playback (read-ahead), so the session buffers them and displays each only when the clock
// reaches its window — this is the "which cue is on screen at time T" decision. Half-open [start,end):
// start shows, end clears. Pure and headless; no demux/video needed.
#include "player2/core/Player2Session.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Colosseum::Player2;

namespace {
SubtitleCue at(qint64 startUs, qint64 endUs)
{
    SubtitleCue cue;
    cue.startUs = startUs;
    cue.endUs = endUs;
    return cue;
}
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    try {
        const std::vector<SubtitleCue> cues{at(100, 200), at(300, 400)};
        require(activeSubtitleCueIndex(cues, 50) == -1, "before the first cue, nothing is active");
        require(activeSubtitleCueIndex(cues, 100) == 0, "the start of a cue is inclusive");
        require(activeSubtitleCueIndex(cues, 150) == 0, "mid-cue selects that cue");
        require(activeSubtitleCueIndex(cues, 200) == -1, "the end of a cue is exclusive");
        require(activeSubtitleCueIndex(cues, 250) == -1, "between cues, nothing is active");
        require(activeSubtitleCueIndex(cues, 350) == 1, "the second cue activates in its window");
        require(activeSubtitleCueIndex(cues, 9999) == -1, "past the last cue, nothing is active");
        require(activeSubtitleCueIndex({}, 100) == -1, "an empty queue has no active cue");

        // Overlap: the later-starting cue wins so a newer presentation takes over.
        const std::vector<SubtitleCue> overlap{at(100, 300), at(200, 250)};
        require(activeSubtitleCueIndex(overlap, 220) == 1, "an overlapping newer cue wins");
        require(activeSubtitleCueIndex(overlap, 120) == 0, "before the overlap, the first cue shows");
        require(activeSubtitleCueIndex(overlap, 270) == 0, "after the newer cue ends, the first resumes");

        // PGS open-ended cues (end_display_time = UINT32_MAX -> a ~49-day window) are closed by the
        // stream's own clear segments and by the next composition. Measured on Hemanth's Tenet trace
        // 2026-07-26: every cue arrived with span=4294967.295s and lingered until the next line.
        const qint64 openEnd = 100 + 4294967295000LL;
        std::vector<SubtitleCue> pgs{at(100, openEnd), at(500, 500 + 4294967295000LL)};
        for (SubtitleCue &c : pgs) c.bitmap = true;
        capOpenBitmapCues(&pgs, 300); // the authored clear at 300 ends the FIRST cue only
        require(pgs[0].endUs == 300, "a clear segment caps the open cue at its own time");
        require(pgs[1].endUs == 500 + 4294967295000LL, "a cue starting later is untouched");
        require(activeSubtitleCueIndex(pgs, 320) == -1, "after the clear, nothing shows");
        capOpenBitmapCues(&pgs, 900); // a NEWER composition also closes the still-open one
        require(pgs[1].endUs == 900, "a replacing composition caps its predecessor");
        // Text cues are never capped: overlapping speakers are legitimate and carry real ends.
        std::vector<SubtitleCue> text{at(100, 4000)};
        capOpenBitmapCues(&text, 300);
        require(text[0].endUs == 4000, "text cues keep their authored ends");
    } catch (const std::exception &error) {
        std::cerr << "player2_subtitle_schedule_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_subtitle_schedule_test: PASS\n";
    return EXIT_SUCCESS;
}
