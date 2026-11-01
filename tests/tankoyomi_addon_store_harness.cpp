#include "engine/TankoyomiAddonStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>
#include <openssl/evp.h>
#include <cstring>
#include <iostream>

namespace {
// RFC 8032 test seed, deliberately public; never a production signing key.
const auto seed = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
QByteArray publicKey;
QByteArray script = "globalThis.TankoyomiProvider = {searchSeries:function(){return [];}};";
int failures = 0;
void check(bool condition, const char *label)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
QByteArray envelope(qint64 revision = 1, std::function<void(QJsonObject &)> mutate = {})
{
    QJsonObject data{{"contractVersion", 1}, {"addonId", "colosseum.well.tankoyomi"},
        {"language", "es"}, {"providerId", "mangasin"}, {"revision", double(revision)},
        {"allowedHosts", QJsonArray{"m440.in"}},
        {"scriptSha256", QString::fromLatin1(QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex())},
        {"scriptBase64", QString::fromLatin1(script.toBase64())}};
    if (mutate) mutate(data);
    const QByteArray payload = QJsonDocument(data).toJson(QJsonDocument::Compact);
    const QByteArray message = QByteArray("Colosseum Tankoyomi addon v1\n") + payload;
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(seed.constData()), size_t(seed.size()));
    publicKey.resize(32);
    size_t publicSize = 32;
    EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char *>(publicKey.data()), &publicSize);
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key);
    QByteArray signature(64, Qt::Uninitialized);
    size_t size = 64;
    EVP_DigestSign(context, reinterpret_cast<unsigned char *>(signature.data()), &size,
                   reinterpret_cast<const unsigned char *>(message.constData()), size_t(message.size()));
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return QJsonDocument(QJsonObject{{"formatVersion", 1}, {"keyId", "tankoyomi-addon-2026-10"},
        {"payload", QString::fromLatin1(payload.toBase64())},
        {"signature", QString::fromLatin1(signature.toBase64())}}).toJson(QJsonDocument::Compact);
}
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QByteArray body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request); setUrl(request.url()); setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        QTimer::singleShot(0, this, [this] {
            emit readyRead();
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "aborted"); setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *destination, qint64 maximum) override {
        const qint64 count = qMin(maximum, qint64(m_body.size() - m_offset));
        if (count <= 0) return -1;
        std::memcpy(destination, m_body.constData() + m_offset, size_t(count));
        m_offset += count; return count;
    }
private:
    QByteArray m_body;
    qsizetype m_offset = 0;
};
class Network final : public QNetworkAccessManager {
public:
    QByteArray body;
    int status = 200;
    QList<QUrl> urls;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        urls.append(request.url());
        return new Reply(request, body, status, this);
    }
};
struct Loaded { QByteArray script; QStringList hosts; QString error; bool delivered = false; };
Loaded load(TankoyomiAddonStore &store, const QString &id = "mangasin")
{
    Loaded loaded;
    QEventLoop loop;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    store.load(id, [&](QByteArray bytes, QStringList hosts, QString error) {
        loaded = {bytes, hosts, error, true}; loop.quit();
    });
    loop.exec();
    check(loaded.delivered, "load completed within deadline");
    return loaded;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray valid = envelope();
    TankoyomiAddonStore::VerifiedAddon addon;
    QString error;
    auto verify = [&](const QByteArray &value, const QString &id = "mangasin") {
        return TankoyomiAddonStore::verifyEnvelope(value, id, publicKey, &addon, &error);
    };
    check(verify(valid) && addon.script == script && addon.revision == 1, "valid signature accepted");
    auto tampered = QJsonDocument::fromJson(valid).object();
    tampered["payload"] = QString::fromLatin1(QByteArray("{}").toBase64());
    check(!verify(QJsonDocument(tampered).toJson()), "tampering rejected");
    check(!verify(valid, "lectorxd"), "cross-provider replay rejected");
    check(!verify(envelope(1, [](auto &data) { data["language"] = "en"; })), "wrong language rejected");
    check(!verify(envelope(1, [](auto &data) { data["allowedHosts"] = QJsonArray{"m440.in", "evil.example"}; })), "broader host permissions rejected");
    check(!verify(envelope(1, [](auto &data) { data["scriptSha256"] = QString(64, '0'); })), "script digest mismatch rejected");
    check(!verify(envelope(0)), "invalid revision rejected");
    check(!verify(envelope(1, [](auto &data) { data["scriptBase64"] = "/w=="; })), "invalid UTF-8 rejected");
    check(!verify(envelope(1, [](auto &data) {
        const QByteArray truncated("x\xc3");
        data["scriptBase64"] = QString::fromLatin1(truncated.toBase64());
        data["scriptSha256"] = QString::fromLatin1(QCryptographicHash::hash(truncated, QCryptographicHash::Sha256).toHex());
    })), "truncated UTF-8 script rejected even with a valid signature and digest");
    check(!verify(envelope(1, [](auto &data) { data["scriptBase64"] = QString::fromLatin1(QByteArray(300000, 'x').toBase64()); })), "oversized script rejected");
    tampered = QJsonDocument::fromJson(valid).object();
    tampered["signature"] = tampered["signature"].toString() + " ";
    check(!verify(QJsonDocument(tampered).toJson()), "noncanonical base64 rejected");

    QTemporaryDir cache;
    Network network;
    TankoyomiAddonStore store(QUrl("https://worker.example"), publicKey, cache.path(), &network);
    network.body = envelope(2);
    check(load(store).script == script, "verified remote addon loads and caches");
    check(network.urls.last() == QUrl("https://worker.example/addons/v1/es/mangasin.json"), "fixed addon route");
    network.status = 503;
    check(load(store).script == script, "offline cache reverified and used");
    network.status = 200; network.body = envelope(1);
    check(load(store).script == script, "rollback retains newer verified cache");
    QFile cached(cache.filePath("mangasin.json"));
    check(cached.open(QIODevice::ReadOnly) && cached.readAll() == envelope(2), "rollback does not overwrite cache");
    cached.close();
    network.body = QJsonDocument(tampered).toJson();
    check(load(store).script == script, "tampered refresh retains verified cache");
    check(cached.open(QIODevice::WriteOnly | QIODevice::Truncate), "open cache for tampering fixture");
    cached.write(network.body); cached.close();
    network.status = 503;
    check(!load(store).error.isEmpty(), "tampered cached signature never executes");
    const int before = network.urls.size();
    check(!load(store, "zonatmo").error.isEmpty() && network.urls.size() == before, "unsupported source never requests addon");
    network.status = 200; network.body = QByteArray(1100000, 'x');
    check(!load(store).error.isEmpty(), "oversized envelope rejected");
    std::cout << "Tankoyomi addon store failures: " << failures << '\n';
    return failures ? 1 : 0;
}
