#ifndef COLOSSEUM_SUBTITLESIDECAR_H
#define COLOSSEUM_SUBTITLESIDECAR_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// Pure SRT / WebVTT sidecar subtitle parsing for the Android Media3 player (no
// Android or player dependencies, so it is unit-tested on the host).
namespace Colosseum::Player {

struct SidecarCue {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

// Inflates a gzip stream (RFC 1952). Returns false on any malformed input.
bool gunzipBytes(const QByteArray &gz, QByteArray *out);

// Raw file bytes -> text. Unwraps gzip, honours UTF-8/UTF-16 BOMs, decodes UTF-8
// and falls back to Latin-1 when the bytes are not valid UTF-8.
QString decodeSubtitleBytes(const QByteArray &bytes);

// SRT or WebVTT text -> cues sorted by start time. Unparseable input yields an
// empty list. Simple markup (<i>, <b>, {\an8}, ...) is stripped.
QList<SidecarCue> parseSidecarSubtitles(const QString &text);

// Indices (ascending) of the cues showing at timeMs. `cues` must be sorted by start.
QList<int> activeSidecarCues(const QList<SidecarCue> &cues, qint64 timeMs);

} // namespace Colosseum::Player

#endif // COLOSSEUM_SUBTITLESIDECAR_H
