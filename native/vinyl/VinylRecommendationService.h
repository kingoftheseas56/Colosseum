#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>
#include <functional>
#include <memory>

// Profile-scoped onboarding/read model. Preferences and catalogue remain owners.
class VinylRecommendationService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY stateChanged)
    Q_PROPERTY(QObject* preferences READ preferences WRITE setPreferences NOTIFY stateChanged)
    Q_PROPERTY(QString profileScope READ profileScope WRITE setProfileScope NOTIFY stateChanged)
    Q_PROPERTY(int profileGeneration READ profileGeneration WRITE setProfileGeneration NOTIFY stateChanged)
    Q_PROPERTY(bool profileActive READ profileActive WRITE setProfileActive NOTIFY stateChanged)
    Q_PROPERTY(QVariantList suggestions READ suggestions WRITE setSuggestions NOTIFY resultsChanged)
    Q_PROPERTY(QString onboardingState READ onboardingState NOTIFY stateChanged)
    Q_PROPERTY(int minimumArtists READ minimumArtists CONSTANT)
    Q_PROPERTY(bool canEnterVinyl READ canEnterVinyl NOTIFY stateChanged)
    Q_PROPERTY(int draftArtistCount READ draftArtistCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap artistResults READ artistResults NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList draftArtists READ draftArtists NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedArtists READ selectedArtists NOTIFY stateChanged)
    Q_PROPERTY(int minimumArtistCount READ minimumArtistCount CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap homeCollections READ homeCollections WRITE setHomeCollections NOTIFY homeChanged)
    Q_PROPERTY(QVariantList homeSections READ homeSections NOTIFY homeChanged)
    Q_PROPERTY(QString homeStatus READ homeStatus NOTIFY homeChanged)
    Q_PROPERTY(QString homeError READ homeError NOTIFY homeChanged)
    Q_PROPERTY(int homeRevision READ homeRevision NOTIFY homeChanged)
    Q_PROPERTY(QVariantMap homeSnapshot READ homeSnapshot NOTIFY homeChanged)
    Q_PROPERTY(QVariantList homeCarouselItems READ homeCarouselItems NOTIFY homeChanged)
    Q_PROPERTY(QVariantList homeListeningCards READ homeListeningCards NOTIFY listeningChanged)
    Q_PROPERTY(QVariantList homeQuickPicks READ homeQuickPicks NOTIFY listeningChanged)
    Q_PROPERTY(QVariantMap homeMixView READ homeMixView NOTIFY listeningChanged)
    Q_PROPERTY(bool homeRadioActive READ homeRadioActive NOTIFY listeningChanged)
    Q_PROPERTY(QString homeRadioState READ homeRadioState NOTIFY listeningChanged)
    Q_PROPERTY(QVariantMap homeRadioContext READ homeRadioContext NOTIFY listeningChanged)
    Q_PROPERTY(QObject* activityStore READ activityStore WRITE setActivityStore NOTIFY stateChanged)
    Q_PROPERTY(QObject* historyStore READ historyStore WRITE setHistoryStore NOTIFY stateChanged)
    Q_PROPERTY(bool keepHistory READ keepHistory WRITE setKeepHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap historySnapshot READ historySnapshot NOTIFY homeChanged)
