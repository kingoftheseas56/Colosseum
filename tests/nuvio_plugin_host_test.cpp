#include "nuvio/NuvioNetwork.h"
#include "nuvio/NuvioTmdbCredentials.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

class NuvioPluginHostTest final : public QObject
{
    Q_OBJECT

private slots:
    void credentialOnlyAttachesToExactTmdbHttps();
    void nativeWorkerKeepsCredentialAndFilesystemOutOfJs();
};

void NuvioPluginHostTest::credentialOnlyAttachesToExactTmdbHttps()
{
    using Auth = NuvioTmdb::Authorization;
    const NuvioTmdb::Credential fake{QByteArray(32, 'f'), false};
    const auto query = QStringLiteral("/3/movie/550?api_key=") + QLatin1String(NuvioTmdb::placeholder);
    for (const auto &base : {"https://api.themoviedb.org.evil.example", "https://example.com",
                            "http://api.themoviedb.org", "https://api.themoviedb.org:444",
                            "https://user@api.themoviedb.org"}) {
        QNetworkRequest request{QUrl(QString::fromLatin1(base) + query)};
        const auto original = request.url();
        QCOMPARE(NuvioTmdb::authorize(request, fake), Auth::Unchanged);
        QCOMPARE(request.url(), original);
        QVERIFY(request.rawHeader("Authorization").isEmpty());
    }

    QNetworkRequest request{QUrl(QStringLiteral("https://api.themoviedb.org") + query)};
    QCOMPARE(NuvioTmdb::authorize(request, {}), Auth::Missing);
    request.setRawHeader("Referer", "https://other.example/");
    request.setRawHeader("Cookie", "untrusted=value");
    QCOMPARE(NuvioTmdb::authorize(request, fake), Auth::Attached);
    QCOMPARE(QUrlQuery(request.url()).queryItemValue("api_key"), QString::fromLatin1(fake.secret));
    QVERIFY(!request.hasRawHeader("Referer"));
    QVERIFY(!request.hasRawHeader("Cookie"));
    NuvioNetwork::pin(request, QHostAddress("8.8.8.8"));
    QCOMPARE(request.url().host(), QStringLiteral("8.8.8.8"));
    QCOMPARE(request.peerVerifyName(), QStringLiteral("api.themoviedb.org"));
    QCOMPARE(request.rawHeader("Host"), QByteArray("api.themoviedb.org"));
    QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));

    const NuvioTmdb::Credential bearer{QByteArray("synthetic.test.value"), true};
    QNetworkRequest tokenRequest{QUrl(QStringLiteral("https://api.themoviedb.org") + query)};
    QCOMPARE(NuvioTmdb::authorize(tokenRequest, bearer), Auth::Attached);
    QCOMPARE(tokenRequest.rawHeader("Authorization"), QByteArray("Bearer synthetic.test.value"));
    QVERIFY(!QUrlQuery(tokenRequest.url()).hasQueryItem("api_key"));
}

void NuvioPluginHostTest::nativeWorkerKeepsCredentialAndFilesystemOutOfJs()
{
    const auto worker = qEnvironmentVariable("COLOSSEUM_NUVIO_TEST_WORKER");
    QVERIFY(QFile::exists(worker));
    const QByteArray script = R"JS(
module.exports.getStreams = async () => {
    const expected = "__COLOSSEUM_TMDB_KEY__";
    if (TMDB_API_KEY !== expected || TMDB_ACCESS_TOKEN !== expected ||
        process.env.TMDB_API_KEY !== expected || process.env.TMDB_ACCESS_TOKEN !== expected ||
        Object.keys(process.env).length !== 2 || typeof process.cwd !== "undefined")
        throw new Error("wrong credential boundary");
    let fileDenied = false;
    try { require("fs"); } catch (_) { fileDenied = true; }
    if (!fileDenied) throw new Error("filesystem exposed");
    let loopbackDenied = false;
    try { await fetch("http://127.0.0.1/"); } catch (_) { loopbackDenied = true; }
    if (!loopbackDenied) throw new Error("loopback fetch exposed");
    const html = require("cheerio-without-node-native").load("<a href='/ok'>link</a>");
    if (html("a").attr("href") !== "/ok" || require("crypto-js").SHA256("abc").toString() !==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        throw new Error("module bridge");
    await new Promise(resolve => setTimeout(resolve, 5));
    return [{url: "https://8.8.8.8/test", name: "fixture"},
            {url: "http://127.0.0.1/", name: "loopback"},
            {url: "file:///secret", name: "file"}];
};
)JS";

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QFile file(temporary.filePath(QStringLiteral("fixture.js")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(script), script.size());
    file.close();

    QStringList args{QStringLiteral("--plugin"), file.fileName(),
                     QString::fromLatin1(QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex()),
                     QStringLiteral("550"), QStringLiteral("movie"), QStringLiteral("1"),
                     QStringLiteral("1"), temporary.path()};
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("TMDB_API_KEY", QStringLiteral("test-only-environment-sentinel"));
    process.setProcessEnvironment(env);
    process.start(worker, args, QIODevice::ReadOnly);
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    const auto stdoutBytes = process.readAllStandardOutput();
    QVERIFY(!stdoutBytes.contains("test-only-environment-sentinel"));
    const auto rows = QJsonDocument::fromJson(stdoutBytes).object().value("streams").toArray();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toObject().value("name").toString(), QStringLiteral("fixture"));

    args[2] = QString(64, QLatin1Char('0'));
    process.start(worker, args, QIODevice::ReadOnly);
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 1);
    QCOMPARE(QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("error").toString(),
             QStringLiteral("script_changed"));
}

QTEST_GUILESS_MAIN(NuvioPluginHostTest)
#include "nuvio_plugin_host_test.moc"
