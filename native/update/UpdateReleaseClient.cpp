#include "update/UpdateReleaseClient.h"

#include "update/UpdateTrust.h"
#include "update/UpdateVersion.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QTimer>

namespace Colosseum::Update {
namespace {

constexpr qint64 kApiBodyCap = 2 * 1024 * 1024;
constexpr qint64 kManifestBodyCap = 512 * 1024;
constexpr qint64 kSignatureBodyCap = 128;
constexpr auto kManifestAsset = "colosseum-update-v1.json";
constexpr auto kSignatureAsset = "colosseum-update-v1.json.sig";

struct ApiAsset {
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;
};

struct DiscoveryState {
    QString apiTag;
    QString etag;
    QHash<QString, ApiAsset> assets;
    QByteArray manifestBytes;
};

void reject(ReleaseCheckResult* result, const QString& code)
{
    result->status = ReleaseCheckResult::Status::Rejected;
    result->errorCode = code;
}

QByteArray sha256Text(const QJsonValue& value)
{
    if (!value.isString())
        return {};
    const QString text = value.toString();
    static const QRegularExpression pattern(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
    const auto match = pattern.match(text);
    return match.hasMatch() ? QByteArray::fromHex(match.captured(1).toLatin1()) : QByteArray{};
}

bool finiteInteger(const QJsonValue& value, qint64* result)
{
    if (!value.isDouble())
        return false;
    const qint64 number = value.toInteger(0);
    if (number <= 0)
        return false;
    *result = number;
    return true;
}

} // namespace

UpdateReleaseClient::UpdateReleaseClient(QNetworkAccessManager* nam, ReleaseClientConfig config,
                                         QObject* parent)
    : QObject(parent), m_nam(nam), m_config(std::move(config))
{
}

bool UpdateReleaseClient::allowedUrl(const QUrl& url) const
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    return url.scheme() == QLatin1String("https")
        || (m_config.allowHttpForTests && url.scheme() == QLatin1String("http"));
}

void UpdateReleaseClient::fetch(const QUrl& url, qint64 cap, const QByteArray& priorEtag,
                                std::function<void(const FetchResult&)> done)
{
    if (m_cancelled || !m_nam) {
        FetchResult result;
        result.errorCode = m_cancelled ? QStringLiteral("cancelled")
                                       : QStringLiteral("network_manager_missing");
        done(result);
        return;
    }
    if (!allowedUrl(url)) {
        FetchResult result;
        result.unsafeRedirect = true;
        result.errorCode = QStringLiteral("unsafe_url");
        done(result);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "Colosseum/1.1.4");
    if (!priorEtag.isEmpty())
        request.setRawHeader("If-None-Match", priorEtag);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(request);
    m_reply = reply;
    const auto buffer = QSharedPointer<QByteArray>::create();
    const auto tooLarge = QSharedPointer<bool>::create(false);
    const auto unsafeRedirect = QSharedPointer<bool>::create(false);
    const auto timedOut = QSharedPointer<bool>::create(false);

    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, cap, tooLarge] {
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() > cap) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, cap, buffer, tooLarge] {
        buffer->append(reply->readAll());
        if (buffer->size() > cap) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::redirected, this,
            [this, reply, unsafeRedirect](const QUrl& target) {
                if (!allowedUrl(target)
                    || (reply->url().scheme() == QLatin1String("https")
                        && target.scheme() != QLatin1String("https"))) {
                    *unsafeRedirect = true;
                    reply->abort();
                }
            });

    QTimer::singleShot(qMax(1, m_config.timeoutMs), reply, [reply, timedOut] {
        if (!reply->isFinished()) {
            *timedOut = true;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cap, buffer, tooLarge, unsafeRedirect, timedOut, done = std::move(done)]() mutable {
                FetchResult result;
                result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.etag = QString::fromUtf8(reply->rawHeader("ETag"));
                if (m_reply == reply)
                    m_reply = nullptr;

                if (m_cancelled) {
                    result.errorCode = QStringLiteral("cancelled");
                } else if (*unsafeRedirect) {
                    result.unsafeRedirect = true;
                    result.errorCode = QStringLiteral("unsafe_redirect");
                } else if (*tooLarge) {
                    result.bodyTooLarge = true;
                    result.errorCode = QStringLiteral("body_too_large");
                } else if (*timedOut) {
                    result.errorCode = QStringLiteral("timeout");
                } else if (reply->error() == QNetworkReply::InsecureRedirectError) {
                    result.unsafeRedirect = true;
                    result.errorCode = QStringLiteral("unsafe_redirect");
                } else if (result.httpStatus == 304) {
                    result.transportOk = true;
                } else if (reply->error() != QNetworkReply::NoError) {
                    result.errorCode = QStringLiteral("network_error");
                } else if (result.httpStatus < 200 || result.httpStatus >= 300) {
                    result.errorCode = QStringLiteral("http_%1").arg(result.httpStatus);
                } else {
                    buffer->append(reply->readAll());
                    if (buffer->size() > cap) {
                        result.bodyTooLarge = true;
                        result.errorCode = QStringLiteral("body_too_large");
                    } else {
                        result.transportOk = true;
                        result.body = *buffer;
                    }
                }
                reply->deleteLater();
                done(result);
            });
}

