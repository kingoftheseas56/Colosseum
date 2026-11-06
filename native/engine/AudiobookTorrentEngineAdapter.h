#pragma once
#include <QObject>
#include <QPointer>
#include <QJsonArray>
#include <QVector>
class TorrentEngine;
// Narrow acquisition seam, following the existing Comic/Manga queued adapters.
class IAudiobookTorrentEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool isRunning() const = 0;
    virtual void start() = 0;
    virtual bool hasTorrent(const QString&) const = 0;
    virtual QString addMagnet(const QString&, const QString&, bool metadataOnly) = 0;
    virtual QString actualSavePath(const QString&) const = 0;
    virtual QJsonArray files(const QString&) const = 0;
    virtual QJsonArray verifiedBytes(const QString&) const = 0;
    virtual void setPriorities(const QString&, const QVector<int>&) = 0;
    virtual void startPayload(const QString&) = 0;
    virtual bool requestRemoval(const QString& token,const QString& hash,const QString& expectedPath,bool erase) = 0;
signals:
    void unavailable();
    void removalSettled(const QString& token,bool success,int errorCode);
    void changed(const QString& hash); // advisory only: always read fresh live state
};
class AudiobookTorrentEngineAdapter final : public IAudiobookTorrentEngine {
public:
    explicit AudiobookTorrentEngineAdapter(TorrentEngine*, QObject* parent = nullptr);
    bool isRunning() const override;
    void start() override;
    bool hasTorrent(const QString&) const override;
    QString addMagnet(const QString&, const QString&, bool) override;
    QString actualSavePath(const QString&) const override;
    QJsonArray files(const QString&) const override;
    QJsonArray verifiedBytes(const QString&) const override;
    void setPriorities(const QString&, const QVector<int>&) override;
    void startPayload(const QString&) override;
    bool requestRemoval(const QString&,const QString&,const QString&,bool) override;
private:
    QPointer<TorrentEngine> m_engine;
};
