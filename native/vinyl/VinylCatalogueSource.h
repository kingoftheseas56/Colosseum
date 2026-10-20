#pragma once
#include "VinylLocalSource.h"
#include "VinylProvider.h"
#include "backend/artist-live-prototypes-20261015/bridge/ArtistServiceBridge.h"
#include <QPointer>
#include <QNetworkAccessManager>
#include <QSet>
#include "backend/artist-live-prototypes-20261015/artwork/ArtistArtworkResolver.h"

class QNetworkReply;

class CollectionStore;
// Catalogue projection/transport only. Local files, personal state and playback
// remain owned by VaultIdentity, Collection and VinylBackendSession.
class VinylCatalogueSource final : public QObject,
    private vinyl::artist::IArtistReadPort, private vinyl::artist::ISavedArtistPort,
    private vinyl::artist::IPlaybackCommandPort {
    Q_OBJECT
    Q_PROPERTY(int cataloguePageVersion READ cataloguePageVersion CONSTANT)
    Q_PROPERTY(QVariantMap artist READ artist NOTIFY artistChanged)
    Q_PROPERTY(QVariantList artistAlbums READ artistAlbums NOTIFY artistChanged)
    Q_PROPERTY(QVariantList artistSingles READ artistSingles NOTIFY artistChanged)
    Q_PROPERTY(QVariantMap artistMedia READ artistMedia NOTIFY artistChanged)
    Q_PROPERTY(QVariantMap artistIdentity READ artistIdentity NOTIFY artistChanged)
    Q_PROPERTY(QVariantMap artistArtwork READ artistArtwork NOTIFY artistChanged)
    Q_PROPERTY(bool artistBusy READ artistBusy NOTIFY artistChanged)
    Q_PROPERTY(QString artistError READ artistError NOTIFY artistChanged)
    Q_PROPERTY(bool cached READ cached NOTIFY artistChanged)
    Q_PROPERTY(QObject* canonicalArtistService READ canonicalArtistService CONSTANT)
public:
    VinylCatalogueSource(VinylLocalSource *,VinylProvider *,CollectionStore *,QObject *parent=nullptr);
    int cataloguePageVersion() const {return 1;}
    Q_INVOKABLE int request(const QString &,const QString &,const QString &);
    Q_INVOKABLE int request(const QString &,const QString &,const QString &,const QVariantMap &);
    Q_INVOKABLE void cancel(const QString &);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE int startImport(const QStringList &paths){return m_local->startImport(paths);}
    Q_INVOKABLE QVariantMap importFiles(const QStringList &paths){return m_local->importFiles(paths);}
    Q_INVOKABLE void cancelImport(){m_local->cancelImport();}
    Q_INVOKABLE bool decideImportIdentity(const QString &relationship,const QString &choice){return m_local->decideImportIdentity(relationship,choice);}
    Q_INVOKABLE void setAlbums(const QVariantList &);
    Q_INVOKABLE void setCollectionStore(QObject *);
    Q_INVOKABLE void openArtist(const QVariantMap &);
    Q_INVOKABLE void leaveArtist();
    Q_INVOKABLE void retryArtist();
    void setArtworkNetworkManager(QNetworkAccessManager *manager){m_artworkNam=manager;}
    QVariantMap artistArtwork() const{return m_artwork;}
    QVariantMap artist() const{return m_artist;}
    QVariantList artistAlbums() const{return m_artistAlbums;}
    QVariantList artistSingles() const{return m_artistSingles;}
    QVariantMap artistMedia() const{return m_artistMedia;}
    QVariantMap artistIdentity() const{return m_identity;}
    bool artistBusy() const{return !m_artistRequests.isEmpty()||!m_artworkReplies.isEmpty();}
    QString artistError() const{return m_artistError;}
    bool cached() const{return m_cached;}
    QObject *canonicalArtistService(){return &m_bridge;}
signals:
    void completed(const QString &,int,const QVariant &,const QString &);
    void importCompleted(int,const QVariant &);
    void importProgress(int,int,int,const QString &);
    void profileLeaving();void profileEntered();void checkpointRequested();void shuttingDown();
    void artistChanged();
private:
    struct Pending {int id;int transportId;bool local;QString operation,value;};
    VinylLocalSource *m_local;VinylProvider *m_provider;QPointer<CollectionStore> m_collection;
    int m_serial=0;QHash<QString,Pending> m_pending;
    QHash<QString,QVariantMap> m_albums;QHash<QString,QString> m_videos;
    QVariantMap m_artist,m_artistMedia,m_identity,m_artistIntent;
    QVariantList m_artistAlbums,m_artistSingles;
    QString m_artistError,m_channel;bool m_cached=false;
    QHash<QString,int> m_artistRequests;
    vinyl::artist::ArtistServiceBridge m_bridge;
    QPointer<QNetworkAccessManager> m_artworkNam;
    QSet<QNetworkReply*> m_artworkReplies;
    QVariantMap m_artwork;
    vinyl::artwork::Request m_artworkRequest;
    quint64 m_artworkGeneration=0;
    void loadArtwork(const QVariantMap &);
    void publishArtwork();
    void accept(const QString &,int,const QVariant &,const QString &,bool);
    void artistRequest(const QString &,const QString &,const QString &,const QVariantMap &payload={});
    void acceptArtist(const QString &,const QVariant &,const QString &);
    void requestIdentity();
    QVariantMap remember(QVariantMap);
    void request(const vinyl::artist::ReadRequest &,std::function<void(vinyl::artist::ReadReply)>) override;
    void cancel(quint64) override {}
    vinyl::artist::SavedReply read(const QString &,const QString &) override;
    vinyl::artist::SavedReply write(const QString &,const QString &,bool) override;
    vinyl::artist::CommandReply selectRecording(const vinyl::artist::ExactSelection &) override;
    vinyl::artist::CommandReply selectRelease(const vinyl::artist::ExactSelection &) override;
};