public:
    static constexpr int MinimumArtistCount = 5;
    int minimumArtistCount() const { return MinimumArtistCount; }
    explicit VinylRecommendationService(QObject *parent=nullptr,std::function<qint64()> clock={});
    ~VinylRecommendationService() override;
    QObject* source() const{return m_source;}
    QObject* preferences() const{return m_preferences;}
    QString profileScope() const{return m_scope;}
    int profileGeneration() const{return m_generation;}
    bool profileActive() const{return m_active;}
    QVariantList suggestions() const{return m_suggestions;}
    QString onboardingState() const{return m_state;}
    int minimumArtists() const{return MinimumArtistCount;}
    bool canEnterVinyl() const;
    int draftArtistCount() const;
    QVariantMap artistResults() const{return m_results;}
    QVariantList draftArtists() const{return m_draft;}
    QVariantList selectedArtists() const{return m_saved;}
    QString error() const{return m_error;}
    void setSource(QObject*);
    void setPreferences(QObject*);
    void setProfileScope(const QString&);
    void setProfileGeneration(int);
    void setProfileActive(bool);
    void setSuggestions(const QVariantList&);
    Q_INVOKABLE void beginHomeVisit();
    Q_INVOKABLE void searchArtists(const QString&,const QVariantMap &cursor=QVariantMap());
    Q_INVOKABLE void toggleDraftArtist(const QVariantMap&);
    Q_INVOKABLE bool commitTaste(bool skip);
    Q_INVOKABLE void beginTasteEdit();
    Q_INVOKABLE void cancelTasteEdit();
    Q_INVOKABLE void cancelSearch();
    QVariantMap homeCollections() const;
    void setHomeCollections(const QVariantMap&);
    QVariantList homeSections() const;
    QString homeStatus() const;
    QString homeError() const;
    int homeRevision() const;
    QVariantMap homeSnapshot() const;
    QVariantList homeCarouselItems() const;
    QVariantList homeListeningCards() const;
    QVariantList homeQuickPicks() const;
    QVariantMap homeMixView() const;
    bool homeRadioActive() const;
    QString homeRadioState() const;
    QVariantMap homeRadioContext() const;
    Q_INVOKABLE void refreshHomeSelection();
    Q_INVOKABLE void openHomeMix(const QString &kind);
    Q_INVOKABLE void startHomeRadio(const QVariantMap &artist);
    Q_INVOKABLE void refillHomeRadio(const QStringList &queuedTracks = {});
    Q_INVOKABLE void stopHomeRadio();
    Q_INVOKABLE void restoreHomeRadio(const QVariantMap &context, const QStringList &queuedTracks);
    Q_INVOKABLE void refreshHome(bool force=false);
    QObject *activityStore() const{return m_activityStore;}
    QObject *historyStore() const{return m_historyStore;}
    bool keepHistory() const{return m_keepHistory;}
    void setActivityStore(QObject*);
    void setHistoryStore(QObject*);
    void setKeepHistory(bool);
    QVariantMap historySnapshot() const;
    Q_INVOKABLE bool setFeedback(const QVariantMap &ref,const QString &action,int expectedGeneration);
    Q_INVOKABLE bool recordVisibleItems(const QString &shelf,const QString &snapshot,const QVariantList &refs,int expectedGeneration);
signals:
    void stateChanged();
    void resultsChanged();
    void homeChanged();
    void listeningChanged();
    void homeRadioBatchReady(const QVariantList &songs, bool append);
private slots:
    void loadPreferences();
    void accept(const QString&,int,const QVariant&,const QString&);
    void historyInputsChanged();
private:
    QPointer<QObject> m_source,m_preferences;
    QPointer<QObject> m_activityStore,m_historyStore;
    bool m_keepHistory=true;
    QString m_scope="local",m_state="unanswered",m_error,m_query;
    int m_generation=0,m_request=-1;
    bool m_active=true,m_committing=false;
    QTimer m_debounce;
    QVariantList m_saved,m_draft,m_suggestions;
    QVariantMap m_results,m_cursor;
    void invalidate();
    void dispatch();
    static QVariantMap artist(const QVariantMap&);
    static QVariantList uniqueArtists(const QVariantList&);
    static int uniqueTasteArtistCount(const QVariantList&);
    struct HomeState;
    std::shared_ptr<HomeState> m_home;
    void initializeHome(std::function<qint64()>);
    void cancelHome(bool clear);
    void clearHomeProfile();
    void homeSeedsChanged();
    void visitHome();
    void rebuildHome();
    void selectHomeCarousel(bool refresh=false);
    void pumpHome();
    bool acceptHome(const QString&,int,const QVariant&,const QString&);
    bool storeExposure(const QString &snapshot);
};
