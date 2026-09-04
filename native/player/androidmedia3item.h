#ifndef COLOSSEUM_ANDROIDMEDIA3ITEM_H
#define COLOSSEUM_ANDROIDMEDIA3ITEM_H

#include <QtCore/qglobal.h>

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QQuickItem>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "androidmedia3state.h"
#include "playerbackendcontract.h"

class AndroidMedia3Item final : public QQuickItem, public PlayerBackendContract
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString formattedPosition READ formattedPosition NOTIFY positionChanged)
    Q_PROPERTY(QString formattedDuration READ formattedDuration NOTIFY durationChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool mute READ mute WRITE setMute NOTIFY muteChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString audioTrack READ audioTrack WRITE setAudioTrack NOTIFY audioTrackChanged)
    Q_PROPERTY(QString subtitleTrack READ subtitleTrack WRITE setSubtitleTrack NOTIFY subtitleTrackChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY trackListChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY trackListChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(QVariantList subtitleCues READ subtitleCues NOTIFY subtitleCuesChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(double subDelay READ subDelay WRITE setSubDelay NOTIFY subDelayChanged)
    Q_PROPERTY(double panscan READ panscan WRITE setPanscan NOTIFY videoFillChanged)
    Q_PROPERTY(double videoZoom READ videoZoom WRITE setVideoZoom NOTIFY videoFillChanged)
    Q_PROPERTY(QString videoAspect READ videoAspect WRITE setVideoAspect NOTIFY videoFillChanged)
    Q_PROPERTY(QUrl currentUrl READ currentUrl NOTIFY currentUrlChanged)
    Q_PROPERTY(int decodedWidth READ decodedWidth NOTIFY decodedDimensionsChanged)
    Q_PROPERTY(int decodedHeight READ decodedHeight NOTIFY decodedDimensionsChanged)
    Q_PROPERTY(double cacheTime READ cacheTime NOTIFY cacheTimeChanged)
    Q_PROPERTY(double cacheBufferingState READ cacheBufferingState NOTIFY cacheBufferingStateChanged)
    Q_PROPERTY(bool coreSeeking READ coreSeeking NOTIFY coreSeekingChanged)
    Q_PROPERTY(bool gifEncoding READ gifEncoding NOTIFY gifEncodingChanged)

public:
    explicit AndroidMedia3Item(QQuickItem *parent = nullptr);
    ~AndroidMedia3Item() override;

    QVariantMap capabilities() const override;
    QString mediaTitle() const;
    double position() const;
    void setPosition(double value);
    double duration() const;
    QString formattedPosition() const;
    QString formattedDuration() const;
    bool pause() const;
    void setPause(bool value);
    int volume() const;
    void setVolume(int value);
    bool mute() const;
    void setMute(bool value);
    double speed() const;
    void setSpeed(double value);
    QString audioTrack() const;
    void setAudioTrack(const QString &value);
    QString subtitleTrack() const;
    void setSubtitleTrack(const QString &value);
    QVariantList audioTracks() const;
    QVariantList subtitleTracks() const;
    QVariantList chapters() const;
    QVariantList subtitleCues() const;
    double audioDelay() const;
    void setAudioDelay(double value);
    double subDelay() const;
    void setSubDelay(double value);
    double panscan() const;
    void setPanscan(double value);
    double videoZoom() const;
    void setVideoZoom(double value);
    QString videoAspect() const;
    void setVideoAspect(const QString &value);
    QUrl currentUrl() const;
    int decodedWidth() const;
    int decodedHeight() const;
    double cacheTime() const;
    double cacheBufferingState() const;
    bool coreSeeking() const;
    bool gifEncoding() const;

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
    Q_INVOKABLE void addSubtitle(const QString &url, const QString &title = QString(),
                                 const QString &lang = QString(), bool select = true);
    Q_INVOKABLE void setSubOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void setAudioNormalization(const QString &mode);
    Q_INVOKABLE QString captureFrame(const QString &title = QString(),
                                      const QString &subtitle = QString());
    Q_INVOKABLE void revealCaptureFolder(const QString &path = QString());
    Q_INVOKABLE bool startGifRecording();
    Q_INVOKABLE void stopGifRecording(const QString &title = QString(),
                                       const QString &subtitle = QString());
    Q_INVOKABLE void abortGifRecording();

    void handlePlaybackSnapshot(quint64 generation, qint64 positionMs, qint64 durationMs,
                                qint64 bufferedPositionMs, double bufferedPercentage,
                                bool paused, double requestedVolume, bool muted, double speed);
    void handleReady(quint64 generation, qint64 durationMs, qint64 positionMs, bool playWhenReady);
    void handleEnded(quint64 generation);
    void handleError(quint64 generation, const QString &family,
                     const QString &code, const QString &message);
    void handleTimeline(quint64 generation, qint64 durationMs, bool seekable, bool live);
    void handleVideoSize(quint64 generation, int width, int height, double pixelRatio);
    void handleFirstFrame(quint64 generation);
    void handleSeekDiscontinuity(quint64 generation, qint64 oldPositionMs, qint64 newPositionMs);
    void handleTracks(quint64 generation, const QString &json);
    void handleChapters(quint64 generation, const QString &json);
    void handleMetadata(quint64 generation, const QString &json);
    void handleCues(quint64 generation, const QString &json);

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
    void subtitleCuesChanged();
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

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    void publishSnapshotChanges(const Colosseum::Player::AndroidMedia3Snapshot &before);
    void callHost(const char *method);
    void callHostSeek(qint64 positionMs);
    void callHostTrack(const char *method, const QString &trackId);
    void updateControlSnapshot(int volume, bool muted, double speed);
    QString formatTime(double seconds) const;
    jlong nativeHandle() const;

    Colosseum::Player::AndroidMedia3State m_state;
    QJniObject m_host;
    QString m_mediaTitle;
    QString m_audioTrack;
    QString m_subtitleTrack;
    double m_audioDelay = 0.0;
    double m_subDelay = 0.0;
    double m_panscan = 0.0;
    double m_videoZoom = 0.0;
    QString m_videoAspect = QStringLiteral("auto");
    QSize m_videoSize;
    bool m_fileLoadedEmitted = false;
    bool m_surfaceReleased = false;
    quint64 m_restoreLifecycleEpoch = 0;
};

#endif // Q_OS_ANDROID

#endif // COLOSSEUM_ANDROIDMEDIA3ITEM_H
