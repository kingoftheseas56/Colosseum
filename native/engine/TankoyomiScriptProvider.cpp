#include "TankoyomiScriptProvider.h"
#include "TankoyomiAddonStore.h"
#include "TankoyomiNetworkPolicy.h"

#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <QTimer>
#include <QSet>

#include <openssl/evp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
namespace {
constexpr qint64 kMetadataResponseLimit = 8 * 1024 * 1024;
constexpr qint64 kWorkerResponseLimit = 2 * 1024 * 1024;
constexpr qint64 kOperationResponseLimit = 16 * 1024 * 1024;
constexpr qint64 kScriptResultLimit = 8 * 1024 * 1024;
constexpr qint64 kRequestBodyLimit = 1 * 1024 * 1024;
constexpr qint64 kProviderScriptLimit = 2 * 1024 * 1024;
constexpr int kOperationRequestBudget = 40;
constexpr int kWorkerDeadlineMs = 45000;
constexpr int kScriptOperationDeadlineMs = 45000;
constexpr int kScriptSliceDeadlineMs = 5000;

class ScriptSliceDeadline final
{
public:
    ScriptSliceDeadline(QJSEngine &engine, int timeoutMs)
        : m_engine(engine),
          m_thread([this, timeoutMs] {
              std::unique_lock lock(m_mutex);
              if (m_condition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                       [this] { return m_cancelled; }))
                  return;
              m_expired.store(true, std::memory_order_release);
              lock.unlock();
              m_engine.setInterrupted(true);
          })
    {
    }

    ~ScriptSliceDeadline()
    {
        {
            std::lock_guard lock(m_mutex);
            m_cancelled = true;
        }
        m_condition.notify_one();
        if (m_thread.joinable()) m_thread.join();
        if (m_expired.load(std::memory_order_acquire)) m_engine.setInterrupted(false);
    }

    bool expired() const { return m_expired.load(std::memory_order_acquire); }

private:
    QJSEngine &m_engine;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_cancelled = false;
    std::atomic_bool m_expired{false};
    std::thread m_thread;
};

bool decodeUtf8Strict(const QByteArray &bytes, QString *text)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) return false;
    if (text) *text = decoded;
    return true;
}

bool deriveCryptoJsKey(const QByteArray &password, const QByteArray &salt,
                       QByteArray *key, QByteArray *iv)
{
    if (salt.size() != 8 || !key || !iv) return false;
    QByteArray material;
    QByteArray previous;
    while (material.size() < 48) {
        EVP_MD_CTX *digest = EVP_MD_CTX_new();
        if (!digest) return false;
        unsigned char output[EVP_MAX_MD_SIZE]{};
        unsigned int outputSize = 0;
        const bool ok = EVP_DigestInit_ex(digest, EVP_md5(), nullptr) == 1
            && (previous.isEmpty()
                || EVP_DigestUpdate(digest, previous.constData(), size_t(previous.size())) == 1)
            && EVP_DigestUpdate(digest, password.constData(), size_t(password.size())) == 1
            && EVP_DigestUpdate(digest, salt.constData(), size_t(salt.size())) == 1
            && EVP_DigestFinal_ex(digest, output, &outputSize) == 1;
        EVP_MD_CTX_free(digest);
        if (!ok || outputSize != 16) return false;
        previous = QByteArray(reinterpret_cast<const char *>(output), int(outputSize));
        material.append(previous);
    }
    *key = material.left(32);
    *iv = material.mid(32, 16);
    return true;
}

QString decryptCryptoJs(const QString &ciphertextBase64,
                        const QString &saltHex,
                        const QString &password)
{
    const QByteArray encoded = ciphertextBase64.toLatin1();
    const QByteArray saltText = saltHex.toLatin1();
    if (encoded.isEmpty() || encoded.size() > 12 * 1024 * 1024
        || !QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{16}$")).match(saltHex).hasMatch())
        return {};
    const QByteArray ciphertext = QByteArray::fromBase64(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (ciphertext.isEmpty() || ciphertext.size() > kMetadataResponseLimit
        || ciphertext.size() % 16 != 0)
        return {};
    const QByteArray salt = QByteArray::fromHex(saltText);
    QByteArray key;
    QByteArray iv;
    if (!deriveCryptoJsKey(password.toUtf8(), salt, &key, &iv)) return {};

    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context) return {};
    QByteArray plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int firstSize = 0;
    int finalSize = 0;
    const bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_cbc(), nullptr,
                                       reinterpret_cast<const unsigned char *>(key.constData()),
                                       reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        && EVP_DecryptUpdate(context,
                             reinterpret_cast<unsigned char *>(plaintext.data()), &firstSize,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                             ciphertext.size()) == 1
        && EVP_DecryptFinal_ex(context,
                               reinterpret_cast<unsigned char *>(plaintext.data()) + firstSize,
                               &finalSize) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok || firstSize < 0 || finalSize < 0
        || qint64(firstSize) + finalSize > kMetadataResponseLimit)
        return {};
    plaintext.resize(firstSize + finalSize);
    QString decoded;
    return decodeUtf8Strict(plaintext, &decoded) ? decoded : QString();
}

