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
    } catch (const std::exception &error) {
        std::cerr << "player2_subtitle_schedule_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_subtitle_schedule_test: PASS\n";
    return EXIT_SUCCESS;
}
