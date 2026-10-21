#pragma once

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <QVector>
class MpvItem;
namespace vinyl {
class QueueOccurrence;
class PlaybackWorkToken;
class PlaybackSeekCommand;
class PlaybackLoadInput;
struct PlaybackRequestHeader;
}

// One transient command/event owner. Persistence and identity remain host-owned.
class VinylBackendSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QObject *surface READ surface WRITE setSurface NOTIFY surfaceChanged)
    Q_PROPERTY(QObject *collectionStore READ collectionStore WRITE setCollectionStore NOTIFY collectionStoreChanged)
    Q_PROPERTY(QObject *preferences MEMBER m_preferences)
    Q_PROPERTY(QString profileScope READ profileScope WRITE setProfileScope NOTIFY profileScopeChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY queueChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY queueChanged)
    Q_PROPERTY(QObject *player READ player NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool seekPending READ seekPending NOTIFY stateChanged)
    Q_PROPERTY(bool shuffled READ shuffled NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY stateChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(QString sourceState READ sourceState NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString qualityLabel READ qualityLabel NOTIFY stateChanged)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY stateChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY stateChanged)
    Q_PROPERTY(QString repeatMode READ repeatMode NOTIFY stateChanged)
public:
    explicit VinylBackendSession(QObject *parent=nullptr);
    ~VinylBackendSession() override;
    QObject *source() const { return m_source; }
    void setSource(QObject *source);
    QObject *surface() const { return m_surface; }
    QObject *collectionStore() const { return m_collection; }
    void setCollectionStore(QObject *store) { if(m_collection==store)return;m_collection=store;emit collectionStoreChanged(); }
    void setSurface(QObject *surface) { if(m_surface==surface)return;m_surface=surface;emit surfaceChanged(); }
    QString profileScope() const { return m_profile; }
    void setProfileScope(const QString &profile);
    QVariantList rows() const;
    int activeIndex() const;
    QObject *player() const;
    bool paused() const { return m_paused; }
    bool loaded() const { return m_loaded; }
    bool seekPending() const;
    bool shuffled() const { return m_shuffleEnabled; }
    double position() const;
    double duration() const { return m_duration; }
    QString sourceState() const { return m_state; }
    QString error() const { return m_error; }
    QString qualityLabel() const { return m_quality; }
    QString sourceKind() const { return m_sourceKind; }
    QString fallbackReason() const { return m_fallbackReason; }
    QString repeatMode() const { return m_repeat; }

    Q_INVOKABLE bool replaceRows(const QVariantList &rows,int index,bool shuffle);
    Q_INVOKABLE bool appendRows(const QVariantList &rows,bool playNext);
    Q_INVOKABLE void playQueue(int index);
    Q_INVOKABLE void removeQueue(int index);
    Q_INVOKABLE void moveQueue(int from,int to);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void setShuffle(bool enabled);
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void next(int delta);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void clearProfile();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE QVariantMap checkpoint() const;
    Q_INVOKABLE bool restoreCheckpoint(const QVariantMap &checkpoint);
    Q_INVOKABLE bool restoreLegacy(const QVariantList &rows,int index,bool shuffle,
        const QStringList &originalOrder,const QString &repeat,double position,double duration);
    Q_INVOKABLE void beginListeningFacts();
    Q_INVOKABLE bool qualifyListeningSample(bool consuming);
    Q_INVOKABLE void endListeningFacts(bool natural);
    Q_INVOKABLE bool validateFeedbackSnapshot(const QString &snapshot) const;
    Q_INVOKABLE QVariantMap feedbackState() const;
    Q_INVOKABLE bool storeFeedbackSnapshot(const QString &snapshot);
    Q_INVOKABLE bool setSavedAlbum(const QVariantMap &entry,bool saved);
    Q_INVOKABLE QVariantMap personalRecords() const;
    Q_INVOKABLE QVariantMap mutatePersonal(const QString &operation,const QVariantMap &arguments);
    Q_INVOKABLE QString personalSongId(const QVariantMap &row) const;
    Q_INVOKABLE bool personalRowAvailable(const QVariantMap &row) const;
signals:
    void stateChanged();
    void pausedChanged();
    void queueChanged();
    void sourceChanged();
    void surfaceChanged();
    void collectionStoreChanged();
    void profileScopeChanged();
    void playbackRequested();
    void playbackEnding(bool natural);
    void discontinuity(double seconds);
private slots:
    void sourceCompleted(const QString &lane,int request,const QVariant &data,const QString &error);
private:
    std::optional<vinyl::PlaybackLoadInput> inputFor(const vinyl::QueueOccurrence &);
    void loadFile(const vinyl::PlaybackWorkToken &,const QString &,const QString &);
    void loadFileWithHeaders(const vinyl::PlaybackWorkToken &,const QString &,
        const QVector<vinyl::PlaybackRequestHeader> &,const QString &);
    void setPaused(const vinyl::PlaybackWorkToken &,bool);
    void seekExact(const vinyl::PlaybackSeekCommand &,double);
    void stop(const vinyl::PlaybackWorkToken &);
    void beginActive(double seconds=0,bool play=true,bool fallback=false);
    bool tryFallback(const QString &reason,std::optional<double> resume=std::nullopt);
    void retirePlayer();
    void cancelResolution();
    void fail(const QString &message);
    void naturalEnd(const vinyl::PlaybackWorkToken &token);
    void dispatchSeek(const vinyl::PlaybackSeekCommand &command);
    QVariantMap activeRow() const;
    bool validRow(const QVariantMap &row) const;
    std::optional<vinyl::QueueOccurrence> occurrence(const QVariantMap &row) const;
    static qint64 monotonicMs();
    void listeningPauseTransition(bool paused);
    void publishState();
    QPointer<QObject> m_source,m_surface;
    QPointer<QObject> m_collection,m_preferences;
    QPointer<MpvItem> m_player;
    QString m_profile,m_repeat="off",m_state="idle",m_error,m_quality="Local audio";
    QString m_sourceKind,m_fallbackReason;
    bool m_forceFallback=false,m_fallbackAttempted=false;
    qint64 m_lastProgressMs=0;
    double m_lastProgress=0;
    bool m_paused=true,m_loaded=false,m_shutdown=false,m_profileActive=false,m_shuffleEnabled=false;
    bool m_publishedPaused=true;
    double m_resume=0,m_duration=0;
    quint64 m_epoch=0;
    int m_request=-1;
    QString m_resolvingId;
    QHash<QString,QVariantMap> m_rows;
    struct Backend;
    std::unique_ptr<Backend> m_backend;
    bool m_seekAccepted=false;
    QTimer m_deadline;
};
