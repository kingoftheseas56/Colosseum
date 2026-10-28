#pragma once
#include <QObject>
#include <QJsonValue>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <memory>
class QNetworkAccessManager;

// Shared immutable data transport. Catalogue/result truth stays in existing owners.
class StaticCatalogueResolver final : public QObject {
    Q_OBJECT
  public:
    enum class Status { Prepared, NotPrepared, Failure };
    struct Outcome {
        Status status = Status::Failure;
        QJsonValue result;
        QString publication, sourceRevision, error;
        QVariantMap metrics;
    };
    using Callback = std::function<void(Outcome)>;
    static StaticCatalogueResolver *shared(QNetworkAccessManager *, const QUrl &endpoint,
                                           const QString &cacheDirectory, QObject *offlineOwner);
    ~StaticCatalogueResolver() override;
    quint64 resolve(const QString &dataset, const QString &method, const QVariantList &args,
                    QObject *context, Callback);
    void cancel(quint64);
    void refresh(); // Next request conditionally revalidates current.json.
    bool needsRefresh() const;
    bool hasLastGood() const;
    QString publication() const;
  signals:
    void publicationChanged(QString revision);
    void measured(QString dataset, QString method, QVariantMap metrics);

  private:
    StaticCatalogueResolver(QNetworkAccessManager *, QUrl, QString, QObject *);
    struct Impl;
    std::unique_ptr<Impl> d;
};
