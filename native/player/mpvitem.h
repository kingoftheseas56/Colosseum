// MpvItem — the playable mpv surface, a QQuickItem subclass of MpvQt's MpvAbstractItem.
// Lifted 1:1 from KDE mpvqt's video-player example (proven to play real video on our
// Qt 6.11 / MinGW), with one change: QML_ELEMENT is removed. Colosseum loads its QML
// live from disk (no qt_add_qml_module), so the type is registered by hand in main.cpp
//   qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
// and reached from QML with `import Colosseum.Player`.
#ifndef COLOSSEUM_MPVITEM_H
#define COLOSSEUM_MPVITEM_H

#include <MpvAbstractItem>
#include <QVariantList>

class MpvItem : public MpvAbstractItem
{
    Q_OBJECT
public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() = default;

    enum class AsyncIds {
        None,
        SetVolume,
        GetVolume,
        ExpandText,
    };
    Q_ENUM(AsyncIds)

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

    Q_INVOKABLE void loadFile(const QString &file);
    Q_INVOKABLE void seekExact(double value);
    Q_INVOKABLE void seekStep(double delta);
    // Add an EXTERNAL subtitle (e.g. an online .srt/.vtt URL) and select it. mpv loads
    // http(s) URLs directly. `select` true makes it the active sub immediately.
    Q_INVOKABLE void addSubtitle(const QString &url, const QString &title = QString(),
                                 const QString &lang = QString(), bool select = true);
    Q_INVOKABLE void setSubOption(const QString &key, const QVariant &value);

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
    void audioDelayChanged();
    void subDelayChanged();
    void videoFillChanged();

    void fileStarted();
    void fileLoaded();
    void endFile(QString reason);
    void videoReconfig();

private:
    void setupConnections();
    void onPropertyChanged(const QString &property, const QVariant &value);
    void onAsyncReply(const QVariant &data, mpv_event event);
    QString formatTime(const double time);
    QVariantList tracksForType(const QString &type) const;
    QString stringifyId(const QVariant &value) const;

    double m_position{0.0};
    QString m_formattedPosition;
    double m_duration{0.0};
    QString m_formattedDuration;
    QUrl m_currentUrl;
    QVariantList m_trackList;
};

#endif // COLOSSEUM_MPVITEM_H