QString decodeBase64Utf8(const QString &value)
{
    QByteArray encoded = value.toLatin1();
    if (encoded.isEmpty() || encoded.size() > 16 * 1024)
        return {};
    encoded.replace('-', '+');
    encoded.replace('_', '/');
    while (encoded.size() % 4 != 0) encoded.append('=');
    const QByteArray bytes = QByteArray::fromBase64(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.size() > 8 * 1024) return {};
    QString decoded;
    return decodeUtf8Strict(bytes, &decoded) ? decoded : QString();
}

bool isWorkerPathComponent(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return pattern.match(value).hasMatch();
}

bool isValidWorkerBase(const QUrl &url, const QString &providerId, const QString &language)
{
    if (!url.isValid() || url.isRelative()
        || url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0
        || url.host().isEmpty() || (url.port(-1) != -1 && url.port(-1) != 443)
        || !url.userInfo(QUrl::FullyEncoded).isEmpty()
        || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        || url.hasQuery() || url.hasFragment())
        return false;
    return isWorkerPathComponent(providerId) && isWorkerPathComponent(language);
}

QUrl workerMethodUrl(const QUrl &base, const QString &language,
                     const QString &providerId, const QString &method)
{
    QByteArray encoded = base.toEncoded(QUrl::FullyEncoded);
    while (encoded.endsWith('/')) encoded.chop(1);
    encoded += QByteArrayLiteral("/v1/") + language.toLatin1() + '/'
        + providerId.toLatin1() + '/' + method.toLatin1();
    return QUrl::fromEncoded(encoded, QUrl::StrictMode);
}
} // namespace

struct TankoyomiScriptProvider::Fetch
{
    QString callbackId;
    QString operationToken;
    QUrl logicalUrl;
    QNetworkRequest request;
    QString method;
    QByteArray body;
    QByteArray response;
    int redirects = 0;
    bool settled = false;
    QPointer<QTimer> deadline;
    QPointer<QNetworkReply> reply;
};

struct TankoyomiScriptProvider::ScriptOperation
{
    QString token;
    int requestCount = 0;
    qint64 responseBytes = 0;
    bool settled = false;
    QPointer<QTimer> deadline;
};

struct TankoyomiScriptProvider::WorkerCall
{
    quint64 id = 0;
    QString token;
    QString method;
    QByteArray response;
    bool settled = false;
    QPointer<QTimer> deadline;
    QPointer<QNetworkReply> reply;
};

TankoyomiScriptProvider::TankoyomiScriptProvider(QString providerId,
                                                 QString language,
                                                 QString resourcePath,
                                                 QStringList hosts,
                                                 QNetworkAccessManager *nam,
                                                 QObject *parent)
    : TankoyomiScriptProvider(std::move(providerId), std::move(language), std::move(resourcePath),
                              std::move(hosts), nam, MangaImageHostResolver::Lookup(), parent)
{
}

TankoyomiScriptProvider::TankoyomiScriptProvider(QString providerId,
                                                 QString language,
                                                 QString resourcePath,
                                                 QStringList hosts,
                                                 QNetworkAccessManager *nam,
                                                 MangaImageHostResolver::Lookup lookup,
                                                 QObject *parent)
    : QObject(parent),
      m_providerId(std::move(providerId)),
      m_language(std::move(language)),
      allowedHosts(std::move(hosts)),
      m_nam(nam ? nam : new QNetworkAccessManager(this)),
      m_hostResolver(std::move(lookup), this)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("Unable to open Tankoyomi provider %1").arg(resourcePath);
        return;
    }
    const QByteArray script = file.read(kProviderScriptLimit + 1);
    if (script.size() > kProviderScriptLimit) {
        m_loadError = QStringLiteral("Tankoyomi provider script exceeds 2 MiB");
        return;
    }
    initializeScript(script, resourcePath);
    m_ready = m_deviceScriptLoaded;
}

