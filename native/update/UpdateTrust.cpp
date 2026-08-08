#include "update/UpdateTrust.h"

#include "update/UpdatePublicKey.h"
#ifdef COLOSSEUM_UPDATE_TESTING
#include "update/UpdateTestPublicKey.h"
#endif

#include <QCryptographicHash>
#include <QIODevice>

#include <openssl/evp.h>

namespace Colosseum::Update {

QByteArrayView embeddedUpdatePublicKey()
{
#ifdef COLOSSEUM_UPDATE_TESTING
    return QByteArrayView(reinterpret_cast<const char*>(kUpdateTestPublicKey.data()),
                          static_cast<qsizetype>(kUpdateTestPublicKey.size()));
#else
    return QByteArrayView(reinterpret_cast<const char*>(kUpdatePublicKey.data()),
                          static_cast<qsizetype>(kUpdatePublicKey.size()));
#endif
}

bool verifyEd25519Raw(QByteArrayView message, QByteArrayView signature,
                      QByteArrayView publicKey, QString* error)
{
    if (publicKey.size() != 32) {
        if (error) *error = QStringLiteral("invalid_public_key_length");
        return false;
    }
    if (signature.size() != 64) {
        if (error) *error = QStringLiteral("invalid_signature_length");
        return false;
    }

    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(publicKey.data()),
        static_cast<std::size_t>(publicKey.size()));
    if (!key) {
        if (error) *error = QStringLiteral("public_key_creation_failed");
        return false;
    }

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) {
        EVP_PKEY_free(key);
        if (error) *error = QStringLiteral("verify_context_creation_failed");
        return false;
    }

    const unsigned char* messageData = message.size() == 0
        ? nullptr
        : reinterpret_cast<const unsigned char*>(message.data());
    const int initialized = EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key);
    const int result = initialized == 1
        ? EVP_DigestVerify(
              context,
              reinterpret_cast<const unsigned char*>(signature.data()),
              static_cast<std::size_t>(signature.size()),
              messageData,
              static_cast<std::size_t>(message.size()))
        : 0;

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (result != 1) {
        if (error) *error = QStringLiteral("signature_verification_failed");
        return false;
    }
    return true;
}

QByteArray sha256(QIODevice* device, QString* error)
{
    if (!device || !device->isOpen() || !device->isReadable()) {
        if (error) *error = QStringLiteral("hash_device_not_readable");
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(device)) {
        if (error) *error = QStringLiteral("hash_read_failed");
        return {};
    }
    return hash.result();
}

} // namespace Colosseum::Update
