#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class LiveStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isLive READ isLive NOTIFY changed)
    Q_PROPERTY(QVariantMap activeChannel READ activeChannel NOTIFY changed)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY changed)
    Q_PROPERTY(QString group READ group NOTIFY changed)
    Q_PROPERTY(QString query READ query NOTIFY changed)
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY changed)
    Q_PROPERTY(QString defaultRecordingDir READ defaultRecordingDir NOTIFY changed)

public:
    explicit LiveStore(QObject *parent = nullptr);
    ~LiveStore() override;

    bool isLive() const { return m_isLive; }
    QVariantMap activeChannel() const { return m_activeChannel; }
    QVariantList channels() const { return m_channels; }
    QString group() const { return m_group; }
    QString query() const { return m_query; }
    QVariantList recordings() const { return m_recordings; }
    QString defaultRecordingDir() const { return m_defaultRecordingDir; }

    Q_INVOKABLE void setLiveChannel(const QVariantMap &channel);
    Q_INVOKABLE void addChannel(const QVariantMap &channel);
    Q_INVOKABLE void setGroup(const QString &group);
    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void switchChannel(const QVariantMap &channel);
    Q_INVOKABLE QString startRecording(const QVariantMap &request);
    Q_INVOKABLE void stopRecording(const QString &id);
    Q_INVOKABLE void revealRecording(const QString &id);

signals:
    void changed();
    void channelSwitchRequested(const QVariantMap &channel);

private:
    QVariantMap normalizeChannel(const QVariantMap &channel) const;
    int findChannel(const QString &id) const;
    int findRecording(const QString &id) const;
    QString buildDefaultRecordingDir() const;
    QString buildOutputPath(const QVariantMap &request, const QString &channelName, const QString &title) const;
    QString locateRecorder() const;
    QString sanitizeFilePart(const QString &value) const;
    void startProgressTimer();
    void updateRecordingProgress();
    void finishRecording(const QString &id, const QString &error = QString());
    void markRecordingError(const QString &id, const QString &error);

    bool m_isLive = false;
    QVariantMap m_activeChannel;
    QVariantList m_channels;
    QString m_group;
    QString m_query;
    QVariantList m_recordings;
    QString m_defaultRecordingDir;
    QHash<QString, QProcess *> m_recorders;
    QTimer m_recordingTimer;
};
