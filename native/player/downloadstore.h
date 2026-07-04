#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantMap>

class QFile;
class QNetworkReply;

class DownloadStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QString defaultDownloadDir READ defaultDownloadDir NOTIFY changed)

public:
    explicit DownloadStore(QObject *parent = nullptr);
    ~DownloadStore() override;

    QVariantMap status() const { return m_status; }
    QString defaultDownloadDir() const { return m_defaultDownloadDir; }

    Q_INVOKABLE void startDownload(const QVariantMap &request);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void revealDownload();
    Q_INVOKABLE void resetDownload();

signals:
    void changed();

private:
    QString buildDefaultDownloadDir() const;
    QString buildOutputPath(const QVariantMap &request) const;
    QString sanitizeFilePart(const QString &value) const;
    QString extensionFromUrl(const QString &url) const;
    void setStatus(const QVariantMap &status);
    void failDownload(const QString &message);
    void cleanupActiveReply();

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_defaultDownloadDir;
    QString m_outputPath;
    QString m_partPath;
    QVariantMap m_status;
};
