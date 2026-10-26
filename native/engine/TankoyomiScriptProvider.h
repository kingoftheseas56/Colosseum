#pragma once

#include "MangaImageHostResolver.h"

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include <memory>
#include <QJSEngine>
#include <QJSValue>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

class QNetworkAccessManager;
class TankoyomiAddonStore;

class TankoyomiScriptProvider final : public QObject
{
    Q_OBJECT
public:
    TankoyomiScriptProvider(QString providerId,
                            QString language,
                            QString resourcePath,
                            QStringList allowedHosts,
                            QNetworkAccessManager *nam,
                            QObject *parent = nullptr);
    TankoyomiScriptProvider(QString providerId,
                            QString language,
                            QString resourcePath,
                            QStringList allowedHosts,
                            QNetworkAccessManager *nam,
                            MangaImageHostResolver::Lookup lookup,
                            QObject *parent = nullptr);
    TankoyomiScriptProvider(QString providerId,
                            QString language,
                            QUrl workerBase,
                            QNetworkAccessManager *nam,
                            QObject *parent = nullptr);
    // Fixture seam for the signed addon store and the provider's isolated fetch path.
    TankoyomiScriptProvider(QString providerId,
                            QString language,
                            TankoyomiAddonStore *addonStore,
                            QNetworkAccessManager *sourceNam,
                            MangaImageHostResolver::Lookup lookup,
                            QObject *parent = nullptr);
    ~TankoyomiScriptProvider() override;

    bool isReady() const { return m_ready; }
    QString loadError() const { return m_loadError; }
    QString providerId() const { return m_providerId; }
    QString language() const { return m_language; }

    void searchSeries(const QString &token, const QString &query);
    void getChapters(const QString &token, const QVariantMap &series);
    void getPages(const QString &token, const QVariantMap &chapter);

signals:
    void resolved(const QString &token, const QVariant &value);
    void failed(const QString &token, const QString &message);

public:
    // Internal QJSEngine bridge. These must be public in the QObject meta-surface
    // so newQObject(this) can invoke them from the capability-scoped JS wrapper.
    Q_INVOKABLE void jsFetchText(const QString &callbackId,
                                 const QString &url,
                                 const QString &optionsJson,
                                 const QString &operationToken);
    Q_INVOKABLE void jsResolve(const QString &token, const QString &json);
    Q_INVOKABLE void jsReject(const QString &token, const QString &message);
    Q_INVOKABLE QString jsDecryptCryptoJs(const QString &ciphertextBase64,
                                          const QString &saltHex,
                                          const QString &password);
    Q_INVOKABLE QString jsDecodeBase64(const QString &value);

private:
    struct Fetch;
    struct WorkerCall;
    struct ScriptOperation;
    struct PendingDeviceCall {
        QString method;
        QString token;
        QVariant value;
    };
    bool initializeScript(const QByteArray &script, const QString &filename);
    void queueDeviceCall(const QString &method, const QString &token, const QVariant &value);
    void ensureDeviceScriptLoaded();
    void drainDeviceCalls();
    void startScriptOperation(const QString &token);
    void finishScriptOperation(const QString &token, bool ok,
                               const QVariant &value, const QString &message);
    void timeoutScriptOperation(const QString &token);
    void invokeDeviceCall(const PendingDeviceCall &call);
    void invokeWorker(const QString &method, const QString &token, const QJsonObject &payload);
    void finishWorkerCall(const std::shared_ptr<WorkerCall> &call,
                          bool ok,
                          const QVariant &value,
                          const QString &message);
    void readWorkerResponse(const std::shared_ptr<WorkerCall> &call);
    void readFetchResponse(const std::shared_ptr<Fetch> &fetch);
    void issueFetch(const std::shared_ptr<Fetch> &fetch);
    void finishFetch(const std::shared_ptr<Fetch> &fetch, bool ok, const QString &payload);
    void invoke(const QString &method, const QString &token, const QJSValueList &args);
    void deliverFetch(const QString &callbackId, bool ok, const QString &payload);
    bool hostAllowed(const QString &host) const;

    QString m_providerId;
    QString m_language;
    QUrl m_workerBase;
    QStringList allowedHosts;
    QString m_loadError;
    bool m_ready = false;
    bool m_workerMode = false;
    quint64 m_nextWorkerCallId = 1;
    QJSEngine m_engine;
    QJSValue m_provider;
    QJSValue m_context;
    QJSValue m_run;
    QJSValue m_deliverFetch;
    QPointer<QNetworkAccessManager> m_nam;
    QHash<quint64, std::shared_ptr<WorkerCall>> m_workerCalls;
    QHash<QString, std::shared_ptr<Fetch>> m_fetches;
    QHash<QString, std::shared_ptr<ScriptOperation>> m_scriptOperations;
    QHash<QString, QString> m_fetchTokens;
    QVector<PendingDeviceCall> m_pendingDeviceCalls;
    QPointer<TankoyomiAddonStore> m_addonStore;
    QPointer<QNetworkAccessManager> m_ownedDeviceNam;
    bool m_deviceMode = false;
    bool m_deviceScriptLoading = false;
    bool m_deviceScriptLoaded = false;
    MangaImageHostResolver m_hostResolver;
    QHash<QString, QString> m_pins;
};
