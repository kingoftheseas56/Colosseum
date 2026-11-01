#include "engine/TankoyomiAddonStore.h"
#include "engine/TankoyomiScriptProvider.h"
#include "tankoyomi_fake_network.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <openssl/evp.h>

#include <functional>
#include <iostream>

namespace {
const QByteArray kTestSeed = QByteArray::fromHex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
int failures = 0;

void check(bool condition, const char *label)
{
    std::cerr << (condition ? "ok " : "FAIL ") << label << '\n';
    if (!condition) ++failures;
}

QString findRepositoryFile(const QString &relative)
{
    QDir directory(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = directory.filePath(relative);
        if (QFile::exists(candidate)) return candidate;
        if (!directory.cdUp()) break;
    }
    return {};
}

QByteArray testPublicKey()
{
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(kTestSeed.constData()),
        size_t(kTestSeed.size()));
    if (!key) return {};
    QByteArray publicKey(32, Qt::Uninitialized);
    size_t size = size_t(publicKey.size());
    const bool ok = EVP_PKEY_get_raw_public_key(
        key, reinterpret_cast<unsigned char *>(publicKey.data()), &size) == 1;
    EVP_PKEY_free(key);
    return ok && size == size_t(publicKey.size()) ? publicKey : QByteArray();
}

QByteArray signedEnvelope(const QString &providerId, const QString &host,
                          const QByteArray &script)
{
    const QByteArray digest = QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex();
    const QJsonObject payloadObject{
        {QStringLiteral("contractVersion"), 1},
        {QStringLiteral("addonId"), QStringLiteral("colosseum.well.tankoyomi")},
        {QStringLiteral("language"), QStringLiteral("es")},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("revision"), 1},
        {QStringLiteral("allowedHosts"), QJsonArray{host}},
        {QStringLiteral("scriptSha256"), QString::fromLatin1(digest)},
        {QStringLiteral("scriptBase64"), QString::fromLatin1(script.toBase64())}};
    const QByteArray payload = QJsonDocument(payloadObject).toJson(QJsonDocument::Compact);
    const QByteArray message = QByteArrayLiteral("Colosseum Tankoyomi addon v1\n") + payload;

    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(kTestSeed.constData()),
        size_t(kTestSeed.size()));
    if (!key) return {};
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context) {
        EVP_PKEY_free(key);
        return {};
    }
    QByteArray signature(64, Qt::Uninitialized);
    size_t signatureSize = size_t(signature.size());
    const bool ok = EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1
        && EVP_DigestSign(context,
                          reinterpret_cast<unsigned char *>(signature.data()), &signatureSize,
                          reinterpret_cast<const unsigned char *>(message.constData()),
                          size_t(message.size())) == 1;
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (!ok || signatureSize != size_t(signature.size())) return {};
    const QJsonObject envelope{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("keyId"), QStringLiteral("tankoyomi-addon-2026-10")},
        {QStringLiteral("payload"), QString::fromLatin1(payload.toBase64())},
        {QStringLiteral("signature"), QString::fromLatin1(signature.toBase64())}};
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

struct Outcomes {
    QHash<QString, QVariant> values;
    QHash<QString, QString> errors;
    bool timedOut = false;
};

Outcomes await(TankoyomiScriptProvider &provider, int expected,
               const std::function<void()> &start, int timeoutMs = 6000)
{
    Outcomes outcomes;
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
        outcomes.timedOut = true;
        loop.quit();
    });
    const auto maybeQuit = [&] {
        if (outcomes.values.size() + outcomes.errors.size() >= expected) loop.quit();
    };
    QObject::connect(&provider, &TankoyomiScriptProvider::resolved, &loop,
                     [&](const QString &token, const QVariant &value) {
        outcomes.values.insert(token, value);
        maybeQuit();
    });
    QObject::connect(&provider, &TankoyomiScriptProvider::failed, &loop,
                     [&](const QString &token, const QString &message) {
        outcomes.errors.insert(token, message);
        maybeQuit();
    });
    watchdog.start(timeoutMs);
    start();
    if (outcomes.values.size() + outcomes.errors.size() < expected) loop.exec();
    return outcomes;
}

