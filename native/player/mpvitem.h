// MpvItem — the playable mpv surface, a QQuickItem subclass of MpvQt's MpvAbstractItem.
// Lifted 1:1 from KDE mpvqt's video-player example (proven to play real video on our
// Qt 6.11 / MinGW), with one change: QML_ELEMENT is removed. Colosseum loads its QML
// live from disk (no qt_add_qml_module), so the type is registered by hand in main.cpp
//   qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
// and reached from QML with `import Colosseum.Player`.
#ifndef COLOSSEUM_MPVITEM_H
#define COLOSSEUM_MPVITEM_H

#include <QElapsedTimer>
#include <MpvAbstractItem>
#include <QTimer>
#include <QVariantList>

#include "playerbackendcontract.h"

class QProcess;

class MpvItem : public MpvAbstractItem, public PlayerBackendContract
{
    Q_OBJECT
public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() override;

    enum class AsyncIds {
        None,
        SetVolume,
        GetVolume,
        ExpandText,
    };
    Q_ENUM(AsyncIds)

    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    QVariantMap capabilities() const override;

    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    QString mediaTitle();

    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    double position();
    void setPosition(double value);

    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    double duration();

    Q_PROPERTY(QString formattedPosition READ formattedPosition NOTIFY positionChanged)
    QString formattedPosition() const;

    Q_PROPERTY(QString formattedDuration READ formattedDuration NOTIFY durationChanged)
    QString formattedDuration() const;

    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    bool pause();
    void setPause(bool value);

    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    int volume();
    void setVolume(int value);

    Q_PROPERTY(bool mute READ mute WRITE setMute NOTIFY muteChanged)
    bool mute();
    void setMute(bool value);

    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    double speed();
    void setSpeed(double value);

    Q_PROPERTY(QString audioTrack READ audioTrack WRITE setAudioTrack NOTIFY audioTrackChanged)
    QString audioTrack();
    void setAudioTrack(const QString &value);

    Q_PROPERTY(QString subtitleTrack READ subtitleTrack WRITE setSubtitleTrack NOTIFY subtitleTrackChanged)
    QString subtitleTrack();
    void setSubtitleTrack(const QString &value);

    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY trackListChanged)
    QVariantList audioTracks() const;

    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY trackListChanged)
    QVariantList subtitleTracks() const;

    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    QVariantList chapters() const;

    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    double audioDelay();
    void setAudioDelay(double value);

    Q_PROPERTY(double subDelay READ subDelay WRITE setSubDelay NOTIFY subDelayChanged)
    double subDelay();
    void setSubDelay(double value);

    Q_PROPERTY(double panscan READ panscan WRITE setPanscan NOTIFY videoFillChanged)
    double panscan();
    void setPanscan(double value);

    Q_PROPERTY(double videoZoom READ videoZoom WRITE setVideoZoom NOTIFY videoFillChanged)
    double videoZoom();
    void setVideoZoom(double value);

    Q_PROPERTY(QString videoAspect READ videoAspect WRITE setVideoAspect NOTIFY videoFillChanged)
    QString videoAspect();
    void setVideoAspect(const QString &value);

    Q_PROPERTY(QUrl currentUrl READ currentUrl NOTIFY currentUrlChanged)
    QUrl currentUrl() const;

    // Decoded-frame truth (Agent Visibility Phase 2, Slice J1-Video-Seam, 2026-08-13):
    // the SAME two mpv properties MediaAdmissionProbe.cpp:59-60 observes on its own
    // headless handle ("dwidth"/"dheight"), mirrored here on the LIVE playing instance
    // so a caller can tell "a route succeeded" / "mpv opened the file" apart from "a
    // real video frame actually decoded" — the exact vacuity MediaAdmissionProbe closes
    // for the Vault admission gate (MediaAdmissionProbe.cpp:99-123: FILE_LOADED fires
    // for audio-only sources too, but dwidth/dheight never go positive for them). Zero
    // until the first real decoded frame arrives; reset to zero at the start of every
    // new load so a same-size reload cannot read "ready" from a stale value mpv never
    // re-announces (dwidth/dheight are change-notified, not re-sent on every load).
    // Read-only, observability-only: no playback/control path reads these.
    Q_PROPERTY(int decodedWidth READ decodedWidth NOTIFY decodedDimensionsChanged)
    int decodedWidth() const;

    Q_PROPERTY(int decodedHeight READ decodedHeight NOTIFY decodedDimensionsChanged)
    int decodedHeight() const;

    Q_PROPERTY(double cacheTime READ cacheTime NOTIFY cacheTimeChanged)
    double cacheTime() const;

    // Cached mid-play buffering percentage (mpv cache-buffering-state). Watch Party
    // observes this without adding another blocking getProperty() call to PlayerPage's
    // hot path. 100 means no active cache refill; lower values mean mpv is buffering.
    Q_PROPERTY(double cacheBufferingState READ cacheBufferingState NOTIFY cacheBufferingStateChanged)
    double cacheBufferingState() const;

    Q_PROPERTY(bool coreSeeking READ coreSeeking NOTIFY coreSeekingChanged)
    bool coreSeeking() const;

    // True while FFmpeg is turning captured frames into a GIF in the background.
    Q_PROPERTY(bool gifEncoding READ gifEncoding NOTIFY gifEncodingChanged)
    bool gifEncoding() const;

    Q_INVOKABLE void loadFile(const QString &file);
    // Same as loadFile, but first installs `headers` as mpv's http-header-fields so a source that
    // requires a Referer/Origin actually plays. loadFile clears the field, so headers set here can
    // never leak into a later plain load. (Theatre House HTTP Source, slice 1.)
    Q_INVOKABLE void loadFileWithHeaders(const QString &url, const QVariantMap &headers);
    // Backend-neutral PlayerItem contract used by shared QML. Android supplies the
    // same surface without inheriting MpvQt.
    Q_INVOKABLE void loadSource(const QString &url);
    Q_INVOKABLE void loadSource(const QString &url, const QVariantMap &headers) override;
    Q_INVOKABLE void stopPlayback() override;
    Q_INVOKABLE void setHostLifecycleState(const QString &state) override;
    Q_INVOKABLE void setAudioFocusState(const QString &state) override;
    Q_INVOKABLE void releaseVideoSurface() override;
    Q_INVOKABLE void restoreVideoSurface() override;
    Q_INVOKABLE void applyPlaybackProfile();
    Q_INVOKABLE void refreshAudioOutput();
    Q_INVOKABLE QVariant playbackStat(const QString &name);
    Q_INVOKABLE void seekExact(double value);
    Q_INVOKABLE void seekStep(double delta);
    Q_INVOKABLE void frameStep();
    Q_INVOKABLE void frameBackStep();
    // Add an EXTERNAL subtitle (e.g. an online .srt/.vtt URL) and select it. mpv loads
    // http(s) URLs directly. `select` true makes it the active sub immediately.
    Q_INVOKABLE void addSubtitle(const QString &url, const QString &title = QString(),
                                 const QString &lang = QString(), bool select = true);
    Q_INVOKABLE void setSubOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void setAudioNormalization(const QString &mode);   // "off" | "light" | "full"
    Q_INVOKABLE QVariant mpvProperty(const QString &name);
    Q_INVOKABLE QString captureFrame(const QString &title = QString(), const QString &subtitle = QString());
    Q_INVOKABLE void revealCaptureFolder(const QString &path = QString());
    Q_INVOKABLE bool startGifRecording();
    // Kicks off the FFmpeg encode asynchronously; the result arrives via
    // gifSaved(path) / gifFailed() — never blocks the UI thread.
    Q_INVOKABLE void stopGifRecording(const QString &title = QString(), const QString &subtitle = QString());
    Q_INVOKABLE void abortGifRecording();