void UpdateReleaseClient::checkLatest(const QString& priorEtag, Callback done)
{
    cancel();
    m_cancelled = false;
    m_done = std::move(done);
    if (!m_done)
        return;

    fetch(m_config.latestReleaseUrl, kApiBodyCap, priorEtag.toUtf8(),
          [this](const FetchResult& latest) {
              if (m_cancelled)
                  return;
              ReleaseCheckResult result;
              result.etag = latest.etag;
              if (latest.httpStatus == 304 && latest.transportOk) {
                  result.status = ReleaseCheckResult::Status::NotModified;
                  finish(result);
                  return;
              }
              if (!latest.transportOk) {
                  result.status = latest.bodyTooLarge || latest.unsafeRedirect
                      ? ReleaseCheckResult::Status::Rejected
                      : ReleaseCheckResult::Status::NetworkError;
                  result.errorCode = latest.errorCode;
                  finish(result);
                  return;
              }

              QJsonParseError parseError;
              const QJsonDocument document = QJsonDocument::fromJson(latest.body, &parseError);
              if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                  reject(&result, QStringLiteral("invalid_api_json"));
                  finish(result);
                  return;
              }
              const QJsonObject root = document.object();
              const QJsonValue tagValue = root.value(QStringLiteral("tag_name"));
              const QJsonValue draftValue = root.value(QStringLiteral("draft"));
              const QJsonValue prereleaseValue = root.value(QStringLiteral("prerelease"));
              if (!tagValue.isString() || !draftValue.isBool() || !prereleaseValue.isBool()
                  || draftValue.toBool() || prereleaseValue.toBool()) {
                  reject(&result, QStringLiteral("release_not_stable"));
                  finish(result);
                  return;
              }

              const auto apiVersion = Version::parseTag(tagValue.toString());
              const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
              if (!apiVersion || assets.isEmpty()) {
                  reject(&result, QStringLiteral("invalid_release_metadata"));
                  finish(result);
                  return;
              }

              const auto state = QSharedPointer<DiscoveryState>::create();
              state->apiTag = tagValue.toString();
              state->etag = latest.etag;
              for (const QJsonValue& value : assets) {
                  const QJsonObject asset = value.toObject();
                  const QString name = asset.value(QStringLiteral("name")).toString();
                  const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
                  qint64 size = 0;
                  const QByteArray digest = sha256Text(asset.value(QStringLiteral("digest")));
                  if (name.isEmpty() || state->assets.contains(name) || !allowedUrl(url)
                      || !finiteInteger(asset.value(QStringLiteral("size")), &size)
                      || digest.size() != 32) {
                      reject(&result, state->assets.contains(name)
                          ? QStringLiteral("duplicate_asset_name")
                          : QStringLiteral("invalid_asset_metadata"));
                      finish(result);
                      return;
                  }
                  state->assets.insert(name, ApiAsset{url, size, digest});
              }
              if (!state->assets.contains(QString::fromLatin1(kManifestAsset))
                  || !state->assets.contains(QString::fromLatin1(kSignatureAsset))) {
                  reject(&result, QStringLiteral("missing_manifest_or_signature"));
                  finish(result);
                  return;
              }

              const ApiAsset manifestAsset = state->assets.value(QString::fromLatin1(kManifestAsset));
              fetch(manifestAsset.url, kManifestBodyCap, {},
                    [this, state](const FetchResult& manifest) {
                        if (m_cancelled)
                            return;
                        ReleaseCheckResult result;
                        result.etag = state->etag;
                        if (!manifest.transportOk) {
                            reject(&result, manifest.bodyTooLarge
                                ? QStringLiteral("manifest_body_too_large")
                                : manifest.errorCode);
                            finish(result);
                            return;
                        }
                        state->manifestBytes = manifest.body;
                        const ApiAsset signatureAsset =
                            state->assets.value(QString::fromLatin1(kSignatureAsset));
                        fetch(signatureAsset.url, kSignatureBodyCap, {},
                              [this, state](const FetchResult& signature) {
                                  if (m_cancelled)
                                      return;
                                  ReleaseCheckResult result;
                                  result.etag = state->etag;
                                  if (!signature.transportOk) {
                                      reject(&result, signature.bodyTooLarge
                                          ? QStringLiteral("signature_body_too_large")
                                          : signature.errorCode);
                                      finish(result);
                                      return;
                                  }
                                  const QByteArray key = m_config.publicKey.isEmpty()
                                      ? QByteArray(embeddedUpdatePublicKey().data(),
                                                   embeddedUpdatePublicKey().size())
                                      : m_config.publicKey;
                                  QString error;
                                  if (!verifyEd25519Raw(state->manifestBytes, signature.body, key,
                                                        &error)) {
                                      reject(&result, QStringLiteral("invalid_manifest_signature"));
                                      finish(result);
                                      return;
                                  }
                                  const auto manifest = parseManifest(state->manifestBytes, &error);
                                  if (!manifest) {
                                      reject(&result, QStringLiteral("invalid_manifest"));
                                      finish(result);
                                      return;
                                  }
                                  const auto apiVersion = Version::parseTag(state->apiTag);
                                  if (!apiVersion || manifest->tag != state->apiTag
                                      || manifest->version.compare(*apiVersion) != 0) {
                                      reject(&result, QStringLiteral("api_manifest_tag_mismatch"));
                                      finish(result);
                                      return;
                                  }
                                  if (!m_config.automaticInstallerSupported) {
                                      result.status = ReleaseCheckResult::Status::ManualUpdateRequired;
                                      result.manifest = *manifest;
                                      result.verifiedManifestBytes = state->manifestBytes;
                                      result.verifiedSignatureBytes = signature.body;
                                      // Deliberately return no asset URLs. In particular the legacy
                                      // schema's Windows .exe is never handed to the Linux downloader.
                                      finish(result);
                                      return;
                                  }

                                  const auto installerIt = state->assets.constFind(manifest->installerAsset);
                                  if (installerIt == state->assets.constEnd()) {
                                      reject(&result, QStringLiteral("missing_installer"));
                                      finish(result);
                                      return;
                                  }
                                  if (installerIt->size != manifest->installerSize) {
                                      reject(&result, QStringLiteral("api_size_mismatch"));
                                      finish(result);
                                      return;
                                  }
                                  if (installerIt->sha256 != manifest->installerSha256) {
                                      reject(&result, QStringLiteral("api_digest_mismatch"));
                                      finish(result);
                                      return;
                                  }
                                  result.status = ReleaseCheckResult::Status::Valid;
                                  result.manifest = *manifest;
                                  result.verifiedManifestBytes = state->manifestBytes;
                                  result.verifiedSignatureBytes = signature.body;
                                  for (auto it = state->assets.constBegin(); it != state->assets.constEnd(); ++it)
                                      result.assetUrls.insert(it.key(), it->url);
                                  finish(result);
                              });
                    });
          });
}

void UpdateReleaseClient::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void UpdateReleaseClient::finish(const ReleaseCheckResult& result)
{
    if (!m_done)
        return;
    Callback done = std::move(m_done);
    m_done = {};
    done(result);
}

} // namespace Colosseum::Update
