#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

// Public YouTube watch URLs only. Stream addresses are transient; browser
// cookies, credentials and arbitrary extractor commands are never accepted.
class FeriaYouTubeSource : public QObject {
    Q_OBJECT
public:
    explicit FeriaYouTubeSource(QObject *parent=nullptr);
    ~FeriaYouTubeSource() override;
    static QUrl watchUrl(const QUrl &url);
    Q_INVOKABLE QUrl canonicalWatchUrl(const QUrl &url) const { return watchUrl(url); }
    static QVariantMap candidate(const QByteArray &json, const QUrl &watch);
    Q_INVOKABLE bool resolve(const QUrl &url,const QString &token);
    Q_INVOKABLE void cancel();
signals:
    void resolved(const QString &token,const QVariantMap &source);
    void failed(const QString &token,const QString &reason);
private:
    QPointer<QProcess> m_process;
    QTimer m_deadline;
    QByteArray m_output;
    QString m_token;
    QUrl m_watch;
};
