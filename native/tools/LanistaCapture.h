#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <utility>

namespace lanista {

struct CaptureSpec {
    QString mp4Path;
    QString gifPath;
    int captureFps = 15;
    int gifFps = 15;
    int width = 1280;
    int height = 720;
};

inline QStringList sceneRecordingArgs(const CaptureSpec& spec, const QString& framePattern,
                                       double inputFps = -1.0)
{
    const QString scale = QStringLiteral(
        "scale=%1:%2:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black")
        .arg(spec.width).arg(spec.height);
    const double fps = inputFps > 0.0 ? inputFps : double(spec.captureFps);
    return {
        QStringLiteral("-y"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-framerate"), QString::number(fps, 'f', 3),
        QStringLiteral("-i"), framePattern,
        QStringLiteral("-vf"), scale,
        QStringLiteral("-an"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("18"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        spec.mp4Path
    };
}

inline QStringList gifArgs(const CaptureSpec& spec)
{
    const QString filter = QStringLiteral(
        "fps=%1,scale=%2:%3:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=%2:%3:(ow-iw)/2:(oh-ih)/2:color=black,split[s0][s1];"
        "[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=bayer:bayer_scale=3")
        .arg(spec.gifFps).arg(spec.width).arg(spec.height);
    return {
        QStringLiteral("-y"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-i"), spec.mp4Path,
        QStringLiteral("-filter_complex"), filter,
        QStringLiteral("-loop"), QStringLiteral("0"),
        spec.gifPath
    };
}

class CaptureController {
public:
    using FrameGrabber = std::function<QImage(QString*)>;

    CaptureController(QString outputRoot, CaptureSpec spec, FrameGrabber frameGrabber)
        : m_outputRoot(std::move(outputRoot)),
          m_spec(std::move(spec)),
          m_frameGrabber(std::move(frameGrabber)) {}

    CaptureController(QString outputRoot, FrameGrabber frameGrabber)
        : CaptureController(std::move(outputRoot), CaptureSpec{}, std::move(frameGrabber)) {}

    ~CaptureController() { abort(); }

    bool isActive() const { return m_active; }
    QStringList artifacts() const { return m_artifacts; }

    bool start(const QString& name, QString* detail = nullptr)
    {
        const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
        if (!safeName.match(name).hasMatch())
            return fail(detail, QStringLiteral("capture name must use letters, digits, _ or -"));
        if (m_spec.width <= 0 || m_spec.height <= 0)
            return fail(detail, QStringLiteral("capture dimensions must be positive"));
        if (m_spec.captureFps <= 0 || m_spec.gifFps <= 0)
            return fail(detail, QStringLiteral("capture frame rates must be positive"));
        if (m_active)
            return fail(detail, QStringLiteral("a capture is already active"));
        if (!m_frameGrabber)
            return fail(detail, QStringLiteral("scene-frame grabber is unavailable"));

        m_ffmpegPath = bundledFfmpegPath();
        if (m_ffmpegPath.isEmpty())
            return fail(detail, QStringLiteral("bundled ffmpeg.exe was not found beside lanista"));

        QDir().mkpath(m_outputRoot);
        m_lock = std::make_unique<QLockFile>(
            QDir(m_outputRoot).filePath(QStringLiteral(".lanista-reddit-capture.lock")));
        m_lock->setStaleLockTime(120000);
        if (!m_lock->tryLock(0)) {
            qint64 holderPid = 0;
            QString holderHost, holderApp;
            m_lock->getLockInfo(&holderPid, &holderHost, &holderApp);
            m_lock.reset();
            return fail(detail, QStringLiteral("another Lanista presentation capture is already running")
                                + (holderPid ? QStringLiteral(" (pid %1)").arg(holderPid) : QString{}));
        }

        m_spec.mp4Path = QDir(m_outputRoot).filePath(name + QStringLiteral(".mp4"));
        m_spec.gifPath = QDir(m_outputRoot).filePath(name + QStringLiteral(".gif"));
        m_logPath = QDir(m_outputRoot).filePath(name + QStringLiteral("-ffmpeg.log"));
        QFile::remove(m_spec.mp4Path);
        QFile::remove(m_spec.gifPath);
        QFile::remove(m_logPath);

        m_frameDir = QDir(m_outputRoot).filePath(QStringLiteral(".lanista-frames-") + name);
        QDir(m_frameDir).removeRecursively();
        if (!QDir().mkpath(m_frameDir)) {
            releaseLock();
            return fail(detail, QStringLiteral("could not create scene-frame directory"));
        }

        m_frameIndex = 0;
        m_sceneDurationMs = 0;
        m_active = true;
        QString frameWhy;
        if (!captureSceneFrame(&frameWhy)) {
            m_active = false;
            cleanupFrames();
            releaseLock();
            return fail(detail, frameWhy);
        }
        if (detail) *detail = QStringLiteral("recording scene -> ") + m_spec.mp4Path;
        return true;
    }

    bool hold(int holdMs, QString* detail = nullptr)
    {
        if (!m_active)
            return fail(detail, QStringLiteral("presentation hold requires an active capture"));
        if (holdMs < 0 || holdMs > 5000)
            return fail(detail, QStringLiteral("presentation_hold_ms must be between 0 and 5000"));

        QElapsedTimer clock;
        clock.start();
        const int frameMs = qMax(1, 1000 / m_spec.captureFps);
        qint64 nextFrameAt = 0;
        const int before = m_frameIndex;
        while (clock.elapsed() < holdMs) {
            QString why;
            if (!captureSceneFrame(&why))
                return fail(detail, why);
            nextFrameAt += frameMs;
            const qint64 waitMs = nextFrameAt - clock.elapsed();
            if (waitMs > 0)
                QThread::msleep(unsigned(waitMs));
        }
        if (m_frameIndex == before) {
            QString why;
            if (!captureSceneFrame(&why))
                return fail(detail, why);
        }
        m_sceneDurationMs += holdMs;
        if (detail)
            *detail = QStringLiteral("held %1 ms, captured %2 scene frames")
                          .arg(holdMs).arg(m_frameIndex - before);
        return true;
    }

    bool stop(QString* detail = nullptr)
    {
        if (!m_active)
            return fail(detail, QStringLiteral("no active capture to stop"));

        QString frameWhy;
        if (!captureSceneFrame(&frameWhy)) {
            m_active = false;
            cleanupFrames();
            releaseLock();
            return fail(detail, frameWhy);
        }
        m_active = false;

        const QString pattern = QDir(m_frameDir).filePath(QStringLiteral("frame-%06d.bmp"));
        const double observedFps = m_sceneDurationMs > 0
            ? qBound(0.25, (double(m_frameIndex) * 1000.0) / double(m_sceneDurationMs),
                     double(m_spec.captureFps))
            : 1.0;
        QProcess encode;
        encode.setProgram(m_ffmpegPath);
        encode.setArguments(sceneRecordingArgs(m_spec, pattern, observedFps));
        encode.setStandardOutputFile(QProcess::nullDevice());
        encode.setStandardErrorFile(m_logPath, QIODevice::Truncate);
        encode.start();
        if (!encode.waitForStarted(5000) || !encode.waitForFinished(60000)
            || encode.exitStatus() != QProcess::NormalExit || encode.exitCode() != 0
            || !QFileInfo::exists(m_spec.mp4Path) || QFileInfo(m_spec.mp4Path).size() < 1000) {
            if (encode.state() != QProcess::NotRunning) {
                encode.kill();
                encode.waitForFinished(2000);
            }
            const QString tail = logTail(m_logPath);
            cleanupFrames();
            releaseLock();
            return fail(detail, QStringLiteral("scene-frame MP4 conversion failed: ") + tail);
        }

        QProcess gif;
        gif.setProgram(m_ffmpegPath);
        gif.setArguments(gifArgs(m_spec));
        gif.setStandardOutputFile(QProcess::nullDevice());
        gif.setStandardErrorFile(m_logPath, QIODevice::Append);
        gif.start();
        if (!gif.waitForStarted(5000) || !gif.waitForFinished(60000)
            || gif.exitStatus() != QProcess::NormalExit || gif.exitCode() != 0
            || !QFileInfo::exists(m_spec.gifPath) || QFileInfo(m_spec.gifPath).size() < 1000) {
            if (gif.state() != QProcess::NotRunning) {
                gif.kill();
                gif.waitForFinished(2000);
            }
            const QString tail = logTail(m_logPath);
            cleanupFrames();
            releaseLock();
            return fail(detail, QStringLiteral("GIF conversion failed: ") + tail);
        }

        m_artifacts << m_spec.mp4Path << m_spec.gifPath;
        cleanupFrames();
        releaseLock();
        if (detail)
            *detail = QStringLiteral("captured -> %1 | %2").arg(m_spec.mp4Path, m_spec.gifPath);
        return true;
    }

    void abort()
    {
        m_active = false;
        cleanupFrames();
        releaseLock();
    }

private:
    bool captureSceneFrame(QString* why)
    {
        if (!m_frameGrabber) {
            if (why) *why = QStringLiteral("scene-frame grabber is unavailable");
            return false;
        }
        QString grabWhy;
        const QImage image = m_frameGrabber(&grabWhy);
        if (image.isNull()) {
            if (why)
                *why = grabWhy.isEmpty()
                    ? QStringLiteral("scene-frame grab came back empty")
                    : grabWhy;
            return false;
        }
        const QString path = QDir(m_frameDir).filePath(
            QStringLiteral("frame-%1.bmp").arg(m_frameIndex, 6, 10, QLatin1Char('0')));
        if (!image.save(path, "BMP")) {
            if (why) *why = QStringLiteral("could not save scene frame: ") + path;
            return false;
        }
        ++m_frameIndex;
        if (why) why->clear();
        return true;
    }

    void cleanupFrames()
    {
        if (m_frameDir.isEmpty()) return;
        QDir(m_frameDir).removeRecursively();
        m_frameDir.clear();
        m_frameIndex = 0;
        m_sceneDurationMs = 0;
    }

    void releaseLock()
    {
        if (!m_lock) return;
        m_lock->unlock();
        m_lock.reset();
    }

    static bool fail(QString* detail, const QString& text)
    {
        if (detail) *detail = text;
        return false;
    }

    static QString bundledFfmpegPath()
    {
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("tools/ffmpeg.exe"));
        return QFileInfo::exists(path) ? QFileInfo(path).absoluteFilePath() : QString{};
    }

    static QString logTail(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        const qint64 keep = qMin<qint64>(4096, f.size());
        f.seek(qMax<qint64>(0, f.size() - keep));
        return QString::fromUtf8(f.readAll()).trimmed();
    }

    QString m_outputRoot;
    QString m_ffmpegPath;
    QString m_logPath;
    QString m_frameDir;
    CaptureSpec m_spec;
    FrameGrabber m_frameGrabber;
    int m_frameIndex = 0;
    qint64 m_sceneDurationMs = 0;
    bool m_active = false;
    std::unique_ptr<QLockFile> m_lock;
    QStringList m_artifacts;
};

} // namespace lanista