bool TankoyomiScriptProvider::initializeScript(const QByteArray &script,
                                               const QString &filename)
{
    if (script.size() > kProviderScriptLimit) {
        m_loadError = QStringLiteral("Tankoyomi provider script exceeds 2 MiB");
        return false;
    }
    QString source;
    if (!decodeUtf8Strict(script, &source)) {
        m_loadError = QStringLiteral("Tankoyomi provider script is not valid UTF-8");
        return false;
    }
    // The provider owns its engine; the engine must never collect the provider.
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
    m_engine.globalObject().setProperty(QStringLiteral("__tkNative"), m_engine.newQObject(this));
    const QString bootstrap = QString::fromUtf8(R"JS(
this.__tankoyomiCallbacks = {};
this.__tankoyomiNextCallbackId = 1;
this.__tankoyomiActiveToken = '';

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
    if (value && typeof value.then === 'function') {
      var settled = false;
      try {
        value.then(function(nextValue) {
          if (settled) return;
          settled = true;
          adopt(nextValue, resolve, reject);
        }, function(error) {
          if (settled) return;
          settled = true;
          reject(error);
        });
      } catch (error) {
        if (!settled) reject(error);
      }
      return;
    }
    resolve(value);
  }

  function fetchText(url, options, operationToken) {
    return makeTask(function(resolve, reject) {
      var callbackId = String(root.__tankoyomiNextCallbackId++);
      root.__tankoyomiCallbacks[callbackId] = { resolve: resolve, reject: reject };
      __tkNative.jsFetchText(callbackId, String(url), JSON.stringify(options || {}),
                             String(operationToken || ''));
    });
  }

  root.__tankoyomiContextForToken = function(operationToken) {
    var token = String(operationToken || '');
    return Object.freeze({
      fetchText: function(url, options) { return fetchText(url, options, token); },
      fetchJson: function(url, options) {
        return fetchText(url, options, token).then(function(text) { return JSON.parse(text); });
      },
      decryptCryptoJs: function(ciphertextBase64, saltHex, password) {
        return __tkNative.jsDecryptCryptoJs(String(ciphertextBase64), String(saltHex), String(password));
      },
      decodeBase64: function(value) { return __tkNative.jsDecodeBase64(String(value)); }
    });
  };

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
        var message = String(error && error.message ? error.message : error);
        __tkNative.jsReject(String(token), String(message));
      }
    );
  };
})(this);
)JS");
    QJSValue setup;
    {
        ScriptSliceDeadline deadline(m_engine, kScriptSliceDeadlineMs);
        setup = m_engine.evaluate(bootstrap, QStringLiteral("tankoyomi-runtime.js"));
        if (deadline.expired()) {
            m_loadError = QStringLiteral("Tankoyomi runtime setup exceeded its execution limit");
            return false;
        }
    }
    if (setup.isError()) {
        m_loadError = setup.toString();
        return false;
    }

    m_engine.globalObject().setProperty(QStringLiteral("TankoyomiProvider"),
                                        QJSValue(QJSValue::UndefinedValue));
    QStringList providerTrace;
    QJSValue evaluated;
    {
        ScriptSliceDeadline deadline(m_engine, kScriptSliceDeadlineMs);
        evaluated = m_engine.evaluate(source, filename, 1, &providerTrace);
        if (deadline.expired()) {
            m_loadError = QStringLiteral("Tankoyomi provider script exceeded its execution limit");
            return false;
        }
    }
    if (evaluated.isError()) {
        m_loadError = QStringLiteral("%1 at %2:%3")
            .arg(evaluated.toString(), filename)
            .arg(evaluated.property(QStringLiteral("lineNumber")).toInt());
        if (!providerTrace.isEmpty())
            m_loadError += QStringLiteral(" | ") + providerTrace.join(QStringLiteral(" | "));
        return false;
    }

    m_provider = m_engine.globalObject().property(QStringLiteral("TankoyomiProvider"));
    m_context = m_engine.globalObject().property(QStringLiteral("__tankoyomiContextForToken"));
    m_run = m_engine.globalObject().property(QStringLiteral("__tankoyomiRun"));
    m_deliverFetch = m_engine.globalObject().property(QStringLiteral("__tankoyomiDeliverFetch"));
    if (!m_provider.isObject() || !m_context.isCallable()
        || !m_run.isCallable() || !m_deliverFetch.isCallable()) {
        m_loadError = QStringLiteral("Tankoyomi provider %1 did not expose the provider contract")
                          .arg(m_providerId);
        return false;
    }
    m_deviceScriptLoaded = true;
    return true;
}

TankoyomiScriptProvider::TankoyomiScriptProvider(QString providerId,
                                                 QString language,
                                                 QUrl workerBase,
                                                 QNetworkAccessManager *nam,
                                                 QObject *parent)
    : QObject(parent),
      m_providerId(std::move(providerId)),
      m_language(std::move(language)),
      m_workerBase(std::move(workerBase)),
      m_workerMode(true),
      m_hostResolver(MangaImageHostResolver::Lookup(), this)
{
    if (!isValidWorkerBase(m_workerBase, m_providerId, m_language)) {
        m_loadError = QStringLiteral("Invalid Tankoyomi worker configuration");
        return;
    }
    if (m_language == QLatin1String("es")
        && (m_providerId == QLatin1String("mangasin")
            || m_providerId == QLatin1String("lectorxd"))) {
        m_deviceMode = true;
        m_workerMode = false;
        m_addonStore = new TankoyomiAddonStore(m_workerBase, this);
        // Never share the app manager or its account cookies with device scripts.
        m_ownedDeviceNam = new QNetworkAccessManager(this);
        m_nam = m_ownedDeviceNam;
        m_ready = true;
        return;
    }
    m_workerMode = true;
    m_nam = nam ? nam : new QNetworkAccessManager(this);
    m_ready = true;
}

