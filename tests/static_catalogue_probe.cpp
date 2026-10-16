// Reusable local publication probe for Phase 2 measurements and later parity.
// No live endpoints, credentials, application profile or visible GUI.
#include "engine/StaticCatalogueResolver.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <iostream>
#include <functional>
#include <algorithm>

int staticCatalogueProbe(const QString &publication, const QString &casesPath,
                         const QString &evidence) {
    const bool parity = casesPath.endsWith(".jsonl");
    const auto require = [](bool ok, const QString &why) {
        if (!ok) {
            std::cerr << "FAIL " << why.toStdString() << '\n';
            std::exit(1);
        }
    };
    const auto until = [&](const std::function<bool()> &done) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        require(done(), "probe async timeout");
    };
    const auto read = [&](const QString &path) {
        QFile f(path);
        require(f.open(QIODevice::ReadOnly), "probe input: " + path);
        return f.readAll();
    };
    QFile input(casesPath);
    require(input.open(QIODevice::ReadOnly), "cases input");
    const auto cases = parity ? QJsonArray() : QJsonDocument::fromJson(input.readAll()).array();
    require(parity || !cases.isEmpty(), "nonempty cases");
    QTemporaryDir profile;
    require(profile.isValid(), "isolated cache");
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "local publication server");
    const QUrl origin(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    int requests = 0, workerRequests = 0;
    qint64 transferred = 0;
    bool offline = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *s = server.nextPendingConnection();
            auto buffer = QSharedPointer<QByteArray>::create();
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            QObject::connect(s, &QTcpSocket::readyRead, s, [&, s, buffer] {
                buffer->append(s->readAll());
                if (!buffer->contains("\r\n\r\n"))
                    return;
                s->disconnect(SIGNAL(readyRead()));
                ++requests;
                const auto path = QString::fromUtf8(buffer->split(' ').value(1));
                if (path.startsWith("/v1/"))
                    ++workerRequests;
                if (offline) {
                    s->disconnectFromHost();
                    return;
                }
                QFile file(QDir(publication).filePath(path.mid(1)));
                QByteArray body;
                int status = 200;
                if (path.contains("..") ||
                    !(path.startsWith("/catalogue/") || path.startsWith("/assets/content/")) ||
                    !file.open(QIODevice::ReadOnly) || file.size() > 25 * 1024 * 1024)
                    status = 404;
                else
                    body = file.readAll();
                transferred += body.size();
                s->write("HTTP/1.1 " + QByteArray::number(status) +
                         " Fixture\r\nContent-Type: application/json\r\nContent-Length: " +
                         QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        }
    });
    QJsonArray rows;
    QNetworkAccessManager parityNam;
    auto *parityResolver = StaticCatalogueResolver::shared(&parityNam, origin, {}, &parityNam);
    QJsonObject groups;
    int parityPassed = 0, equalResults = 0, expectedResourceFailures = 0;
    if (parity) {
        while (!input.atEnd()) {
            const auto raw = input.readLine(8 * 1024 * 1024);
            require(raw.endsWith('\n'), "bounded JSONL case");
            const auto spec = QJsonDocument::fromJson(raw).object();
            const auto dataset = spec.value("dataset").toString();
            const auto method = spec.value("method").toString();
            bool done = false;
            StaticCatalogueResolver::Outcome out;
            parityResolver->resolve(dataset, method, spec.value("args").toArray().toVariantList(),
                                    &server, [&](auto result) { out = result; done = true; });
            until([&] { return done; });
            const bool fallback = spec.value("notPrepared").toBool();
            const QString expectedFailure = spec.value("expectedFailure").toString();
            const auto label = QString::number(parityPassed) + ":" + spec.value("label").toString();
            require(out.status == (!expectedFailure.isEmpty() ? StaticCatalogueResolver::Status::Failure
                                    : fallback ? StaticCatalogueResolver::Status::NotPrepared
                                            : StaticCatalogueResolver::Status::Prepared),
                    label + " status " + out.error);
            if (!expectedFailure.isEmpty()) {
                require(out.error == expectedFailure, label + " resource boundary " + out.error);
                ++expectedResourceFailures;
            } else if (!fallback && out.result != spec.value("expected")) {
                QFile failed(evidence + ".mismatch.json");
                require(failed.open(QIODevice::WriteOnly), "mismatch evidence");
                failed.write(QJsonDocument(QJsonObject{{"case", spec}, {"actual", out.result}}).toJson());
                require(false, label + " native/TypeScript mismatch");
            } else if (!fallback) ++equalResults;
            const auto group = spec.value("group").toString();
            groups[group] = groups.value(group).toInt() + 1;
            if (++parityPassed % 1000 == 0)
                std::cout << "PARITY " << parityPassed << std::endl;
        }
        require(parityPassed > 0 && workerRequests == 0, "native parity without Worker");
        QFile output(evidence);
        require(output.open(QIODevice::WriteOnly), "parity report");
        output.write(QJsonDocument(QJsonObject{{"passed", parityPassed}, {"groups", groups},
            {"equalResults", equalResults}, {"expectedResourceFailures", expectedResourceFailures},
            {"workerRequests", workerRequests}, {"staticRequests", requests},
            {"staticBytes", transferred}, {"deployed", false}}).toJson());
        std::cout << "PASS native parity " << parityPassed << std::endl;
        return 0;
    }
    int caseNumber = 0;
    for (const auto &v : cases) {
        const auto spec = v.toObject();
        const auto dataset = spec.value("dataset").toString(),
                   method = spec.value("method").toString();
        const auto args = spec.value("args").toArray().toVariantList();
        const auto expected = spec.value("expected");
        const bool notPrepared = spec.value("notPrepared").toBool();
        QNetworkAccessManager nam;
        const auto cache = profile.filePath(QString::number(caseNumber++));
        auto *r = StaticCatalogueResolver::shared(&nam, origin, cache, &nam);
        for (const auto &mode :
             {QStringLiteral("cold"), QStringLiteral("warm"), QStringLiteral("offline-restart")}) {
            QObject offlineOwner;
            auto *resolver =
                mode == "offline-restart"
                    ? StaticCatalogueResolver::shared(nullptr, origin, cache, &offlineOwner)
                    : r;
            const int before = requests;
            qint64 beforeBytes = transferred;
            bool done = false;
            StaticCatalogueResolver::Outcome out;
            QElapsedTimer clock;
            clock.start();
            resolver->resolve(dataset, method, args, &server, [&](auto result) {
                out = result;
                done = true;
            });
            until([&] { return done; });
            require(out.status == (notPrepared ? StaticCatalogueResolver::Status::NotPrepared
                                               : StaticCatalogueResolver::Status::Prepared),
                    dataset + "/" + method + " " + mode + " " + out.error);
            if (!notPrepared)
                require(out.result == expected,
                        dataset + "/" + method + " result mismatch " + mode);
            if (mode != "cold")
                require(requests == before, "warm/offline performs no HTTP");
            rows.append(QJsonObject{{"dataset", dataset},
                                    {"method", method},
                                    {"mode", mode},
                                    {"requestFiles", requests - before},
                                    {"networkBytes", transferred - beforeBytes},
                                    {"wallMs", clock.nsecsElapsed() / 1e6},
                                    {"metrics", QJsonObject::fromVariantMap(out.metrics)},
                                    {"notPrepared", notPrepared},
                                    {"pass", true}});
        }
    }
    // This is the largest *artwork* file, which the resolver deliberately doesn't
    // parse. Measure native QFile/hash and loopback HTTP separately from JSON loads.
    const auto entry =
        QJsonDocument::fromJson(read(QDir(publication).filePath("catalogue/current.json")))
            .object();
    const auto index =
        QJsonDocument::fromJson(
            read(QDir(publication)
                     .filePath(entry.value("index").toObject().value("path").toString().mid(1))))
            .object();
    // These selectors are present in the immutable real publication's list buckets.
    // Sample routine Movies and Discover paths, not synthetic generated rows.
    const QJsonArray routineCases{
        QJsonObject{{"label", "imdb-titleCatalog"},
                    {"dataset", "imdb"}, {"method", "titleCatalog"},
                    {"args", QJsonArray{QJsonObject{{"genre", "Thriller"}, {"type", "movie"}}, 0, 24}}},
        QJsonObject{{"label", "mal-discover"},
                    {"dataset", "mal"}, {"method", "discoverPageFiltered"},
                    {"args", QJsonArray{"popular",
                        QJsonArray{QJsonObject{{"axis", "demographic"}, {"key", "seinen"}}},
                        false, 0, 24}}},
        QJsonObject{{"label", "comics-discover"},
                    {"dataset", "comics"}, {"method", "discoverPageFiltered"},
                    {"args", QJsonArray{"popular",
                        QJsonArray{QJsonObject{{"axis", "publisher"}, {"key", "marvel knights"}}},
                        false, 0, 24}}}};
    QJsonArray routineRows;
    for (const auto &caseValue : routineCases) {
        const auto spec = caseValue.toObject();
        const auto ds = spec.value("dataset").toString();
        const auto method = spec.value("method").toString();
        const auto args = spec.value("args").toArray().toVariantList();
        const auto label = spec.value("label").toString();
        QNetworkAccessManager routineNam;
        QObject diskOwner;
        const auto cache = profile.filePath("routine-" + label);
        auto *warmResolver = StaticCatalogueResolver::shared(
            &routineNam, origin, cache, &routineNam);
        auto *diskResolver = static_cast<StaticCatalogueResolver *>(nullptr);
        QJsonValue expected;
        for (const auto &mode : {QStringLiteral("cold"), QStringLiteral("warm"),
                                QStringLiteral("offline-restart")}) {
            if (mode == "offline-restart")
                diskResolver = StaticCatalogueResolver::shared(nullptr, origin, cache, &diskOwner);
            auto *resolver = mode == "offline-restart" ? diskResolver : warmResolver;
            const int before = requests;
            const qint64 beforeBytes = transferred;
            bool done = false;
            StaticCatalogueResolver::Outcome out;
            QElapsedTimer clock;
            clock.start();
            resolver->resolve(ds, method, args, &server, [&](auto result) {
                out = result;
                done = true;
            });
            until([&] { return done; });
            require(out.status == StaticCatalogueResolver::Status::Prepared,
                    "real publication routine selector must be prepared: " + label);
            if (mode == "cold") {
                expected = out.result;
                const auto values = expected.isArray() ? expected.toArray()
                    : expected.toObject().value("items").toArray();
                require(!values.isEmpty() && requests > before,
                        "real publication routine selector must return published rows");
            } else
                require(out.result == expected && requests == before,
                        "routine warm/offline selector exact parity and zero HTTP: " + label);
            routineRows.append(QJsonObject{
                {"label", label}, {"mode", mode}, {"wallMs", clock.nsecsElapsed() / 1e6},
                {"requestFiles", requests - before}, {"networkBytes", transferred - beforeBytes},
                {"metrics", QJsonObject::fromVariantMap(out.metrics)}, {"pass", true}});
        }
    }
    // Report inspected directory selection alongside measured resolver events.
    // Transfer counts below come from real loaded files, never the bucket model.
    const auto imdbFacts = index.value("facts").toObject().value("imdb").toObject();
    const auto hashed = imdbFacts.value("hashBuckets").toObject();
    const auto hashPaths = hashed.value("paths").toObject();
    auto addActualIO = [](QJsonObject row) {
        const auto m = row.value("metrics").toObject();
        row["actualIO"] = QJsonObject{
            {"networkFiles", m.value("networkFiles")}, {"networkBytes", m.value("networkBytes")},
            {"diskFiles", m.value("diskFiles")}, {"diskBytes", m.value("diskBytes")},
            {"memoryFiles", m.value("memoryFiles")}, {"sharedFiles", m.value("sharedFiles")},
            {"logicalFiles", m.value("logicalFiles")},
            {"loopbackRequests", row.value("requestFiles")},
            {"loopbackBytes", row.value("networkBytes")},
            {"freshDistinctOfflineOwner", row.value("mode") == "offline-restart"}};
        row["phaseCounters"] = QJsonObject{
            {"evaluationRuns", m.value("evaluationRuns")}, {"evaluationMs", m.value("evaluationMs")},
            {"validatedFactsTuples", m.value("validatedFactsTuples")},
            {"bucketSelectedPrefixes", m.value("bucketSelectedPrefixes")},
            {"bucketUniquePaths", m.value("bucketUniquePaths")},
            {"bucketAdmissions", m.value("bucketAdmissions")},
            {"bucketPeakPending", m.value("bucketPeakPending")},
            {"bucketPrefetchWaves", m.value("bucketPrefetchWaves")}};
        return row;
    };
    for (qsizetype n = 0; n < rows.size(); ++n) {
        auto row = addActualIO(rows.at(n).toObject());
        const auto spec = cases.at(n / 3).toObject();
        if (spec.value("dataset") == "imdb" && spec.value("method") == "titleFacts") {
            const auto argArray = spec.value("args").toArray();
            const auto ids = argArray.isEmpty() ? QJsonArray() : argArray.at(0).toArray();
            QSet<QString> selectedPrefixes, selectedPaths;
            for (const auto &idValue : ids) {
                if (!idValue.isString()) continue;
                const auto id = idValue.toString();
                if (!QRegularExpression(QStringLiteral("^tt[0-9]+$")).match(id).hasMatch())
                    continue;
                if (!hashed.isEmpty()) {
                    const auto prefix = QString::fromLatin1(
                        QCryptographicHash::hash(id.toUtf8(),
                            QCryptographicHash::Sha256).toHex().left(2));
                    selectedPrefixes.insert(prefix);
                    selectedPaths.insert(hashPaths.value(prefix).toString());
                } else {
                    for (const auto &blockValue : imdbFacts.value("blocks").toArray()) {
                        const auto block = blockValue.toObject();
                        if (id >= block.value("first").toString() &&
                            id <= block.value("last").toString())
                            selectedPaths.insert(block.value("path").toString());
                    }
                }
            }
            auto keys = selectedPrefixes.values();
            std::sort(keys.begin(), keys.end());
            QJsonArray ordered;
            for (const auto &key : keys) ordered.append(key);
            row["directorySelection"] = QJsonObject{
                {"mode", hashed.isEmpty() ? "aligned-v1" : "hashBuckets-v1"},
                {"advertisedEntries", hashPaths.size()},
                {"selectedPrefixes", ordered},
                {"selectedPrefixCount", selectedPrefixes.size()},
                {"uniquePaths", selectedPaths.size()}};
        }
        rows[n] = row;
    }
    for (qsizetype n = 0; n < routineRows.size(); ++n)
        routineRows[n] = addActualIO(routineRows.at(n).toObject());

    QFileInfo largest;
    const auto artwork =
        QDir(QDir(publication).filePath("assets/content")).entryInfoList(QDir::Files);
    for (const auto &f : artwork)
        if (f.size() > largest.size())
            largest = f;
    QJsonObject big;
    if (largest.exists()) {
        QElapsedTimer clock;
        clock.start();
        const auto bytes = read(largest.absoluteFilePath());
        const double diskMs = clock.nsecsElapsed() / 1e6;
        clock.restart();
        const auto hash = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const double hashMs = clock.nsecsElapsed() / 1e6;
        require(hash == largest.completeBaseName(), "largest artwork fingerprint");
        QNetworkAccessManager nam;
        QNetworkRequest request(origin.resolved(QUrl("/assets/content/" + largest.fileName())));
        request.setTransferTimeout(15000);
        clock.restart();
        auto *reply = nam.get(request);
        QByteArray body;
        bool done = false;
        QObject::connect(reply, &QNetworkReply::readyRead, reply,
                         [&] { body += reply->readAll(); });
        QObject::connect(reply, &QNetworkReply::finished, reply, [&] {
            body += reply->readAll();
            done = true;
        });
        until([&] { return done; });
        const double networkMs = clock.nsecsElapsed() / 1e6;
        require(reply->error() == QNetworkReply::NoError && body == bytes,
                "largest loopback transfer bytes");
        reply->deleteLater();
        big = {{"bytes", bytes.size()},       {"path", "/assets/content/" + largest.fileName()},
               {"fileReadMs", diskMs},        {"sha256Ms", hashMs},
               {"loopbackHttpMs", networkMs}, {"internetLatencyQualified", false}};
    }
    QFile output(evidence);
    require(output.open(QIODevice::WriteOnly), "probe report output");
    output.write(QJsonDocument(QJsonObject{{"cases", cases.size()},
                                           {"checks", rows.size()},
                                           {"measurements", rows},
                                           {"routineSamples", routineRows},
                                           {"workerRequests", workerRequests},
                                           {"largestArtwork", big},
                                           {"sourceRevision", index.value("sourceRevision")},
                                           {"publication", entry.value("revision")},
                                           {"remoteRequests", 0},
                                           {"deployed", false}})
                     .toJson());
    std::cout << "PASS publication probe " << cases.size() << " cases / " << rows.size()
              << " cold-warm-offline checks\n";
    return 0;
}
