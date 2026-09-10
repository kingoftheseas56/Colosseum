#include "TankoyomiScriptProvider.h"
#include "TankoyomiNetworkPolicy.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>

TankoyomiScriptProvider::TankoyomiScriptProvider(QString providerId,
                                                 QString language,
                                                 QString resourcePath,
                                                 QStringList hosts,
                                                 QNetworkAccessManager *nam,
                                                 QObject *parent)
    : QObject(parent),
      m_providerId(std::move(providerId)),
      m_language(std::move(language)),
      allowedHosts(std::move(hosts)),
      m_nam(nam ? nam : new QNetworkAccessManager(this))
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("Unable to open Tankoyomi provider %1").arg(resourcePath);
        return;
    }
    m_engine.globalObject().setProperty(QStringLiteral("__tkNative"), m_engine.newQObject(this));
    const QString bootstrap = QString::fromUtf8(R"JS(
this.__tankoyomiCallbacks = {};
this.__tankoyomiNextCallbackId = 1;

(function(root) {
  function makeTask(start) {
    return {
      __tankoyomiTask: true,
      start: start,
      then: function(onFulfilled, onRejected) {
        var parent = this;
        return makeTask(function(resolve, reject) {
          parent.start(
            function(value) {
              if (typeof onFulfilled !== 'function') {
                resolve(value);
                return;
              }
              try {
                adopt(onFulfilled(value), resolve, reject);
              } catch (error) {
                reject(error);
              }
            },
            function(error) {
              if (typeof onRejected !== 'function') {
                reject(error);
                return;
              }
              try {
                adopt(onRejected(error), resolve, reject);
              } catch (nextError) {
                reject(nextError);
              }
            }
          );
        });
      },
      catch: function(onRejected) {
        return this.then(null, onRejected);
      }
    };
  }

  function adopt(value, resolve, reject) {
    if (value && value.__tankoyomiTask === true && typeof value.start === 'function') {
      value.start(resolve, reject);
      return;
    }
    resolve(value);
  }

  function fetchText(url, options) {
    return makeTask(function(resolve, reject) {
      var callbackId = String(root.__tankoyomiNextCallbackId++);
      root.__tankoyomiCallbacks[callbackId] = { resolve: resolve, reject: reject };
      __tkNative.jsFetchText(callbackId, String(url), JSON.stringify(options || {}));
    });
  }

  root.__tankoyomiCtx = Object.freeze({
    fetchText: fetchText,
    fetchJson: function(url, options) {
      return fetchText(url, options).then(function(text) { return JSON.parse(text); });
    }
  });

  root.__tankoyomiDeliverFetch = function(callbackId, ok, payload) {
    var callbacks = root.__tankoyomiCallbacks[String(callbackId)];
    if (!callbacks) return;
    delete root.__tankoyomiCallbacks[String(callbackId)];
    if (ok) callbacks.resolve(String(payload));
    else callbacks.reject(new Error(String(payload)));
  };

  root.__tankoyomiRun = function(task, token) {
    adopt(
      task,
      function(value) { __tkNative.jsResolve(String(token), JSON.stringify(value)); },
      function(error) {
        var message = String(error);
        if (error && error.stack) message += '\n' + String(error.stack);
        __tkNative.jsReject(String(token), String(message));
      }
    );
  };
})(this);
)JS");
    QJSValue setup = m_engine.evaluate(bootstrap, QStringLiteral("tankoyomi-runtime.js"));
    if (setup.isError()) {
        m_loadError = setup.toString();
        return;
    }

    m_engine.globalObject().setProperty(QStringLiteral("TankoyomiProvider"),
                                        QJSValue(QJSValue::UndefinedValue));
    QStringList providerTrace;
    QJSValue evaluated = m_engine.evaluate(QString::fromUtf8(file.readAll()),
                                           resourcePath, 1, &providerTrace);
    if (evaluated.isError()) {
        m_loadError = QStringLiteral("%1 at %2:%3")
            .arg(evaluated.toString(), resourcePath)
            .arg(evaluated.property(QStringLiteral("lineNumber")).toInt());
        if (!providerTrace.isEmpty())
            m_loadError += QStringLiteral(" | ") + providerTrace.join(QStringLiteral(" | "));
        return;
    }

    m_provider = m_engine.globalObject().property(QStringLiteral("TankoyomiProvider"));
    m_context = m_engine.globalObject().property(QStringLiteral("__tankoyomiCtx"));
    m_run = m_engine.globalObject().property(QStringLiteral("__tankoyomiRun"));
    m_deliverFetch = m_engine.globalObject().property(QStringLiteral("__tankoyomiDeliverFetch"));
    if (!m_provider.isObject() || !m_context.isObject()
        || !m_run.isCallable() || !m_deliverFetch.isCallable()) {
        m_loadError = QStringLiteral("Tankoyomi provider %1 did not expose the provider contract")
                          .arg(m_providerId);
        return;
    }
    m_ready = true;
}