TankoyomiScriptProvider::TankoyomiScriptProvider(
    QString providerId, QString language, TankoyomiAddonStore *addonStore,
    QNetworkAccessManager *sourceNam, MangaImageHostResolver::Lookup lookup,
    QObject *parent)
    : QObject(parent),
      m_providerId(std::move(providerId)),
      m_language(std::move(language)),
      m_nam(sourceNam ? sourceNam : new QNetworkAccessManager(this)),
      m_hostResolver(std::move(lookup), this)
{
    m_deviceMode = true;
    m_ready = true;
    const bool allowedId = m_language == QLatin1String("es")
        && (m_providerId == QLatin1String("mangasin")
            || m_providerId == QLatin1String("lectorxd"));
    if (!allowedId || !addonStore) {
        m_loadError = QStringLiteral("Invalid Tankoyomi device addon configuration");
        return;
    }
    m_addonStore = addonStore;
}

void TankoyomiScriptProvider::searchSeries(const QString &token, const QString &query)
{
    if (m_deviceMode) {
        queueDeviceCall(QStringLiteral("searchSeries"), token, query);
        return;
    }
    if (m_workerMode) {
        QJsonObject payload;
        payload.insert(QStringLiteral("query"), query);
        invokeWorker(QStringLiteral("searchSeries"), token, payload);
        return;
    }
    invoke(QStringLiteral("searchSeries"), token, {m_engine.toScriptValue(query)});
}

void TankoyomiScriptProvider::getChapters(const QString &token, const QVariantMap &series)
{
    if (m_deviceMode) {
        queueDeviceCall(QStringLiteral("getChapters"), token, series);
        return;
    }
    if (m_workerMode) {
        QJsonObject payload;
        payload.insert(QStringLiteral("series"), QJsonObject::fromVariantMap(series));
        invokeWorker(QStringLiteral("getChapters"), token, payload);
        return;
    }
    invoke(QStringLiteral("getChapters"), token, {m_engine.toScriptValue(series)});
}

void TankoyomiScriptProvider::getPages(const QString &token, const QVariantMap &chapter)
{
    if (m_deviceMode) {
        queueDeviceCall(QStringLiteral("getPages"), token, chapter);
        return;
    }
    if (m_workerMode) {
        QJsonObject payload;
        payload.insert(QStringLiteral("chapter"), QJsonObject::fromVariantMap(chapter));
        invokeWorker(QStringLiteral("getPages"), token, payload);
        return;
    }
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
    if (m_scriptOperations.contains(token)) {
        emit failed(token, QStringLiteral("Duplicate Tankoyomi operation token"));
        return;
    }
    startScriptOperation(token);

    QJSValueList callArgs;
    const QJSValue scopedContext = m_context.call({m_engine.toScriptValue(token)});
    if (scopedContext.isError()) {
        finishScriptOperation(token, false, QVariant(), scopedContext.toString());
        return;
    }
    callArgs.append(scopedContext);
    callArgs.append(args);
    m_engine.globalObject().setProperty(QStringLiteral("__tankoyomiActiveToken"), token);
    QJSValue task;
    QJSValue started;
    bool executionExpired = false;
    {
        ScriptSliceDeadline deadline(m_engine, kScriptSliceDeadlineMs);
        task = function.callWithInstance(m_provider, callArgs);
        if (!task.isError())
            started = m_run.call({task, m_engine.toScriptValue(token)});
        executionExpired = deadline.expired();
    }
    m_engine.globalObject().setProperty(QStringLiteral("__tankoyomiActiveToken"), QString());
    if (executionExpired)
        finishScriptOperation(token, false, QVariant(),
                              QStringLiteral("Tankoyomi provider execution timed out"));
    else if (task.isError())
        finishScriptOperation(token, false, QVariant(), task.toString());
    else if (started.isError())
        finishScriptOperation(token, false, QVariant(), started.toString());
}

