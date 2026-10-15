#pragma once

#include "DemuxSession.h" // SubtitleCue

#include <QtCore/QString>

#include <vector>

struct AVCodecParameters;
struct AVCodecContext;
struct AVPacket;
struct AVRational;

namespace Colosseum::Player2 {

// Decodes one embedded subtitle stream into timed SubtitleCue products. Text subtitles (ASS/SRT/
// mov_text) become plain text with start/end; bitmap subtitles (PGS/DVD) become RGBA region cues.
// Every cue carries the generation so a seek/track flush drops stale cues before they render.
// This is the engine half of the current player's mpv subtitle track (QML paints the cue later).
class SubtitlePipeline
{
public:
    SubtitlePipeline();
    ~SubtitlePipeline();

    bool open(const AVCodecParameters *params, AVRational streamTimeBase, QString *error);
    void close();
    bool isOpen() const noexcept { return m_context != nullptr; }

    // Decode one packet from the selected subtitle stream; append 0+ cues (each already tagged with
    // the generation and stream index).
    bool decode(const AVPacket *packet, quint64 generation, int streamIndex,
                std::vector<SubtitleCue> *out, QString *error);
    void flush();

private:
    AVCodecContext *m_context = nullptr;
    int m_timeBaseNum = 1;
    int m_timeBaseDen = 1000;
};

} // namespace Colosseum::Player2