void TankoyomiScriptProvider::searchSeries(const QString &token, const QString &query)
{
    invoke(QStringLiteral("searchSeries"), token, {m_engine.toScriptValue(query)});
}

void TankoyomiScriptProvider::getChapters(const QString &token, const QVariantMap &series)
{
    invoke(QStringLiteral("getChapters"), token, {m_engine.toScriptValue(series)});
}

void TankoyomiScriptProvider::getPages(const QString &token, const QVariantMap &chapter)
{
    invoke(QStringLiteral("getPages"), token, {m_engine.toScriptValue(chapter)});
}

void TankoyomiScriptProvider::invoke(const QString &method,
                                     const QString &token,
                                     const QJSValueList &args)
{
    if (!m_ready) {
        emit failed(token, m_loadError.isEmpty()
                               ? QStringLiteral("Tankoyomi provider is unavailable")
                               : m_loadError);
        return;
    }

    QJSValue function = m_provider.property(method);
    if (!function.isCallable()) {
        emit failed(token, QStringLiteral("Tankoyomi provider %1 has no %2()")
                           .arg(m_providerId, method));
        return;
    }

    QJSValueList callArgs;
    callArgs.append(m_context);
    callArgs.append(args);
    QJSValue task = function.callWithInstance(m_provider, callArgs);
    if (task.isError()) {
        emit failed(token, task.toString());
        return;
    }

    QJSValue started = m_run.call({task, m_engine.toScriptValue(token)});
    if (started.isError())
        emit failed(token, started.toString());
}
struct TankoyomiScriptProvider::Fetch
{
    QString callbackId;
    QUrl logicalUrl;
    QNetworkRequest request;
    QString method;
    QByteArray body;
    int redirects = 0;
    bool settled = false;
    QPointer<QTimer> deadline;
    QPointer<QNetworkReply> reply;
};

TankoyomiScriptProvider::~TankoyomiScriptProvider()
{
    // The manager can outlive this provider. Cancel its work before QJSEngine
    // destruction, rather than leaving replies attached to a shared manager.
    const auto pending = m_fetches.values();
    m_fetches.clear();
    for (const auto &fetch : pending) {
        fetch->settled = true;
        if (fetch->deadline) fetch->deadline->stop();
        if (fetch->reply) {
            disconnect(fetch->reply, nullptr, this, nullptr);
            if (!fetch->reply->isFinished()) fetch->reply->abort();
            fetch->reply->deleteLater();
        }
    }
}

bool TankoyomiScriptProvider::hostAllowed(const QString &host) const
{
    return TankoyomiNetworkPolicy::metadataHostAllowed(host, allowedHosts);
}

void TankoyomiScriptProvider::jsFetchText(const QString &callbackId,
                                          const QString &url,
                                          const QString &optionsJson)
{
    const QUrl target(url);
    if (!TankoyomiNetworkPolicy::pageUrlAllowedBeforeDns(target, QStringLiteral("public-https"))
        || !hostAllowed(target.host())) {
        deliverFetch(callbackId, false,
                     QStringLiteral("Tankoyomi provider %1 cannot access host %2")
                         .arg(m_providerId, target.host()));
        return;
    }

    QJsonParseError optionsError;
    const QJsonDocument document = QJsonDocument::fromJson(optionsJson.toUtf8(), &optionsError);
    if (optionsError.error != QJsonParseError::NoError || !document.isObject()) {
        deliverFetch(callbackId, false, QStringLiteral("Invalid Tankoyomi request options"));
        return;
    }
    const QJsonObject options = document.object();
    const QString method = options.value(QStringLiteral("method"))
                               .toString(QStringLiteral("GET")).trimmed().toUpper();
    if (method != QLatin1String("GET") && method != QLatin1String("POST")) {
        deliverFetch(callbackId, false,
                     QStringLiteral("Tankoyomi provider %1 cannot use HTTP method %2")
                         .arg(m_providerId, method));
        return;
    }
    if (m_fetches.contains(callbackId)) {
        finishFetch(m_fetches.value(callbackId), false, QStringLiteral("Duplicate Tankoyomi callback"));
        return;
    }

    auto fetch = std::make_shared<Fetch>();
    fetch->callbackId = callbackId;
    fetch->logicalUrl = target;
    fetch->request = QNetworkRequest(target);
    fetch->method = method;
    fetch->body = options.value(QStringLiteral("body")).toString().toUtf8();
    const int defaultTimeoutMs = 15000;
    const int requestedTimeoutMs = options.value(QStringLiteral("timeoutMs")).toInt(defaultTimeoutMs);
    const int timeoutMs = qBound(1000, requestedTimeoutMs, 45000);
    fetch->request.setTransferTimeout(timeoutMs);
    fetch->request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::ManualRedirectPolicy);
    fetch->request.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/152.0 Safari/537.36");
    fetch->request.setRawHeader("Accept", "text/html,application/json;q=0.9,*/*;q=0.8");
    const QJsonObject headers = options.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const QByteArray value = it.value().toString().toUtf8();
        // A provider may describe HTTP metadata, not override the routed host.
        if (name.compare("Host", Qt::CaseInsensitive) == 0
            || name.contains('\r') || name.contains('\n')
            || value.contains('\r') || value.contains('\n'))
            continue;
        fetch->request.setRawHeader(name, value);
    }

    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    fetch->deadline = deadline;
    m_fetches.insert(callbackId, fetch);
    connect(deadline, &QTimer::timeout, this, [this, fetch] {
        if (fetch->settled) return;
        if (fetch->reply) fetch->reply->setProperty("tankoyomiHardDeadlineExpired", true);
        finishFetch(fetch, false, QStringLiteral("Tankoyomi request timeout"));
    });
    // One absolute budget covers the entire redirect chain, not each hop anew.
    deadline->start(timeoutMs);
    issueFetch(fetch);
}

