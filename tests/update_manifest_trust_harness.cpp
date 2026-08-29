#include "update/UpdateManifest.h"
#include "update/UpdateTrust.h"
#include "update/UpdateTestPublicKey.h"

#include <QByteArray>
#include <QBuffer>
#include <QCryptographicHash>

#include <cstdlib>
#include <iostream>

using namespace Colosseum::Update;

static void require(bool ok, const char* message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QByteArray hex(const char* value)
{
    return QByteArray::fromHex(value);
}

static QByteArray replaced(const QByteArray& input, const char* before, const char* after)
{
    QByteArray output = input;
    output.replace(before, after);
    return output;
}

int main()
{
    const QByteArray publicKey = hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const QByteArray signature = hex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    QString error;
    const QByteArray expectedTestKey(
        reinterpret_cast<const char*>(kUpdateTestPublicKey.data()),
        static_cast<qsizetype>(kUpdateTestPublicKey.size()));
    require(QByteArray(embeddedUpdatePublicKey().data(), embeddedUpdatePublicKey().size())
                == expectedTestKey,
            "test build selects the test public key");
    require(verifyEd25519Raw({}, signature, publicKey, &error), "RFC 8032 vector verifies");

    QByteArray mutated = signature;
    mutated[0] = static_cast<char>(mutated.at(0) ^ 0x01);
    require(!verifyEd25519Raw({}, mutated, publicKey, &error), "signature mutation rejected");
    require(!verifyEd25519Raw({}, signature.left(63), publicKey, &error), "short signature rejected");
    require(!verifyEd25519Raw({}, signature, publicKey.left(31), &error), "short key rejected");

    QBuffer hashBuffer;
    hashBuffer.setData("update");
    require(hashBuffer.open(QIODevice::ReadOnly), "hash fixture opens");
    require(sha256(&hashBuffer, &error) == QCryptographicHash::hash("update", QCryptographicHash::Sha256),
            "stream SHA-256 matches Qt oracle");

    const QByteArray valid = R"json({
      "schemaVersion": 1,
      "version": "1.1.0",
      "tag": "v1.1.0",
      "eyebrow": "A NEW CHAPTER IS READY",
      "title": "Colosseum 1.1",
      "summary": "The house keeps itself current.",
      "installer": {"asset": "Colosseum-1.1.0-setup.exe", "size": 123, "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
      "minimumUpdaterVersion": "1.1.0",
      "notesUrl": "https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.0",
      "highlights": [{"kind": "feature", "section": "COLOSSEUM", "title": "Fresh", "body": "Ready."}],
      "artwork": []
    })json";
    require(parseManifest(valid, &error).has_value(), "valid schema parses after trust seam");

    const QList<QByteArray> invalid = {
        replaced(valid, "1.1.0", "1.1"),
        replaced(valid, "v1.1.0", "v1.2.0"),
        replaced(valid, "https://github.com", "http://github.com"),
        replaced(valid, "\"feature\"", "\"javascript\""),
        replaced(valid, "\"feature\"", "\"qml\""),
        replaced(valid, "\"feature\"", "\"html\""),
        replaced(valid, "\"body\": \"Ready.\"", "\"body\": \"Ready.\", \"javascript\": \"alert(1)\""),
        replaced(valid, "\"body\": \"Ready.\"", "\"body\": \"Ready.\", \"qml\": \"Item {}\""),
        replaced(valid, "\"body\": \"Ready.\"", "\"body\": \"Ready.\", \"html\": \"<b>bad</b>\""),
        replaced(valid, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                 "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"),
        replaced(valid, "\"artwork\": []", "\"artwork\": [{\"asset\": \"feature.png\", \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}, {\"asset\": \"feature.png\", \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"} ]"),
        replaced(valid, "\"artwork\": []", "\"artwork\": [{\"asset\": \"foo/bar.png\", \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]"),
        replaced(valid, "\"size\": 123", "\"size\": 9223372036854775808"),
        replaced(valid, "kingoftheseas56/Colosseum", "another-owner/another-repo")
    };
    for (const QByteArray& invalidJson : invalid)
        require(!parseManifest(invalidJson, &error).has_value(), "invalid manifest rejected");

    std::cout << "UPDATE_MANIFEST_TRUST_OK\n";
}