Q_SIGNALS:
    void mediaTitleChanged();
    void currentUrlChanged();
    void positionChanged();
    void durationChanged();
    void pauseChanged();
    void volumeChanged();
    void muteChanged();
    void speedChanged();
    void audioTrackChanged();
    void subtitleTrackChanged();
    void trackListChanged();
    void chaptersChanged();
    void audioDelayChanged();
    void subDelayChanged();
    void videoFillChanged();
    void cacheTimeChanged();
    void cacheBufferingStateChanged();
    void coreSeekingChanged();
    void gifEncodingChanged();
    void gifSaved(QString path);
    void gifFailed();

    void fileStarted();
    void fileLoaded();
    void endFile(QString reason);
    void playbackError(QString code, QString message);
    void videoReconfig();
    void decodedDimensionsChanged();

private:
    void setupConnections();
    // Shared body of loadFile / loadFileWithHeaders: update currentUrl, emit, issue `loadfile`.
    void issueLoadFile(const QString &file);
    void onPropertyChanged(const QString &property, const QVariant &value);

    // Latest values of the properties we already observe, so the QML-facing getters can answer from
    // memory instead of making a blocking cross-thread call into the mpv core. position/duration/
    // pause/volume/speed are all observed (see the observeProperty block in the constructor), so
    // onPropertyChanged keeps these current; the getters no longer need to ask. `mpv.position` alone
    // is referenced 27 times in PlayerPage.qml, and every one of those bindings used to take the mpv
    // core lock on the GUI thread each time it re-evaluated. (2026-07-30)
    double m_cachedPosition = 0.0;
    double m_cachedDuration = 0.0;
    bool   m_cachedPause = false;
    int    m_cachedVolume = 100;
    double m_cachedSpeed = 1.0;
    // Throttles positionChanged only — the cached value above is ALWAYS current. A seek (a jump, not
    // a tick) still emits immediately so the bar never lags a scrub.
    QElapsedTimer m_positionEmitClock;
    void onAsyncReply(const QVariant &data, mpv_event event);
    QString formatTime(const double time) const;
    QVariantList tracksForType(const QString &type) const;
    QString stringifyId(const QVariant &value) const;
    QString captureBaseName(const QString &title, const QString &subtitle) const;
    QString captureDirectory() const;
    QString sanitizeCapturePart(const QString &value) const;
    void gifCaptureFrame();
    QString gifOutputDirectory() const;
    void cleanGifTemp();

public:
    static QString findFfmpeg();   // shared with SeekThumbnailer (exe dir -> tools/ -> PATH)

private:

    double m_position{0.0};
    QString m_formattedPosition;
    double m_duration{0.0};
    QString m_formattedDuration;
    QUrl m_currentUrl;
    int m_decodedWidth{0};
    int m_decodedHeight{0};
    QVariantList m_trackList;
    QVariantList m_chapters;
    double m_cacheTime = 0.0;
    double m_cacheBufferingState = 100.0;
    bool m_coreSeeking = false;
    QTimer m_gifTimer;
    QString m_gifTempDir;
    int m_gifFrame{0};
    qint64 m_gifStartedAt{0};
    bool m_gifRecording{false};
    bool m_gifEncoding{false};
    QProcess *m_gifEncodeProc = nullptr;
    QString m_gifEncodeOutPath;
};

#endif // COLOSSEUM_MPVITEM_H
