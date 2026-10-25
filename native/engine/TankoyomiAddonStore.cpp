#include "TankoyomiAddonStore.h"
#include "TankoyomiAddonPublicKey.h"
#include "update/UpdateTrust.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>
#include <cmath>
#include <memory>

namespace {
constexpr qsizetype kEnvelopeLimit = 1024 * 1024;
constexpr qsizetype kScriptLimit = 256 * 1024;
constexpr auto kDomain = "Colosseum Tankoyomi addon v1\n";
QString hostFor(const QString &id)
{
    if (id == QLatin1String("mangasin")) return QStringLiteral("m440.in");
    if (id == QLatin1String("lectorxd")) return QStringLiteral("lectorxd.com");
    return {};
}
bool reject(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool decodeBase64(const QJsonValue &value, QByteArray *decoded)
{
    if (!value.isString()) return false;
    const QByteArray encoded = value.toString().toLatin1();
    const auto result = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.toBase64() != encoded) return false;
    *decoded = result.decoded;
    return true;
}
bool utf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(bytes);
    Q_UNUSED(decoded);
    return !decoder.hasError();
}
bool validBase(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.authority().contains(QLatin1Char('@'))
        && url.port(443) == 443 && !url.hasQuery() && !url.hasFragment()
        && (url.path().isEmpty() || url.path() == QLatin1String("/"));
}
}

QByteArray TankoyomiAddonStore::productionPublicKey()
{
    return QByteArray::fromHex(kTankoyomiAddonPublicKeyHex);
}

TankoyomiAddonStore::TankoyomiAddonStore(QUrl workerBase, QObject *parent)
    : TankoyomiAddonStore(std::move(workerBase), productionPublicKey(),
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + QStringLiteral("/tankoyomi/signed-addons-v1"), nullptr, parent)
{
}

TankoyomiAddonStore::TankoyomiAddonStore(QUrl workerBase, QByteArray publicKey, QString cacheRoot,
                                       QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_workerBase(std::move(workerBase)), m_publicKey(std::move(publicKey)),
      m_cacheRoot(std::move(cacheRoot)), m_network(network ? network : new QNetworkAccessManager(this))
{
}

bool TankoyomiAddonStore::verifyEnvelope(const QByteArray &envelope, const QString &providerId,
                                       const QByteArray &publicKey, VerifiedAddon *addon, QString *error)
{
    if (addon) *addon = {};
    const QString host = hostFor(providerId);
    if (host.isEmpty() || envelope.size() > kEnvelopeLimit || !utf8(envelope))
        return reject(error, QStringLiteral("Invalid Tankoyomi addon envelope"));
    const auto root = QJsonDocument::fromJson(envelope).object();
    if (root.value(QStringLiteral("formatVersion")) != QJsonValue(1)
        || root.value(QStringLiteral("keyId")) != QLatin1String("tankoyomi-addon-2026-10"))
        return reject(error, QStringLiteral("Unsupported Tankoyomi addon signature format"));
    QByteArray payload, signature;
    if (!decodeBase64(root.value(QStringLiteral("payload")), &payload)
        || !decodeBase64(root.value(QStringLiteral("signature")), &signature))
        return reject(error, QStringLiteral("Invalid Tankoyomi addon encoding"));
    QString trustError;
    if (!Colosseum::Update::verifyEd25519Raw(QByteArray(kDomain) + payload, signature, publicKey, &trustError))
        return reject(error, QStringLiteral("Tankoyomi addon signature verification failed"));
    if (!utf8(payload)) return reject(error, QStringLiteral("Invalid Tankoyomi addon payload encoding"));
    const auto data = QJsonDocument::fromJson(payload).object();
    if (data.value(QStringLiteral("contractVersion")) != QJsonValue(1)
        || data.value(QStringLiteral("addonId")) != QLatin1String("colosseum.well.tankoyomi")
        || data.value(QStringLiteral("language")) != QLatin1String("es")
        || data.value(QStringLiteral("providerId")) != providerId)
        return reject(error, QStringLiteral("Tankoyomi addon identity mismatch"));
    const auto hosts = data.value(QStringLiteral("allowedHosts")).toArray();
    if (hosts.size() != 1 || hosts.at(0) != host)
        return reject(error, QStringLiteral("Tankoyomi addon host permissions rejected"));
    const QJsonValue revisionValue = data.value(QStringLiteral("revision"));
    const double revision = revisionValue.toDouble();
    if (!revisionValue.isDouble() || !std::isfinite(revision) || revision < 1
        || revision > 9007199254740991.0 || std::floor(revision) != revision)
        return reject(error, QStringLiteral("Invalid Tankoyomi addon revision"));
    QByteArray script;
    if (!decodeBase64(data.value(QStringLiteral("scriptBase64")), &script)
        || script.isEmpty() || script.size() > kScriptLimit || !utf8(script))
        return reject(error, QStringLiteral("Invalid Tankoyomi addon script"));
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex());
    if (data.value(QStringLiteral("scriptSha256")) != hash)
        return reject(error, QStringLiteral("Tankoyomi addon script digest mismatch"));
    if (addon) *addon = {script, {host}, static_cast<qint64>(revision)};
    return true;
}

void TankoyomiAddonStore::load(const QString &providerId, Completion completion)
{
    if (hostFor(providerId).isEmpty() || !validBase(m_workerBase) || !m_network) {
        QTimer::singleShot(0, this, [completion] { completion({}, {}, QStringLiteral("Invalid Tankoyomi addon delivery configuration")); });
        return;
    }
    const QString path = QDir(m_cacheRoot).filePath(providerId + QStringLiteral(".json"));
    VerifiedAddon cached;
    QFile file(path);
    bool haveCache = file.open(QIODevice::ReadOnly) && file.size() <= kEnvelopeLimit
        && verifyEnvelope(file.readAll(), providerId, m_publicKey, &cached, nullptr);
    QNetworkRequest request(m_workerBase.resolved(QUrl(QStringLiteral("/addons/v1/es/") + providerId + QStringLiteral(".json"))));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    auto *reply = m_network->get(request);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->read(qMax<qint64>(0, kEnvelopeLimit + 1 - bytes->size())));
        if (bytes->size() > kEnvelopeLimit) reply->abort();
    });
    QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, providerId, path, cached, haveCache, completion] {
        bytes->append(reply->read(qMax<qint64>(0, kEnvelopeLimit + 1 - bytes->size())));
        const bool transportOk = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().isEmpty()
            && bytes->size() <= kEnvelopeLimit;
        reply->deleteLater();
        VerifiedAddon fresh;
        QString error = QStringLiteral("Tankoyomi signed addon update unavailable");
        bool verified = transportOk && verifyEnvelope(*bytes, providerId, m_publicKey, &fresh, &error);
        if (verified && haveCache && (fresh.revision < cached.revision
            || (fresh.revision == cached.revision && fresh.script != cached.script))) {
            verified = false;
            error = QStringLiteral("Tankoyomi addon revision rollback or collision rejected");
        }
        if (!verified) {
            if (haveCache) {
                qWarning() << "[tankoyomi] signed addon refresh rejected; using verified cache" << providerId << cached.revision;
                completion(cached.script, cached.allowedHosts, {});
            } else completion({}, {}, error);
            return;
        }
        QDir().mkpath(m_cacheRoot);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(*bytes) != bytes->size() || !output.commit()) {
            completion({}, {}, QStringLiteral("Tankoyomi verified addon cache write failed"));
            return;
        }
        qInfo() << "[tankoyomi] verified signed device addon loaded" << providerId << fresh.revision;
        completion(fresh.script, fresh.allowedHosts, {});
    });
}
