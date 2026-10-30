#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Anonymous Genius enrichment, projected onto the current BC1 lyric document.
// Playback/identity and persistent caching remain with their existing owners.
class VinylGeniusService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QVariantMap context READ context WRITE setContext NOTIFY contextChanged)
    Q_PROPERTY(QString documentId READ documentId WRITE setDocumentId NOTIFY documentIdChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics WRITE setLyrics NOTIFY lyricsChanged)
    Q_PROPERTY(QVariantMap notes READ notes NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap anchors READ anchors NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap groupEndLines READ groupEndLines NOTIFY dataChanged)
    Q_PROPERTY(QString status READ status NOTIFY dataChanged)
    Q_PROPERTY(QString error READ error NOTIFY dataChanged)
    Q_PROPERTY(QString geniusSongId READ geniusSongId NOTIFY dataChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY dataChanged)
    Q_PROPERTY(int matchedReferentCount READ matchedReferentCount NOTIFY dataChanged)
    Q_PROPERTY(int rejectedReferentCount READ rejectedReferentCount NOTIFY dataChanged)
public:
    explicit VinylGeniusService(QObject *parent=nullptr, QNetworkAccessManager *network=nullptr);
    ~VinylGeniusService() override;
    QObject *provider() const { return m_provider; }
    void setProvider(QObject *provider);
    QVariantMap context() const { return m_context; }
    void setContext(const QVariantMap &context);
    QString documentId() const { return m_documentId; }
    void setDocumentId(const QString &id);
    QVariantList lyrics() const { return m_lyrics; }
    void setLyrics(const QVariantList &lyrics);
    QVariantMap notes() const { return m_projection.value("notes").toMap(); }
    QVariantMap anchors() const { return m_projection.value("anchors").toMap(); }
    QVariantMap groupEndLines() const { return m_projection.value("groupEndLines").toMap(); }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString geniusSongId() const { return m_songId; }
    int annotationCount() const { return m_projection.value("annotationCount").toInt(); }
    int matchedReferentCount() const { return m_projection.value("matchedReferentCount").toInt(); }
    int rejectedReferentCount() const { return m_projection.value("rejectedReferentCount").toInt(); }
    Q_INVOKABLE void retry();

    static QString normalizedText(QString text);
    static QVariantList matchingSongs(const QVariantMap &context,const QVariantMap &search);
    static QVariantMap align(const QString &documentId,const QVariantList &lyrics,const QVariantList &referents);
signals:
    void providerChanged();
    void contextChanged();
    void documentIdChanged();
    void lyricsChanged();
    void dataChanged();
private:
    void cancel();
    void schedule(bool refresh=false);
    void begin();
    void fetch(const QString &path,std::function<void(QVariantMap)> completed);
    void fetchPage(int page);
    void publish();
    void fail(const QString &message,const QString &status="error");
    bool validContext() const;
    QPointer<QObject> m_provider;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QVariantMap m_context,m_projection;
    QVariantList m_lyrics,m_referents;
    QSet<QString> m_seen;
    QString m_documentId,m_songId,m_status="unavailable",m_error;
    quint64 m_generation=0;
    qint64 m_deferredUntil=0;
    bool m_scheduled=false,m_refresh=false;
};
