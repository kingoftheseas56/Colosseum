#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include "TimedTextCore.h"

// Projection of BC1 through the existing provider transport; owns no playback or cache.
class VinylLyricsService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QVariantMap context READ context WRITE setContext NOTIFY contextChanged)
    Q_PROPERTY(QVariant recordingTimeOffsetMs READ recordingTimeOffsetMs WRITE setRecordingTimeOffsetMs NOTIFY documentChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY documentChanged)
    Q_PROPERTY(QString documentId READ documentId NOTIFY documentChanged)
    Q_PROPERTY(QString status READ status NOTIFY documentChanged)
    Q_PROPERTY(QString error READ error NOTIFY documentChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY documentChanged)
    Q_PROPERTY(bool timed READ timed NOTIFY documentChanged)
    Q_PROPERTY(int currentLineIndex READ currentLineIndex NOTIFY currentLineChanged)
public:
    explicit VinylLyricsService(QObject *parent=nullptr) : QObject(parent) {}
    ~VinylLyricsService() override;
    QObject *provider() const { return m_provider; }
    void setProvider(QObject *provider);
    QVariantMap context() const { return m_context; }
    void setContext(const QVariantMap &context);
    QVariant recordingTimeOffsetMs() const;
    void setRecordingTimeOffsetMs(const QVariant &offset);
    double position() const { return m_position; }
    void setPosition(double seconds);
    QVariantList lines() const;
    QString documentId() const;
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString sourceName() const { return m_document?m_document->provenance().providerId().value():QString(); }
    bool timed() const;
    int currentLineIndex() const;
    Q_INVOKABLE QVariant seekTime(int index) const;
    Q_INVOKABLE void retry();
    static vinyl::TimedTextResult<vinyl::TimedTextDocument> fromLrclib(
        const QVariantMap &context,const QVariantMap &record);
signals:
    void providerChanged();
    void contextChanged();
    void documentChanged();
    void positionChanged();
    void currentLineChanged();
private slots:
    void completed(const QString &lane,int request,const QVariant &data,const QString &error);
private:
    void cancel();
    void request(bool refresh=false);
    QPointer<QObject> m_provider;
    QVariantMap m_context;
    std::optional<vinyl::TimedTextDocument> m_document;
    std::optional<qint64> m_recordingOffset;
    QString m_status="unavailable",m_error;
    int m_request=-1;
    double m_position=0;
};