void TankoyomiScriptProvider::issueFetch(const std::shared_ptr<Fetch> &fetch)
{
    if (fetch->settled) return;
    if (!m_nam) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi network manager is unavailable"));
        return;
    }
    fetch->request.setUrl(fetch->logicalUrl);
    QNetworkReply *reply = fetch->method == QLatin1String("POST")
        ? m_nam->post(fetch->request, fetch->body) : m_nam->get(fetch->request);
    fetch->reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, fetch, reply] {
        if (fetch->settled) return;
        fetch->reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (status >= 300 && status < 400) {
            if (fetch->redirects >= 5
                || !TankoyomiNetworkPolicy::redirectAllowed(fetch->logicalUrl, location, allowedHosts)) {
                finishFetch(fetch, false, QStringLiteral("Tankoyomi metadata redirect rejected"));
                return;
            }
            const QUrl next = fetch->logicalUrl.resolved(location);
            if (next.host() != fetch->logicalUrl.host() || next.port(443) != fetch->logicalUrl.port(443)) {
                fetch->request.setRawHeader("Authorization", QByteArray());
                fetch->request.setRawHeader("Cookie", QByteArray());
            }
            if ((status == 301 || status == 302 || status == 303)
                && fetch->method == QLatin1String("POST")) {
                fetch->method = QStringLiteral("GET");
                fetch->body.clear();
                fetch->request.setRawHeader("Content-Type", QByteArray());
                fetch->request.setRawHeader("Content-Length", QByteArray());
            }
            fetch->logicalUrl = next;
            ++fetch->redirects;
            issueFetch(fetch);
            return;
        }
        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            finishFetch(fetch, false,
                        status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(errorText) : errorText);
            return;
        }
        finishFetch(fetch, true, QString::fromUtf8(reply->readAll()));
    });
}

void TankoyomiScriptProvider::finishFetch(const std::shared_ptr<Fetch> &fetch,
                                          bool ok, const QString &payload)
{
    if (fetch->settled) return;
    fetch->settled = true;
    m_fetches.remove(fetch->callbackId);
    if (fetch->deadline) {
        fetch->deadline->stop();
        fetch->deadline->deleteLater();
    }
    if (fetch->reply) {
        disconnect(fetch->reply, nullptr, this, nullptr);
        if (!fetch->reply->isFinished()) fetch->reply->abort();
        fetch->reply->deleteLater();
        fetch->reply.clear();
    }
    deliverFetch(fetch->callbackId, ok, payload);
}

void TankoyomiScriptProvider::deliverFetch(const QString &callbackId,
                                           bool ok,
                                           const QString &payload)
{
    if (!m_deliverFetch.isCallable())
        return;
    QJSValue result = m_deliverFetch.call(
        {m_engine.toScriptValue(callbackId),
         m_engine.toScriptValue(ok),
         m_engine.toScriptValue(payload)});
    if (result.isError())
        emit failed(callbackId, result.toString());
}

void TankoyomiScriptProvider::jsResolve(const QString &token, const QString &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        emit failed(token, QStringLiteral("Tankoyomi provider returned invalid JSON: %1")
                           .arg(error.errorString()));
        return;
    }
    if (document.isArray())
        emit resolved(token, document.array().toVariantList());
    else if (document.isObject())
        emit resolved(token, document.object().toVariantMap());
    else
        emit resolved(token, QVariant());
}

void TankoyomiScriptProvider::jsReject(const QString &token, const QString &message)
{
    emit failed(token, message);
}
