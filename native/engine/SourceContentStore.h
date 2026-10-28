#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>
class CatalogRemote;
class QNetworkAccessManager;

// Versioned presentation data. Remote data never executes QML or JavaScript.
// CatalogRemote owns transport and its existing bounded durable query cache.
class SourceContentStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit SourceContentStore(QObject* parent = nullptr);
    void configure(QNetworkAccessManager*, const QUrl& endpoint,
                   const QString& cachePath, const QString& starterPath);
    Q_INVOKABLE QVariantMap document(const QString& key) const;
    Q_INVOKABLE QString assetUrl(const QString& relative) const;
    Q_INVOKABLE void refresh(const QString& key = QString());
    int generation() const { return m_generation; }
    QString lastError() const { return m_error; }
signals:
    void generationChanged();
    void documentChanged(QString key);
    void lastErrorChanged();
private:
    bool install(const QJsonObject& bundle, bool commit = true);
    void synchronize();
    void setError(const QString&);
    CatalogRemote* m_remote;
    QUrl m_endpoint;
    QJsonObject m_manifest, m_documents;
    QString m_error;
    int m_generation = 0;
};