QByteArray quoteJsonString(const QString &value)
{
    QJsonArray array;
    array.append(value);
    QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return json.mid(1, json.size() - 2);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString scriptPath = findRepositoryFile(
        QStringLiteral("extensions/tankoyomi/languages/es/mangasin.js"));
    QFile scriptFile(scriptPath);
    check(!scriptPath.isEmpty() && scriptFile.open(QIODevice::ReadOnly),
          "actual bundled MangaSin script is available to the harness");
    if (scriptPath.isEmpty() || !scriptFile.isOpen()) return 2;
    const QByteArray mangaSinScript = scriptFile.readAll();
    scriptFile.close();

    const QByteArray ciphertext = QByteArrayLiteral(
        "9LLch4qNWXtY2wJpHf3Hx0AeNxtMNmhfKyLPqqYB13T8eeoga18oMrUEWEMNKcRBes3o/C1rstznssrJ4iPe8vUWldTnF0sZlQcqjaxb2sy6gdZY7zPhYxCNZ4KSzjQX");
    const QJsonObject encrypted{{QStringLiteral("ct"), QString::fromLatin1(ciphertext)},
                                {QStringLiteral("iv"), QStringLiteral("0f5ae79b8ca2a5d6")},
                                {QStringLiteral("s"), QStringLiteral("0102030405060708")}};
    const QString envelopeText = QString::fromUtf8(
        QJsonDocument(encrypted).toJson(QJsonDocument::Compact));
    const QByteArray encryptedHtml = QByteArrayLiteral("<script>const UsaPoncho = ")
        + quoteJsonString(envelopeText) + QByteArrayLiteral(";</script>");
    const QByteArray pageUrl = QByteArrayLiteral("https://4.bp.blogspot.com/-chapter/s0/001.jpg");
    const QByteArray wrappedPage = QByteArrayLiteral("https://")
        + QUrl::toPercentEncoding(QString::fromLatin1(pageUrl)).toBase64();
    const QByteArray pagesHtml = QByteArrayLiteral("<div id=\"all\"><img class=\"img-responsive\" data-src=\"")
        + wrappedPage + QByteArrayLiteral("\"><img class=\"img-responsive\" src=\"https://s1.m440.in/002.webp\"></div>");

    QTemporaryDir cache;
    check(cache.isValid(), "temporary addon cache is ready");
    TankoyomiTest::Nam storeNetwork;
    storeNetwork.respond = [mangaSinScript](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
        TankoyomiTest::ReplySpec response;
        response.body = signedEnvelope(QStringLiteral("mangasin"), QStringLiteral("m440.in"), mangaSinScript);
        return response;
    };
    const QByteArray publicKey = testPublicKey();
    TankoyomiAddonStore store(QUrl(QStringLiteral("https://worker.example")), publicKey,
                              cache.path(), &storeNetwork);

    TankoyomiTest::Nam sourceNetwork;
    sourceNetwork.respond = [encryptedHtml, pagesHtml](const QNetworkRequest &request,
                                                        QNetworkAccessManager::Operation, int) {
        TankoyomiTest::ReplySpec response;
        const QString host = QString::fromLatin1(request.rawHeader("Host"));
        if (host != QLatin1String("m440.in")) {
            response.status = 403;
            response.body = QByteArrayLiteral("wrong logical host");
        } else if (request.url().path() == QLatin1String("/search")) {
            response.contentType = QByteArrayLiteral("application/json");
            response.body = QByteArrayLiteral("[{\"data\":\"solo-bug-player\",\"value\":\"Solo Bug Player\"}]");
        } else if (request.url().path() == QLatin1String("/manga/solo-bug-player")) {
            response.body = encryptedHtml;
        } else if (request.url().path() == QLatin1String("/manga/solo-bug-player/156-chp9g")) {
            response.body = pagesHtml;
        } else {
            response.status = 404;
        }
        return response;
    };
    const auto publicDns = [](const QString &, MangaImageHostResolver::LookupDone done) {
        done(QStringLiteral("93.184.216.34"));
    };
    TankoyomiScriptProvider mangaSin(QStringLiteral("mangasin"), QStringLiteral("es"),
                                     &store, &sourceNetwork, publicDns);
    check(mangaSin.isReady(), "device provider is ready for asynchronous signed loading");

    const Outcomes mangaOutcomes = await(mangaSin, 3, [&] {
        mangaSin.searchSeries(QStringLiteral("search"), QStringLiteral("solo"));
        mangaSin.getChapters(QStringLiteral("chapters"),
                             {{QStringLiteral("url"), QStringLiteral("https://m440.in/manga/solo-bug-player")}});
        mangaSin.getPages(QStringLiteral("pages"),
                          {{QStringLiteral("url"), QStringLiteral("https://m440.in/manga/solo-bug-player/156-chp9g")}});
    });
    check(!mangaOutcomes.timedOut && mangaOutcomes.errors.isEmpty()
              && mangaOutcomes.values.size() == 3,
          "queued MangaSin search, chapter, and page calls resolve from the signed bytes");
    const QVariantList search = mangaOutcomes.values.value(QStringLiteral("search")).toList();
    const QVariantList chapters = mangaOutcomes.values.value(QStringLiteral("chapters")).toList();
    const QVariantList pages = mangaOutcomes.values.value(QStringLiteral("pages")).toList();
    check(search.size() == 1 && search.first().toMap().value(QStringLiteral("id")).toString()
              == QLatin1String("solo-bug-player"),
          "actual MangaSin search parser runs against fixture JSON");
    check(chapters.size() == 1 && chapters.first().toMap().value(QStringLiteral("id")).toString()
              == QLatin1String("fixed-1")
              && chapters.first().toMap().value(QStringLiteral("number")).toDouble() == 156,
          "MangaSin decrypts the fixed OpenSSL salted AES-CBC vector and parses chapters");
    check(pages.size() == 2 && pages.at(0).toMap().value(QStringLiteral("url")).toString()
              == QLatin1String("https://4.bp.blogspot.com/-chapter/s0/001.jpg")
              && pages.at(1).toMap().value(QStringLiteral("url")).toString()
                     == QLatin1String("https://s1.m440.in/002.webp"),
          "MangaSin Base64 URL wrapper and direct page URLs are decoded in scoped context");
    check(storeNetwork.requests.size() == 1
              && storeNetwork.requests.first().url()
                     == QUrl(QStringLiteral("https://worker.example/addons/v1/es/mangasin.json")),
          "the provider makes one signed addon load while three first calls are queued");
    check(sourceNetwork.requests.size() == 3,
          "MangaSin fixture traffic stays on the bounded provider network path");
    for (const QNetworkRequest &request : sourceNetwork.requests) {
        check(request.rawHeader("Host") == QByteArrayLiteral("m440.in")
                  && request.peerVerifyName() == QStringLiteral("m440.in")
                  && request.rawHeader("Cookie").isEmpty()
                  && request.rawHeader("Authorization").isEmpty(),
              "source traffic keeps its logical host and carries no app credentials");
    }

    QFile lectorFile(findRepositoryFile(QStringLiteral("extensions/tankoyomi/languages/es/lectorxd.js")));
    check(lectorFile.open(QIODevice::ReadOnly), "actual LectorXD script is available");
    if (lectorFile.isOpen()) {
        const QByteArray lectorScript = lectorFile.readAll();
        QTemporaryDir lectorCache;
        TankoyomiTest::Nam lectorStoreNetwork;
        lectorStoreNetwork.respond = [lectorScript](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec response;
            response.body = signedEnvelope(QStringLiteral("lectorxd"), QStringLiteral("lectorxd.com"), lectorScript);
            return response;
        };
        TankoyomiAddonStore lectorStore(QUrl(QStringLiteral("https://worker.example")), publicKey,
                                        lectorCache.path(), &lectorStoreNetwork);
        TankoyomiTest::Nam lectorSourceNetwork;
        TankoyomiScriptProvider lector(QStringLiteral("lectorxd"), QStringLiteral("es"),
                                       &lectorStore, &lectorSourceNetwork, publicDns);
        const Outcomes unsupported = await(lector, 1, [&] {
            lector.searchSeries(QStringLiteral("unsupported"), QStringLiteral("manga"));
        });
        check(!unsupported.timedOut && unsupported.values.isEmpty()
                  && unsupported.errors.value(QStringLiteral("unsupported"))
                         .contains(QStringLiteral("title search is unsupported"))
                  && lectorSourceNetwork.requests.isEmpty(),
              "LectorXD preserves its explicit unsupported-search result without invented traffic");
        const QString seriesPath = QStringLiteral("/manhwa/streaming-de-la-guia-oculta-del-retornado");
        const QString seriesUrl = QStringLiteral("https://lectorxd.com") + seriesPath;
        lectorSourceNetwork.respond = [](const QNetworkRequest &request, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec response;
            response.body = request.url().path().contains(QStringLiteral("/leer/"))
                ? QByteArrayLiteral("<div id=\"reader-content\"><img class=\"page-image\" src=\"https://s1.cdnlxd.xyz/fixture/1.webp\"></div>")
                : QByteArrayLiteral("<astro-island component-url=\"/_astro/ChapterList.fixture.js\" props='{\"chapters\":[1,[[0,{\"chapter\":[0,20]}]]]}'>");
            return response;
        };
        const Outcomes parsed = await(lector, 2, [&] {
            lector.getChapters(QStringLiteral("astro"), {{"url", seriesUrl}});
            lector.getPages(QStringLiteral("pages"), {{"url", seriesUrl + QStringLiteral("/leer/20")}});
        });
        check(!parsed.timedOut && parsed.errors.isEmpty() && parsed.values.size() == 2
                  && parsed.values.value(QStringLiteral("astro")).toList().size() == 1
                  && parsed.values.value(QStringLiteral("pages")).toList().size() == 1,
              "actual LectorXD Astro chapters and reader pages parse in the device engine");
    }

    {
        const QByteArray budgetScript = QByteArrayLiteral(R"JS(
this.TankoyomiProvider = {
  searchSeries: function(ctx) {
    var count = 0;
    function next() {
      return ctx.fetchText('https://m440.in/test').then(function() {
        count++;
        return count < 41 ? next() : [];
      });
    }
    // A provider cannot turn a hard transport-budget violation into success.
    return next().catch(function() { return []; });
  }
};
)JS");
        QTemporaryDir budgetCache;
        TankoyomiTest::Nam budgetStoreNetwork;
        budgetStoreNetwork.respond = [budgetScript](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec response;
            response.body = signedEnvelope(QStringLiteral("mangasin"), QStringLiteral("m440.in"), budgetScript);
            return response;
        };
        TankoyomiAddonStore budgetStore(QUrl(QStringLiteral("https://worker.example")), publicKey,
                                        budgetCache.path(), &budgetStoreNetwork);
        TankoyomiTest::Nam budgetSourceNetwork;
        budgetSourceNetwork.respond = [](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            return TankoyomiTest::ReplySpec{};
        };
        TankoyomiScriptProvider budget(QStringLiteral("mangasin"), QStringLiteral("es"),
                                       &budgetStore, &budgetSourceNetwork, publicDns);
        const Outcomes limited = await(budget, 1, [&] {
            budget.searchSeries(QStringLiteral("budget"), QStringLiteral("x"));
        });
        std::cerr << "budget outcome: timeout=" << limited.timedOut << " values=" << limited.values.size()
                  << " requests=" << budgetSourceNetwork.requests.size() << " error="
                  << limited.errors.value(QStringLiteral("budget")).toStdString() << '\n';
        check(!limited.timedOut && limited.values.isEmpty()
                  && limited.errors.value(QStringLiteral("budget"))
                         .contains(QStringLiteral("40-request"))
                  && budgetSourceNetwork.requests.size() == 40,
              "device addon request budget stops the 41st metadata fetch");
    }

    {
        const QByteArray loopingScript = QByteArrayLiteral(
            "this.TankoyomiProvider = {searchSeries: function() { while (true) {} }};");
        QTemporaryDir timeoutCache;
        TankoyomiTest::Nam timeoutStoreNetwork;
        timeoutStoreNetwork.respond = [loopingScript](const QNetworkRequest &, QNetworkAccessManager::Operation, int) {
            TankoyomiTest::ReplySpec response;
            response.body = signedEnvelope(QStringLiteral("mangasin"), QStringLiteral("m440.in"), loopingScript);
            return response;
        };
        TankoyomiAddonStore timeoutStore(QUrl(QStringLiteral("https://worker.example")), publicKey,
                                         timeoutCache.path(), &timeoutStoreNetwork);
        TankoyomiTest::Nam timeoutSourceNetwork;
        TankoyomiScriptProvider timeoutProvider(QStringLiteral("mangasin"), QStringLiteral("es"),
                                                &timeoutStore, &timeoutSourceNetwork, publicDns);
        const Outcomes interrupted = await(timeoutProvider, 1, [&] {
            timeoutProvider.searchSeries(QStringLiteral("loop"), QStringLiteral("x"));
        }, 10000);
        std::cerr << "execution outcome: timeout=" << interrupted.timedOut << " values=" << interrupted.values.size()
                  << " error=" << interrupted.errors.value(QStringLiteral("loop")).toStdString() << '\n';
        check(!interrupted.timedOut && interrupted.values.isEmpty()
                  && interrupted.errors.value(QStringLiteral("loop"))
                         .contains(QStringLiteral("execution timed out"))
                  && timeoutSourceNetwork.requests.isEmpty(),
              "an infinite provider method is interrupted by the bounded execution deadline");
    }

    std::cerr << (failures ? "TANKOYOMI_DEVICE_RUNTIME_FAIL" : "TANKOYOMI_DEVICE_RUNTIME_OK") << '\n';
    return failures ? 1 : 0;
}