void TankoyomiScriptProvider::queueDeviceCall(const QString &method,
                                              const QString &token,
                                              const QVariant &value)
{
    if (!m_ready || !m_loadError.isEmpty()) {
        const QString message = m_loadError.isEmpty()
            ? QStringLiteral("Tankoyomi device addon is unavailable") : m_loadError;
        QMetaObject::invokeMethod(this, [this, token, message] { emit failed(token, message); },
                                  Qt::QueuedConnection);
        return;
    }
    if (m_pendingDeviceCalls.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, token] {
            emit failed(token, QStringLiteral("Too many Tankoyomi addon calls are queued"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_scriptOperations.contains(token)) {
        QMetaObject::invokeMethod(this, [this, token] {
            emit failed(token, QStringLiteral("Duplicate Tankoyomi operation token"));
        }, Qt::QueuedConnection);
        return;
    }
    for (const PendingDeviceCall &pending : std::as_const(m_pendingDeviceCalls)) {
        if (pending.token == token) {
            QMetaObject::invokeMethod(this, [this, token] {
                emit failed(token, QStringLiteral("Duplicate Tankoyomi operation token"));
            }, Qt::QueuedConnection);
            return;
        }
    }
    m_pendingDeviceCalls.append(PendingDeviceCall{method, token, value});
    ensureDeviceScriptLoaded();
}

void TankoyomiScriptProvider::ensureDeviceScriptLoaded()
{
    if (!m_deviceMode) return;
    if (m_deviceScriptLoaded) {
        drainDeviceCalls();
        return;
    }
    if (m_deviceScriptLoading || !m_loadError.isEmpty()) return;
    if (!m_addonStore) {
        m_loadError = QStringLiteral("Tankoyomi signed addon store is unavailable");
        drainDeviceCalls();
        return;
    }
    m_deviceScriptLoading = true;
    const QPointer<TankoyomiScriptProvider> guard(this);
    m_addonStore->load(m_providerId,
                       [guard](QByteArray script, QStringList hosts, QString error) mutable {
        if (!guard) return;
        guard->m_deviceScriptLoading = false;
        if (!error.isEmpty()) {
            guard->m_loadError = std::move(error);
            guard->drainDeviceCalls();
            // A later user action can retry a transient delivery failure.
            if (guard) guard->m_loadError.clear();
            return;
        }
        guard->allowedHosts = std::move(hosts);
        if (guard->allowedHosts.isEmpty()) {
            guard->m_loadError = QStringLiteral("Tankoyomi addon has no metadata host allowlist");
            guard->drainDeviceCalls();
            return;
        }
        if (!guard->initializeScript(script,
                                     QStringLiteral("signed-addon:%1").arg(guard->m_providerId))) {
            guard->drainDeviceCalls();
            return;
        }
        guard->drainDeviceCalls();
    });
}

void TankoyomiScriptProvider::drainDeviceCalls()
{
    if (m_pendingDeviceCalls.isEmpty()) return;
    if (!m_loadError.isEmpty()) {
        const QVector<PendingDeviceCall> pending = std::move(m_pendingDeviceCalls);
        m_pendingDeviceCalls.clear();
        for (const PendingDeviceCall &call : pending)
            emit failed(call.token, m_loadError);
        return;
    }
    if (!m_deviceScriptLoaded) return;
    const QVector<PendingDeviceCall> pending = std::move(m_pendingDeviceCalls);
    m_pendingDeviceCalls.clear();
    for (const PendingDeviceCall &call : pending)
        invokeDeviceCall(call);
}

void TankoyomiScriptProvider::invokeDeviceCall(const PendingDeviceCall &call)
{
    const QByteArray input = QJsonDocument::fromVariant(call.value).toJson(QJsonDocument::Compact);
    const qint64 inputSize = call.value.metaType().id() == QMetaType::QString
        ? call.value.toString().toUtf8().size() : input.size();
    if (inputSize > kRequestBodyLimit) {
        emit failed(call.token, QStringLiteral("Tankoyomi addon input exceeds 1 MiB"));
        return;
    }
    invoke(call.method, call.token, {m_engine.toScriptValue(call.value)});
}

void TankoyomiScriptProvider::startScriptOperation(const QString &token)
{
    auto operation = std::make_shared<ScriptOperation>();
    operation->token = token;
    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    operation->deadline = deadline;
    m_scriptOperations.insert(token, operation);
    connect(deadline, &QTimer::timeout, this, [this, token] { timeoutScriptOperation(token); });
    deadline->start(kScriptOperationDeadlineMs);
}

void TankoyomiScriptProvider::finishScriptOperation(const QString &token, bool ok,
                                                    const QVariant &value,
                                                    const QString &message)
{
    const auto operation = m_scriptOperations.take(token);
    if (!operation) return;
    const auto callbacks = m_engine.globalObject().property(QStringLiteral("__tankoyomiCallbacks"));
    const auto callbackIds = m_fetchTokens.keys(token);
    for (const QString &callbackId : callbackIds) {
        QJSValue callbackMap = callbacks;
        callbackMap.deleteProperty(callbackId);
        m_fetchTokens.remove(callbackId);
    }
    if (operation) {
        operation->settled = true;
        if (operation->deadline) {
            operation->deadline->stop();
            operation->deadline->deleteLater();
            operation->deadline.clear();
        }
    }
    const auto pending = m_fetches.values();
    for (const auto &fetch : pending) {
        if (fetch->operationToken != token) continue;
        fetch->settled = true;
        m_fetches.remove(fetch->callbackId);
        m_fetchTokens.remove(fetch->callbackId);
        if (fetch->deadline) {
            fetch->deadline->stop();
            fetch->deadline->deleteLater();
            fetch->deadline.clear();
        }
        if (fetch->reply) {
            disconnect(fetch->reply, nullptr, this, nullptr);
            if (!fetch->reply->isFinished()) fetch->reply->abort();
            fetch->reply->deleteLater();
            fetch->reply.clear();
        }
    }
    if (ok) emit resolved(token, value);
    else emit failed(token, message);
}

void TankoyomiScriptProvider::timeoutScriptOperation(const QString &token)
{
    if (!m_scriptOperations.contains(token)) return;
    finishScriptOperation(token, false, QVariant(),
                          QStringLiteral("Tankoyomi provider operation timed out"));
}
TankoyomiScriptProvider::~TankoyomiScriptProvider()
{
    m_pendingDeviceCalls.clear();
    const auto operations = m_scriptOperations.values();
    m_scriptOperations.clear();
    for (const auto &operation : operations) {
        operation->settled = true;
        if (operation->deadline) operation->deadline->stop();
    }

    const auto workerCalls = m_workerCalls.values();
    m_workerCalls.clear();
    for (const auto &call : workerCalls) {
        call->settled = true;
        if (call->deadline) call->deadline->stop();
        if (call->reply) {
            disconnect(call->reply, nullptr, this, nullptr);
            if (!call->reply->isFinished()) call->reply->abort();
            call->reply->deleteLater();
        }
    }

    // The manager can outlive this provider. Cancel its work before QJSEngine
    // destruction, rather than leaving replies attached to a shared manager.
    const auto pending = m_fetches.values();
    m_fetches.clear();
    m_fetchTokens.clear();
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

void TankoyomiScriptProvider::invokeWorker(const QString &method,
                                           const QString &token,
                                           const QJsonObject &payload)
{
    if (!m_ready) {
        const QString message = m_loadError.isEmpty()
            ? QStringLiteral("Tankoyomi worker is unavailable") : m_loadError;
        QMetaObject::invokeMethod(this, [this, token, message] { emit failed(token, message); },
                                  Qt::QueuedConnection);
        return;
    }
    if (!m_nam) {
        QMetaObject::invokeMethod(this, [this, token] {
            emit failed(token, QStringLiteral("Tankoyomi worker network manager is unavailable"));
        }, Qt::QueuedConnection);
        return;
    }

    const QUrl endpoint = workerMethodUrl(m_workerBase, m_language, m_providerId, method);
    if (!endpoint.isValid()) {
        QMetaObject::invokeMethod(this, [this, token] {
            emit failed(token, QStringLiteral("Invalid Tankoyomi worker endpoint"));
        }, Qt::QueuedConnection);
        return;
    }

    auto call = std::make_shared<WorkerCall>();
    call->id = m_nextWorkerCallId++;
    call->token = token;
    call->method = method;
    m_workerCalls.insert(call->id, call);

    QNetworkRequest request(endpoint);
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Colosseum-Tankoyomi/1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kWorkerDeadlineMs);

    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    call->deadline = deadline;
    connect(deadline, &QTimer::timeout, this, [this, call] {
        finishWorkerCall(call, false, QVariant(), QStringLiteral("Tankoyomi worker request timeout"));
    });
    deadline->start(kWorkerDeadlineMs);

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (body.size() > kRequestBodyLimit) {
        finishWorkerCall(call, false, QVariant(),
                         QStringLiteral("Tankoyomi worker request body exceeds 1 MiB"));
        return;
    }
    QNetworkReply *reply = m_nam->post(request, body);
    call->reply = reply;
    if (!reply) {
        finishWorkerCall(call, false, QVariant(), QStringLiteral("Tankoyomi worker request could not start"));
        return;
    }
    connect(reply, &QIODevice::readyRead, this, [this, call] { readWorkerResponse(call); });
    connect(reply, &QNetworkReply::finished, this, [this, call, reply] {
        if (call->settled) return;
        readWorkerResponse(call);
        if (call->settled) return;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if ((status >= 300 && status < 400) || !redirect.isEmpty()) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker redirect rejected"));
            return;
        }
        if (status < 200 || status >= 300) {
            const QString message = status > 0
                ? QStringLiteral("Tankoyomi worker returned HTTP %1").arg(status)
                : QStringLiteral("Tankoyomi worker transport failed");
            finishWorkerCall(call, false, QVariant(), message);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            finishWorkerCall(call, false, QVariant(), QStringLiteral("Tankoyomi worker transport failed"));
            return;
        }

        if (!decodeUtf8Strict(call->response, nullptr)) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker returned invalid UTF-8 JSON"));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(call->response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker returned invalid JSON"));
            return;
        }

        const QJsonObject response = document.object();
        const QJsonValue version = response.value(QStringLiteral("contractVersion"));
        if (!version.isDouble() || version.toDouble() != 1.0) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker contract version is unsupported"));
            return;
        }
        const bool hasResult = response.contains(QStringLiteral("result"));
        const bool hasError = response.contains(QStringLiteral("error"));
        if (hasResult == hasError) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker response has an invalid schema"));
            return;
        }
        if (hasError) {
            if (!response.value(QStringLiteral("error")).isString()) {
                finishWorkerCall(call, false, QVariant(),
                                 QStringLiteral("Tankoyomi worker response has an invalid schema"));
                return;
            }
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker rejected the %1 request").arg(call->method));
            return;
        }
        if (!response.value(QStringLiteral("result")).isArray()) {
            finishWorkerCall(call, false, QVariant(),
                             QStringLiteral("Tankoyomi worker response has an invalid schema"));
            return;
        }
        finishWorkerCall(call, true,
                         response.value(QStringLiteral("result")).toArray().toVariantList(), QString());
    });
}

