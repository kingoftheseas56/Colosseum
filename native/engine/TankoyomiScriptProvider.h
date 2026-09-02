#pragma once

#include <QObject>
#include <QJSEngine>
#include <QJSValue>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

class QNetworkAccessManager;

class TankoyomiScriptProvider final : public QObject
{
    Q_OBJECT
public:
    TankoyomiScriptProvider(QString providerId,
                            QString language,
                            QString resourcePath,
                            QStringList allowedHosts,
                            QObject *parent = nullptr);

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
                                 const QString &optionsJson);
    Q_INVOKABLE void jsResolve(const QString &token, const QString &json);
    Q_INVOKABLE void jsReject(const QString &token, const QString &message);

private:
    void invoke(const QString &method, const QString &token, const QJSValueList &args);
    void deliverFetch(const QString &callbackId, bool ok, const QString &payload);
    bool hostAllowed(const QString &host) const;

    QString m_providerId;
    QString m_language;
    QStringList allowedHosts;
    QString m_loadError;
    bool m_ready = false;
    QJSEngine m_engine;
    QJSValue m_provider;
    QJSValue m_context;
    QJSValue m_run;
    QJSValue m_deliverFetch;
    QNetworkAccessManager *m_nam = nullptr;
};
