#pragma once

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>
#include <algorithm>
#include <cmath>

// frameSwapped calls recordSwap() directly on the emitting thread. Only the opt-in path
// allocates or writes an artifact. A gap between swaps may include idle time;
// the caller must restrict samples to an active journey before treating it as jank.
class FrameTimingProbe {
public:
    explicit FrameTimingProbe(bool enabled) : m_enabled(enabled), m_epochMs(QDateTime::currentMSecsSinceEpoch())
    { if (m_enabled) m_clock.start(); }
    bool enabled() const { return m_enabled; }

    void recordSwap()
    {
        if (!m_enabled) return;
        const qint64 ns = m_clock.nsecsElapsed();
        QMutexLocker lock(&m_mutex);
        if (m_lastNs >= 0) m_frames.append({double(ns) / 1e6, double(ns - m_lastNs) / 1e6});
        m_lastNs = ns;
    }

    static QJsonObject summarize(const QVector<double> &intervals)
    {
        if (intervals.isEmpty()) return {{QStringLiteral("count"), 0}};
        QVector<double> sorted = intervals;
        std::sort(sorted.begin(), sorted.end());
        auto percentile = [&sorted](double p) {
            const int index = std::clamp(int(std::ceil(p * sorted.size())) - 1, 0, int(sorted.size()) - 1);
            return sorted[index];
        };
        int over16 = 0, over33 = 0;
        for (double ms : intervals) {
            if (ms > 16.7) ++over16;
            if (ms > 33.0) ++over33;
        }
        return {{QStringLiteral("count"), intervals.size()},
                {QStringLiteral("p50Ms"), percentile(0.50)},
                {QStringLiteral("p95Ms"), percentile(0.95)},
                {QStringLiteral("maxMs"), sorted.last()},
                {QStringLiteral("over16_7"), over16},
                {QStringLiteral("over33"), over33}};
    }

    QJsonObject artifact() const
    {
        QMutexLocker lock(&m_mutex);
        QJsonArray rows;
        QVector<double> intervals;
        for (const Frame &frame : m_frames) {
            rows.append(QJsonObject{{QStringLiteral("atMs"), frame.atMs},
                                    {QStringLiteral("intervalMs"), frame.intervalMs}});
            intervals.append(frame.intervalMs);
        }
        return {{QStringLiteral("schema"), QStringLiteral("colosseum.frame-timing.v1")},
                {QStringLiteral("armedEpochMs"), m_epochMs},
                {QStringLiteral("summary"), summarize(intervals)},
                {QStringLiteral("frames"), rows}};
    }

    bool writeArtifact(const QString &path) const
    {
        if (!m_enabled) return false;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(QJsonDocument(artifact()).toJson()) > 0;
    }

private:
    struct Frame { double atMs; double intervalMs; };
    bool m_enabled = false;
    qint64 m_epochMs = 0;
    QElapsedTimer m_clock;
    mutable QMutex m_mutex;
    qint64 m_lastNs = -1;
    QVector<Frame> m_frames;
};
