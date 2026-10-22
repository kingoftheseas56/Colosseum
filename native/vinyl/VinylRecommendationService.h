#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

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
    Q_PROPERTY(QVariantMap artistResults READ artistResults NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList draftArtists READ draftArtists NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedArtists READ selectedArtists NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    explicit VinylRecommendationService(QObject *parent=nullptr);
    QObject* source() const{return m_source;}
    QObject* preferences() const{return m_preferences;}
    QString profileScope() const{return m_scope;}
    int profileGeneration() const{return m_generation;}
    bool profileActive() const{return m_active;}
    QVariantList suggestions() const{return m_suggestions;}
    QString onboardingState() const{return m_state;}
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
signals:
    void stateChanged();
    void resultsChanged();
private slots:
    void loadPreferences();
    void accept(const QString&,int,const QVariant&,const QString&);
private:
    QPointer<QObject> m_source,m_preferences;
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
};
