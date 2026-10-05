#include "reader2/mediaoverlay/CueTimeline.h"
#include <QCryptographicHash>
#include <utility>

namespace MediaOverlay {
bool CueTimeline::append(Cue cue)
{
    if (cue.beginMs < 0 || (cue.endMs && *cue.endMs <= cue.beginMs)) return false;
    cue.order = m_cues.size();
    QByteArray key;
    const auto field = [&](const QString &s) { const auto bytes = s.toUtf8(); key += QByteArray::number(bytes.size()) + ':' + bytes; };
    field(cue.textPath); field(cue.fragmentId); field(cue.audioPath);
    field(QString::number(cue.beginMs)); field(cue.endMs ? QString::number(*cue.endMs) : QStringLiteral("open"));
    field(cue.sourceTag);
    cue.identity = QString::number(cue.order) + ':' + QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
    m_cues.append(std::move(cue));
    return true;
}
std::optional<qint64> CueTimeline::effectiveEnd(qsizetype index) const
{
    const auto &cue = m_cues[index];
    if (cue.endMs) return cue.endMs;
    for (qsizetype i = index+1; i < m_cues.size(); ++i)
        if (m_cues[i].audioPath == cue.audioPath) return m_cues[i].beginMs;
    return std::nullopt;
}
const Cue *CueTimeline::cueAt(const QString &audioPath, qint64 ms) const
{
    if (audioPath.isEmpty() || ms < 0) return nullptr;
    const Cue *preceding = nullptr;
    bool future = false;
    for (qsizetype i = 0; i < m_cues.size(); ++i) {
        const auto &cue = m_cues[i];
        if (cue.audioPath != audioPath) continue;
        const auto end = effectiveEnd(i);
        if (ms >= cue.beginMs && (!end || ms < *end)) return &cue;
        if (cue.beginMs > ms) future = true;
        else if (!preceding || cue.beginMs > preceding->beginMs) preceding = &cue;
    }
    // Only internal gaps inherit the preceding cue. Past the final finite range is none.
    return future ? preceding : nullptr;
}
const Cue *CueTimeline::cueFor(const QString &path, const QString &fragment) const
{
    for (const auto &cue : m_cues) if (cue.textPath == path && cue.fragmentId == fragment) return &cue;
    return nullptr;
}
bool CueTimeline::owns(const Cue &cue) const
{
    return cue.order >= 0 && cue.order < m_cues.size() && m_cues[cue.order].identity == cue.identity;
}
const Cue *CueTimeline::next(const Cue &cue) const
{
    return owns(cue) && cue.order+1 < m_cues.size() ? &m_cues[cue.order+1] : nullptr;
}
const Cue *CueTimeline::previous(const Cue &cue) const
{
    return owns(cue) && cue.order > 0 ? &m_cues[cue.order-1] : nullptr;
}
const Cue *CueTimeline::firstCueOf(const QString &path) const
{
    for (const auto &cue : m_cues) if (cue.textPath == path) return &cue;
    return nullptr;
}
QVector<const Cue *> CueTimeline::cuesOf(const QString &path) const
{
    QVector<const Cue *> result;
    if (!path.isEmpty()) for (const auto &cue : m_cues) if (cue.audioPath == path) result.append(&cue);
    return result;
}
}