void TankoyomiScriptProvider::readWorkerResponse(const std::shared_ptr<WorkerCall> &call)
{
    if (call->settled || !call->reply) return;
    const qint64 available = call->reply->bytesAvailable();
    if (available < 0 || available > kWorkerResponseLimit - call->response.size()) {
        finishWorkerCall(call, false, QVariant(),
                         QStringLiteral("Tankoyomi worker response exceeds 2 MiB"));
        return;
    }
    if (available == 0) return;
    const QByteArray chunk = call->reply->read(available);
    if (chunk.size() > kWorkerResponseLimit - call->response.size()) {
        finishWorkerCall(call, false, QVariant(),
                         QStringLiteral("Tankoyomi worker response exceeds 2 MiB"));
        return;
    }
    call->response.append(chunk);
}

void TankoyomiScriptProvider::finishWorkerCall(const std::shared_ptr<WorkerCall> &call,
                                               bool ok,
                                               const QVariant &value,
                                               const QString &message)
{
    if (!call || call->settled) return;
    call->settled = true;
    m_workerCalls.remove(call->id);
    if (call->deadline) {
        call->deadline->stop();
        call->deadline->deleteLater();
        call->deadline.clear();
    }
    if (call->reply) {
        disconnect(call->reply, nullptr, this, nullptr);
        if (!call->reply->isFinished()) call->reply->abort();
        call->reply->deleteLater();
        call->reply.clear();
    }

    const QString token = call->token;
    QMetaObject::invokeMethod(this, [this, token, ok, value, message] {
        if (ok) emit resolved(token, value);
        else emit failed(token, message);
    }, Qt::QueuedConnection);
}

