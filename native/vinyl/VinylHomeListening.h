#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QQueue>
#include <QSet>
#include <QMap>
#include <functional>

// Transient recommendation-service work; the existing session and stores own playback and saves.
class VinylHomeListening final : public QObject {
public:
    using Changed = std::function<void()>;
    using Batch = std::function<void(const QVariantList &, bool)>;
    VinylHomeListening(QObject *parent, Changed changed, Batch radioBatch);
    ~VinylHomeListening() override;
    void setInputs(QObject *source, const QString &scope, int generation,
                   const QVariantList &seeds, const QVariantList &sections, const QString &selection,
                   const QStringList &excludedArtistProviderIds = {});
    void clear();
    void openMix(const QString &kind);
    void startRadio(const QVariantMap &artist);
    void restoreRadio(const QVariantMap &context, const QStringList &queuedTracks);
    void refillRadio(const QStringList &queuedTracks = {});
    void stopRadio();
    bool accept(const QString &lane, int id, const QVariant &data, const QString &failure);
    QVariantList cards() const;
    QVariantList quickPicks() const;
    QVariantMap view() const { return m_view; }
    QVariantMap radioContext() const;
    bool radioActive() const { return m_radioActive; }
    QString radioState() const { return m_radioState; }
    static QString trackKey(const QVariantMap &track);
private:
    struct Job { QString role, mix, operation, value; QVariantMap seed, context; };
    struct Active { int id = -1; Job job; };
    QPointer<QObject> m_source;
    QString m_scope, m_selection, m_openMix;
    int m_generation = -1;
    QVariantList m_seeds, m_sections;
    QMap<QString, QVariantList> m_songs;
    QMap<QString, QString> m_errors;
    QSet<QString> m_started;
    QVariantMap m_view, m_radioSeed;
    bool m_radioActive = false, m_radioAppend = false;
    QString m_radioState = "idle";
    QVariantList m_radioAlbums, m_radioRelated, m_radioBatch;
    QSet<QString> m_radioSeen, m_radioAlbumSeen, m_radioArtistSeen;
    QSet<QString> m_excludedArtistProviderIds;
    QQueue<Job> m_waiting;
    QMap<QString, Active> m_active;
    QTimer m_deadline;
    Job m_failedRadioJob;
    bool m_hasFailedRadioJob = false;
    Changed m_changed;
    Batch m_batch;
    void cancel(const QString &prefix = {});
    void pump();
    void enqueue(const Job &job);
    void prepareMix(const QString &kind);
    void addAlbumJobs(const QVariantList &cards, const QString &mix, int maximum);
    void finishMixes();
    void updateView();
    void continueRadio();
    bool hasJobs(const QString &role, const QString &mix = {}) const;
    QVariantList albumSongs(const QVariantMap &album, const QVariantMap &context,
                            int maximum, const QString &salt) const;
    bool hasExcludedArtist(const QVariantMap &artists) const;
    bool hasArtistIdentity(const QVariantMap &artists) const;
    static QString albumKey(const QVariantMap &card);
    static QVariantMap artistRef(const QVariantMap &card);
};
