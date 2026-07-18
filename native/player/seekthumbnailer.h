#pragma once

#include <QCache>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

// Seek-bar hover thumbnails (F9): one short-lived ffmpeg per hovered 5s bucket,
// latest-wins, LRU-cached as data: URLs. QML paints the tooltip; this owns the
// process transport and cache (QML-paints/C++-decides doctrine).
class SeekThumbnailer : public QObject
{
    Q_OBJECT

public:
    explicit SeekThumbnailer(QObject *parent = nullptr);
    ~SeekThumbnailer() override;

    Q_INVOKABLE void request(const QUrl &source, double timeSec);
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void thumbReady(double bucketSec, const QString &imageUrl);

private:
    static qint64 bucketOf(double timeSec);
    void startJob(qint64 bucket);
    void killJob();
    void onJobFinished(int exitCode, QProcess::ExitStatus status);

    QUrl m_source;
    QCache<qint64, QString> m_cache { 128 };
    QProcess *m_proc = nullptr;
    QTimer m_stallTimer;
    qint64 m_jobBucket = -1;
};