bool TankoyomiScriptProvider::hostAllowed(const QString &host) const
{
    return TankoyomiNetworkPolicy::metadataHostAllowed(host, allowedHosts);
}

void TankoyomiScriptProvider::jsFetchText(const QString &callbackId,
                                          const QString &url,
                                          const QString &optionsJson,
                                          const QString &operationToken)
{
    const auto operation = m_scriptOperations.value(operationToken);
    if (!operation || operation->settled) {
        deliverFetch(callbackId, false, QStringLiteral("Tankoyomi operation is unavailable"));
        return;
    }
    if (++operation->requestCount > kOperationRequestBudget) {
        m_fetchTokens.insert(callbackId, operationToken);
        // A hard budget is authoritative even if provider code catches errors.
        finishScriptOperation(operationToken, false, QVariant(),
                              QStringLiteral("Tankoyomi operation exceeded its 40-request budget"));
        return;
    }
    m_fetchTokens.insert(callbackId, operationToken);
    if (optionsJson.toUtf8().size() > kRequestBodyLimit) {
        deliverFetch(callbackId, false, QStringLiteral("Tankoyomi request options exceed 1 MiB"));
        return;
    }
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
    fetch->operationToken = operationToken;
    fetch->logicalUrl = target;
    fetch->request = QNetworkRequest(target);
    fetch->method = method;
    fetch->body = options.value(QStringLiteral("body")).toString().toUtf8();
    if (fetch->body.size() > kRequestBodyLimit
        || (method == QLatin1String("GET") && !fetch->body.isEmpty())) {
        deliverFetch(callbackId, false, QStringLiteral("Tankoyomi request body is invalid or exceeds 1 MiB"));
        return;
    }
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
    static const QSet<QByteArray> allowedHeaders = {
        QByteArrayLiteral("accept"), QByteArrayLiteral("accept-language"),
        QByteArrayLiteral("content-type"), QByteArrayLiteral("referer")};
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toUtf8().toLower();
        const QByteArray value = it.value().toString().toUtf8();
        if (!allowedHeaders.contains(name) || name.size() > 128 || value.size() > 8192
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
    const QString host = fetch->logicalUrl.host().toLower();
    // Policy is decided against the logical hostname before any wire rewrite.
    // Every hop (including redirects to a new allowed host) resolves through
    // the async resolver; a dead-IPv6 or private result fails this fetch closed.
    if (!m_pins.contains(host)) {
        m_hostResolver.resolve(host, [this, fetch, host](const QString &ipv4) {
            if (fetch->settled) return;
            if (!TankoyomiNetworkPolicy::resolvedAddressAllowed(QHostAddress(ipv4),
                                                                QStringLiteral("public-https"))) {
                finishFetch(fetch, false,
                            QStringLiteral("Tankoyomi provider %1 host %2 has no public IPv4 route")
                                .arg(m_providerId, host));
                return;
            }
            m_pins.insert(host, ipv4);
            issueFetch(fetch);
        });
        return;
    }
    QNetworkRequest request = fetch->request;
    QUrl wireUrl = fetch->logicalUrl;
    request.setRawHeader("Host", host.toUtf8());
    request.setPeerVerifyName(host);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    wireUrl.setHost(m_pins.value(host));
    request.setUrl(wireUrl);
    QNetworkReply *reply = fetch->method == QLatin1String("POST")
        ? m_nam->post(request, fetch->body) : m_nam->get(request);
    fetch->reply = reply;
    if (!reply) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi request could not start"));
        return;
    }
    connect(reply, &QIODevice::readyRead, this, [this, fetch] { readFetchResponse(fetch); });
    connect(reply, &QNetworkReply::finished, this, [this, fetch, reply] {
        if (fetch->settled) return;
        readFetchResponse(fetch);
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
            fetch->response.clear();
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
        QString payload;
        if (!decodeUtf8Strict(fetch->response, &payload)) {
            finishFetch(fetch, false, QStringLiteral("Tankoyomi metadata response is not valid UTF-8"));
            return;
        }
        finishFetch(fetch, true, payload);
    });
}

void TankoyomiScriptProvider::readFetchResponse(const std::shared_ptr<Fetch> &fetch)
{
    if (!fetch || fetch->settled || !fetch->reply) return;
    const auto operation = m_scriptOperations.value(fetch->operationToken);
    if (!operation || operation->settled) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi operation is no longer active"));
        return;
    }
    const qint64 available = fetch->reply->bytesAvailable();
    if (available < 0 || available > kMetadataResponseLimit - fetch->response.size()) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi metadata response exceeds 8 MiB"));
        return;
    }
    if (available > kOperationResponseLimit - operation->responseBytes) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi operation responses exceed 16 MiB"));
        return;
    }
    if (available == 0) return;
    const QByteArray chunk = fetch->reply->read(available);
    if (chunk.size() > kMetadataResponseLimit - fetch->response.size()) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi metadata response exceeds 8 MiB"));
        return;
    }
    if (chunk.size() > kOperationResponseLimit - operation->responseBytes) {
        finishFetch(fetch, false, QStringLiteral("Tankoyomi operation responses exceed 16 MiB"));
        return;
    }
    fetch->response.append(chunk);
    operation->responseBytes += chunk.size();
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
    const QString token = m_fetchTokens.take(callbackId);
    if (!m_deliverFetch.isCallable()) {
        if (!token.isEmpty())
            finishScriptOperation(token, false, QVariant(),
                                  QStringLiteral("Tankoyomi runtime fetch bridge is unavailable"));
        return;
    }
    const QString previousToken = m_engine.globalObject()
        .property(QStringLiteral("__tankoyomiActiveToken")).toString();
    m_engine.globalObject().setProperty(QStringLiteral("__tankoyomiActiveToken"), token);
    QJSValue result;
    bool executionExpired = false;
    {
        ScriptSliceDeadline deadline(m_engine, kScriptSliceDeadlineMs);
        result = m_deliverFetch.call(
            {m_engine.toScriptValue(callbackId),
             m_engine.toScriptValue(ok),
             m_engine.toScriptValue(payload)});
        executionExpired = deadline.expired();
    }
    m_engine.globalObject().setProperty(QStringLiteral("__tankoyomiActiveToken"), previousToken);
    if (!token.isEmpty() && executionExpired)
        finishScriptOperation(token, false, QVariant(),
                              QStringLiteral("Tankoyomi provider execution timed out"));
    else if (!token.isEmpty() && result.isError())
        finishScriptOperation(token, false, QVariant(), result.toString());
}

void TankoyomiScriptProvider::jsResolve(const QString &token, const QString &json)
{
    if (json.toUtf8().size() > kScriptResultLimit) {
        finishScriptOperation(token, false, QVariant(),
                              QStringLiteral("Tankoyomi provider result exceeds 8 MiB"));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        finishScriptOperation(token, false, QVariant(),
                              QStringLiteral("Tankoyomi provider returned invalid JSON: %1")
                                  .arg(error.errorString()));
        return;
    }
    if (document.isArray())
        finishScriptOperation(token, true, document.array().toVariantList(), QString());
    else if (document.isObject())
        finishScriptOperation(token, true, document.object().toVariantMap(), QString());
    else
        finishScriptOperation(token, true, QVariant(), QString());
}

void TankoyomiScriptProvider::jsReject(const QString &token, const QString &message)
{
    finishScriptOperation(token, false, QVariant(), message.left(4096));
}

QString TankoyomiScriptProvider::jsDecryptCryptoJs(const QString &ciphertextBase64,
                                                   const QString &saltHex,
                                                   const QString &password)
{
    if (password.isEmpty() || password.size() > 256) return {};
    return decryptCryptoJs(ciphertextBase64, saltHex, password);
}

QString TankoyomiScriptProvider::jsDecodeBase64(const QString &value)
{
    return decodeBase64Utf8(value);
}
