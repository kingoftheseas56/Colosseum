#include "engine/StaticCatalogueResolver.h"
#include "engine/CatalogRemote.h"
#include "engine/TankobanCatalog.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <cstdlib>
#include <mutex>
static std::mutex traceMutex;
static QList<QJsonObject> traceRows;
static QtMessageHandler tracePreviousHandler = nullptr;
static void captureCatalogueTrace(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (message.startsWith("CATALOGUE_TRACE ")) {
        std::lock_guard<std::mutex> lock(traceMutex);
        traceRows.append(QJsonDocument::fromJson(message.mid(16).toUtf8()).object());
    }
    if (tracePreviousHandler) tracePreviousHandler(type, context, message);
    else std::cerr << message.toStdString() << '\n';
}
static void require(bool v, const char *why) {
    if (!v) {
        std::cerr << "FAIL " << why << '\n';
        std::exit(1);
    }
}
template <class F> static void until(F done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 15000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    require(done(), "bounded async completion");
}
static QByteArray json(const QJsonObject &v) {
    return QJsonDocument(v).toJson(QJsonDocument::Compact);
}
static QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
int staticCatalogueProbe(const QString &, const QString &, const QString &);
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 5 && (QString::fromLocal8Bit(argv[1]) == "--offline-landing"
                      || QString::fromLocal8Bit(argv[1]) == "--warm-landing")) {
        QNetworkAccessManager warmNam;
        const bool warm = QString::fromLocal8Bit(argv[1]) == "--warm-landing";
        CatalogRemote remote("imdb");
        remote.configure(warm ? &warmNam : nullptr, QUrl(QString::fromLocal8Bit(argv[2])),
                         QString::fromLocal8Bit(argv[3]));
        require(remote.available(), "offline landing owner is available from persisted cache");
        const auto bundle = remote.read("landing", {"movies"}, QVariantMap()).toMap();
        require(bundle.value("tab") == "movies" && !remote.pending(),
                "offline restart restores valid landing without fetching");
        const auto rows = remote.read("titleCatalog", {QVariantMap(), 0, 24}, QVariantList()).toList();
        require(!rows.isEmpty() && rows.first().toMap().value("title") ==
                    QString::fromLocal8Bit(argv[4]) && !remote.pending(),
                "offline landing child cache is immediately available");
        std::cout << "PASS separate-process landing offline restoration\n";
        return 0;
    }
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == "--offline") {
        auto *resolver = StaticCatalogueResolver::shared(
            nullptr, QUrl(QString::fromLocal8Bit(argv[2])), QString::fromLocal8Bit(argv[3]), &app);
        bool done = false;
        StaticCatalogueResolver::Outcome result;
        resolver->resolve("imdb", "titleCatalog", {QVariantMap(), 0, 24}, &app, [&](auto value) {
            result = value;
            done = true;
        });
        until([&] { return done; });
        require(result.status == StaticCatalogueResolver::Status::Prepared &&
                    result.result.toArray().at(0).toObject().value("title") ==
                        QString::fromLocal8Bit(argv[4]),
                "separate offline process reconstructs result only from verified files");
        std::cout << "PASS separate-process static offline restoration\n";
        return 0;
    }
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == "--publication")
        return staticCatalogueProbe(QString::fromLocal8Bit(argv[2]),
                                    QString::fromLocal8Bit(argv[3]),
                                    QString::fromLocal8Bit(argv[4]));
    QTemporaryDir temp;
    QNetworkAccessManager nam;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "fixture listens");
    const QUrl origin(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    QHash<QString, QByteArray> files;
    QString pub = hash("publication-one"), source(64, 'a'), leaf;
    int gets = 0, posts = 0;
    bool offline = false, holdLeaf = false, rotateMissing = false;
    QList<QPointer<QTcpSocket>> held;
    QVariantList measured;
    auto publish = [&](const QString &label, const QString &landingFault = QString()) {
        pub = hash(label.toUtf8());
        const QString prefix = "/catalogue/" + pub + "/";
        const QByteArray raw =
            QJsonDocument(QJsonArray{QJsonObject{{"title", label}}}).toJson(QJsonDocument::Compact);
        leaf = prefix + "shared/" + hash(raw) + ".json";
        files[leaf] = raw;
        QJsonObject child{{"method", "titleCatalog"},
                          {"args", QJsonArray{QJsonObject(), 0, 24}},
                          {"result", QJsonDocument::fromJson(raw).array()}};
        if (landingFault == "shape") child["result"] = QJsonObject();
        if (landingFault == "args") child["args"] = QJsonArray();
        QJsonArray queries{child};
        if (landingFault == "duplicate") queries.append(child);
        const auto today = QDateTime::currentDateTimeUtc().date();
        const auto landingRaw = json({{"version", 1}, {"dataset", "imdb"}, {"tab", "movies"},
            {"validFrom", today.addDays(-2).toString(Qt::ISODate)},
            {"validUntil", (landingFault == "date" ? today.addDays(-1) : today.addDays(1)).toString(Qt::ISODate)},
            {"queries", queries}});
        const auto landingPath = prefix + "shared/" + hash(landingRaw) + ".json";
        files[landingPath] = landingRaw;
        QJsonObject fixed{{"[\"imdb\",\"titleCatalog\",[{},0,24]]", leaf}};
        if (landingFault != "absent")
            fixed["[\"imdb\",\"landing\",[\"movies\"]]"] = landingPath;
        const QString tankKey = "[\"tankoban\",\"series\",[13]]";
        QJsonArray volumes;
        for (int n = 1; n <= 113; ++n)
            volumes.append(
                QJsonObject{{"number", QString::number(n)}, {"cover", ""}, {"name", ""}});
        const auto tankRaw = json(
            {{tankKey,
              QJsonObject{{"apiVersion", 1},
                          {"ready", true},
                          {"sourceHash", source},
                          {"seriesInfo", QJsonObject{{"volumeCount", 113}, {"countBasis", "mal"}}},
                          {"volumes", volumes}}}});
        const QString tankPath = prefix + "shared/" + hash(tankRaw) + ".json";
        files[tankPath] = tankRaw;
        QJsonObject index{{"version", 1},
                          {"schemaVersion", 1},
                          {"revision", pub},
                          {"sourceRevision", source},
                          {"lists", QJsonObject()},
                          {"listBuckets", QJsonObject()},
                          {"lookups", QJsonObject{{hash(tankKey.toUtf8()).left(2), tankPath}}},
                          {"search", QJsonObject()},
                          {"tankobanSourceHash", source},
                          {"fixed", fixed}};
        const auto bytes = json(index);
        const auto path = prefix + "index-" + hash(bytes) + ".json";
        files[path] = bytes;
        files["/catalogue/current.json"] =
            json({{"schemaVersion", 1},
                  {"revision", pub},
                  {"sourceRevision", source},
                  {"index",
                   QJsonObject{{"path", path}, {"sha256", hash(bytes)}, {"bytes", bytes.size()}}}});
    };
    QString factsPath;
    auto publishFacts = [&](const QString &label, const QString &fault = QString()) {
        publish(label);
        const QString prefix = "/catalogue/" + pub + "/";
        const double changedRating = label == "facts-two" ? 9.7 : 8.4;
        const bool nullable = fault.startsWith("nullable");
        const auto identity = [&](const QString &id, QJsonValue rating, QJsonValue votes, bool anime,
                                  int ordinal) {
            return QJsonObject{{"identity", QJsonObject{{"title", id}, {"rating", rating},
                                                       {"votes", votes}, {"isAnime", anime}}},
                               {"ordinal", ordinal}};
        };
        const QJsonArray sourceRows{
            QJsonArray{"tt0000001", QJsonValue(QJsonValue::Null), 0,
                       identity("tt0000001", changedRating, 100, false, 2)},
            QJsonArray{"tt0000002", QJsonValue(QJsonValue::Null), 0,
                       identity("tt0000002", 9.1, 220, true, 0)},
            QJsonArray{"tt0000003", QJsonValue(QJsonValue::Null), 0,
                       identity("tt0000003", nullable ? QJsonValue(QJsonValue::Null) : QJsonValue(7.5),
                                nullable ? QJsonValue(QJsonValue::Null) : QJsonValue(80), false, 1)}};
        const auto genericRaw = QJsonDocument(sourceRows).toJson(QJsonDocument::Compact);
        const QString genericPath = prefix + "shared/" + hash(genericRaw) + ".json";
        files[genericPath] = genericRaw;
        QJsonArray tuples{
            QJsonArray{"tt0000001", changedRating, 100, false, 2},
            QJsonArray{"tt0000002", 9.1, 220, true, 0},
            QJsonArray{"tt0000003", nullable ? QJsonValue(QJsonValue::Null) : QJsonValue(7.5),
                       nullable ? QJsonValue(QJsonValue::Null) : QJsonValue(80), false, 1}};
        if (fault == "corrupt-tuple")
            tuples[1] = QJsonArray{"tt0000002", 9.1, QStringLiteral("220"), true, 0};
        const auto factsRaw = QJsonDocument(tuples).toJson(QJsonDocument::Compact);
        factsPath = prefix + "shared/" + hash(factsRaw) + ".json";
        files[factsPath] = factsRaw;
        const auto entry = QJsonDocument::fromJson(files["/catalogue/current.json"]).object();
        const auto oldIndexPath = entry.value("index").toObject().value("path").toString();
        auto index = QJsonDocument::fromJson(files.value(oldIndexPath)).object();
        index["search"] = QJsonObject{{"imdb",
                           QJsonObject{{"keys", QJsonArray{QJsonObject{{"path", genericPath},
                                                                      {"first", "tt0000001"},
                                                                      {"last", "tt0000003"}}}},
                                       {"names", QJsonArray()}}}};
        if (fault != "absent" && fault != "nullable-legacy") {
            QJsonObject block{{"path", factsPath}, {"first", "tt0000001"},
                              {"last", "tt0000003"}};
            if (fault == "bad-declaration")
                block["first"] = "tt0000004";
            index["facts"] = fault == "absent-field"
                ? QJsonObject()
                : QJsonObject{{"imdb",
                              QJsonObject{{"version", fault == "bad-version" ? 2 : 1},
                                          {"blocks", fault == "empty-directory" ? QJsonArray()
                                                                        : QJsonArray{block}}}}};
        }
        const auto indexRaw = json(index);
        const auto path = prefix + "index-" + hash(indexRaw) + ".json";
        files[path] = indexRaw;
        files["/catalogue/current.json"] =
            json({{"schemaVersion", 1}, {"revision", pub}, {"sourceRevision", source},
                  {"index", QJsonObject{{"path", path}, {"sha256", hash(indexRaw)},
                                        {"bytes", indexRaw.size()}}}});
    };
    publish("publication-one");
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto *s = server.nextPendingConnection();
            auto data = QSharedPointer<QByteArray>::create();
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            QObject::connect(s, &QTcpSocket::readyRead, s, [&, s, data] {
                data->append(s->readAll());
                const int split = data->indexOf("\r\n\r\n");
                if (split < 0)
                    return;
                const auto header = data->left(split);
                int length = 0;
                for (const auto &line : header.split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                if (data->size() < split + 4 + length)
                    return;
                s->disconnect(SIGNAL(readyRead()));
                const QString path = QString::fromUtf8(header.split(' ').value(1));
                QByteArray body;
                int status = 200;
                if (header.startsWith("POST ")) {
                    ++posts;
                    if (path == "/v1/guest/session")
                        body = json({{"token", "fixture"}, {"expiresAt", "2099-01-01T00:00:00Z"}});
                    else {
                        const auto req = QJsonDocument::fromJson(data->mid(split + 4)).object();
                        body = json({{"apiVersion", 1},
                                     {"revision", source},
                                     {"method", path.mid(path.lastIndexOf('/') + 1)},
                                     {"result", QJsonArray{QJsonObject{{"title", "worker"}}}}});
                    }
                } else {
                    ++gets;
                    if (offline) {
                        s->disconnectFromHost();
                        return;
                    }
                    if (files.contains(path))
                        body = files[path];
                    else
                        status = 404;
                }
                if (rotateMissing && path == leaf) {
                    rotateMissing = false;
                    publish("publication-hot-update");
                    status = 404;
                    body.clear();
                }
                if (holdLeaf && path == leaf) {
                    held.append(s);
                    return;
                }
                const QByteArray etag = '"' + hash(body).toLatin1() + '"';
                if (path == "/catalogue/current.json" &&
                    header.contains("If-None-Match: " + etag)) {
                    status = 304;
                    body.clear();
                }
                s->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nETag: " + etag +
                         "\r\nContent-Type: application/json\r\nContent-Length: " +
                         QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        }
    });
    auto *r = StaticCatalogueResolver::shared(&nam, origin, temp.path(), &app);
    QObject::connect(r, &StaticCatalogueResolver::measured, &app,
                     [&](const QString &dataset, const QString &method, const QVariantMap &m) {
                         auto row = m;
                         row["dataset"] = dataset;
                         row["method"] = method;
                         measured.append(row);
                     });
    // Dedicated cache roots keep these counts independent of the legacy leaf tests.
    const QVariantList landingArgs{QStringLiteral("movies")};
    const QVariantList landingChildArgs{QVariantMap(), 0, 24};
    const auto landingCache = temp.filePath("landing/remote.json");
    {
        CatalogRemote landing("imdb");
        landing.configure(&nam, origin, landingCache);
        const int before = gets, beforePosts = posts;
        landing.read("landing", landingArgs, QVariantMap());
        until([&] { return !landing.pending(); });
        const auto state = landing.queryState("landing", landingArgs, QVariantMap());
        require(state.value("status") == "success" && state.value("cached").toBool(),
                "native landing completes with typed success");
        require(gets == before + 3 && posts == beforePosts,
                "cold landing fetches entry, index and exactly one bundle leaf");
        const auto childState = landing.queryState("titleCatalog", landingChildArgs, QVariantList());
        require(childState.value("status") == "success" && childState.value("cached").toBool(),
                "landing hydrates exact titleCatalog query cache");
        const int primedGets = gets;
        const auto rows = landing.read("titleCatalog", landingChildArgs, QVariantList()).toList();
        require(!rows.isEmpty() && rows.first().toMap().value("title") == "publication-one"
                    && !landing.pending() && gets == primedGets && posts == beforePosts,
                "primed individual query needs no GET, guest lease or Worker POST");
    } // Destructor flushes the complete cache before the separate-process restart.
    QProcess landingRestart;
    landingRestart.start(QCoreApplication::applicationFilePath(),
        {"--offline-landing", origin.toString(QUrl::FullyEncoded), landingCache, "publication-one"});
    require(landingRestart.waitForFinished(15000)
                && landingRestart.exitStatus() == QProcess::NormalExit && landingRestart.exitCode() == 0
                && landingRestart.readAllStandardOutput().contains("PASS separate-process landing offline restoration"),
            "landing offline restart completes cleanly");
    QProcess warmRestart;
    warmRestart.start(QCoreApplication::applicationFilePath(),
        {"--warm-landing", origin.toString(QUrl::FullyEncoded), landingCache, "publication-one"});
    require(warmRestart.waitForFinished(15000) && warmRestart.exitStatus() == QProcess::NormalExit
                && warmRestart.exitCode() == 0,
            "warm separate process restores fresh validated landing without scheduling network");
    // Model a bounded persisted cache where an older child was evicted but its
    // newer landing remains. No 256 network requests are needed to construct it.
    QFile cacheFile(landingCache);
    require(cacheFile.open(QIODevice::ReadOnly), "saved landing cache opens");
    auto cachedObject = QJsonDocument::fromJson(cacheFile.readAll()).object(); cacheFile.close();
    auto cachedEntries = cachedObject.value("entries").toObject();
    CatalogRemote identity("imdb");
    cachedEntries.remove(identity.queryKey("titleCatalog", landingChildArgs));
    auto olderLanding = cachedEntries.value(identity.queryKey("landing", landingArgs)).toObject();
    olderLanding["fetchedAt"] = QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODate);
    cachedEntries[identity.queryKey("landing", landingArgs)] = olderLanding;
    for (int n = 0; n < 255; ++n) {
        const QVariantList args{QVariantMap{{"genre", QString::number(n)}}, 0, 24};
        cachedEntries[identity.queryKey("titleCatalog", args)] = QJsonObject{
            {"method", "titleCatalog"}, {"result", QJsonArray()}, {"revision", source},
            {"fetchedAt", QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate)}, {"etag", ""}};
    }
    require(cachedEntries.size() == 256, "eviction fixture fills the 256-entry cache cap");
    cachedObject["entries"] = cachedEntries;
    const auto evictedCache = temp.filePath("landing/evicted.json");
    QFile evictedFile(evictedCache);
    require(evictedFile.open(QIODevice::WriteOnly), "evicted fixture opens");
    evictedFile.write(json(cachedObject)); evictedFile.close();
    {
        CatalogRemote evicted("imdb"); evicted.configure(nullptr, origin, evictedCache);
        require(evicted.queryState("titleCatalog", landingChildArgs, QVariantList()).value("status") == "missing",
                "evicted child initially absent");
        const int before = gets, beforePosts = posts;
        evicted.read("landing", landingArgs, QVariantMap());
        const auto rows = evicted.read("titleCatalog", landingChildArgs, QVariantList()).toList();
        require(evicted.queryState("landing", landingArgs, QVariantMap()).value("cached").toBool(),
                "priming retains the older landing working set under entry pressure");
        require(!rows.isEmpty() && rows.first().toMap().value("title") == "publication-one"
                    && !evicted.pending() && gets == before && posts == beforePosts,
                "cached landing re-primes an independently evicted child without I/O");
    }
    for (const auto &fault : {QStringLiteral("date"), QStringLiteral("duplicate"),
                              QStringLiteral("shape"), QStringLiteral("args"), QStringLiteral("absent")}) {
        publish("landing-fault-" + fault, fault);
        CatalogRemote rejected("imdb");
        rejected.configure(&nam, origin, temp.filePath("landing-fault-" + fault + "/remote.json"));
        const int beforePosts = posts;
        rejected.read("landing", landingArgs, QVariantMap());
        until([&] { return !rejected.pending(); });
        const auto state = rejected.queryState("landing", landingArgs, QVariantMap());
        require(state.value("status") == "failure" && !state.value("cached").toBool()
                    && state.value("error") == ((fault=="duplicate"||fault=="shape"||fault=="args")?"INVALID_LANDING":"STATIC_NOT_PREPARED")
                    && posts == beforePosts,
                "invalid or absent landing fails without calling old Worker APIs");
        require(rejected.queryState("titleCatalog", landingChildArgs, QVariantList()).value("status") == "missing",
                "invalid date, duplicate child, bad result shape or absent bundle cannot prime children");
    }
    publish("publication-one");
    auto resolve = [&](StaticCatalogueResolver *owner, const QString &method = "titleCatalog",
                       const QVariantList &args = QVariantList{QVariantMap(), 0, 24}) {
        bool done = false;
        StaticCatalogueResolver::Outcome out;
        owner->resolve("imdb", method, args, &app, [&](auto v) {
            out = v;
            done = true;
        });
        require(!done, "completion is deferred");
        until([&] { return done; });
        return out;
    };
    // Compact facts have their own publication and cache fixture; existing
    // catalogue/landing regression paths below remain unchanged.
    // Explicit QVariant wrapping avoids QList copy-initialization flattening the argument.
    const auto factArguments = [](const QVariantList &ids) {
        return QVariantList{QVariant::fromValue(ids)};
    };
    const auto factsArgs = factArguments({QStringLiteral("tt0000002"),
                                         QStringLiteral("tt0000002a"),
                                         QStringLiteral("tt0000001"),
                                         QStringLiteral("tt0000002")});
    const auto factsJsonArgs = QJsonArray::fromVariantList(factsArgs);
    require(factsJsonArgs.size() == 1 && factsJsonArgs.first().isArray() &&
                factsJsonArgs.first().toArray().size() == 4,
            "fixture sends one actual nested ID-list API argument");
    const QJsonObject expectedFacts{
        {"tt0000001", QJsonObject{{"rating", 8.4}, {"votes", 100}, {"isAnime", false}}},
        {"tt0000002", QJsonObject{{"rating", 9.1}, {"votes", 220}, {"isAnime", true}}}};
    publishFacts("facts-one");
    auto *facts = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("facts-cache"), &app);
    const auto factsCold = resolve(facts, "titleFacts", factsArgs);
    require(factsCold.status == StaticCatalogueResolver::Status::Prepared &&
                factsCold.result.toObject() == expectedFacts,
            "compact known/unknown/mixed/duplicate/ordered IDs preserve titleFacts JSON");
    require(resolve(facts, "titleFacts", factArguments({})).status ==
                StaticCatalogueResolver::Status::Prepared &&
                resolve(facts, "titleFacts", factArguments({})).result.toObject().isEmpty(),
            "compact valid empty selection is prepared empty");
    const auto unknown = resolve(facts, "titleFacts",
                                 factArguments({QStringLiteral("tt9999999")}));
    require(unknown.status == StaticCatalogueResolver::Status::Prepared &&
                unknown.result.toObject().isEmpty(),
            "compact unknown-only IDs are prepared empty without unrelated shard downloads");
    const int factsColdGets = gets;
    require(resolve(facts, "titleFacts", factsArgs).result.toObject() == expectedFacts &&
                gets == factsColdGets,
            "compact facts warm query reuses verified shared files");
    const auto title = resolve(facts, "titlePresentation", {QStringLiteral("tt0000001")});
    require(title.status == StaticCatalogueResolver::Status::Prepared &&
                title.result.toObject().value("title") == "tt0000001",
            "compact facts do not divert full generic titlePresentation identities");
    const int beforeFactsOffline = gets;
    auto *factsOffline = StaticCatalogueResolver::shared(
        nullptr, origin, temp.filePath("facts-cache"), &app);
    const auto offlineFacts = resolve(factsOffline, "titleFacts", factsArgs);
    require(offlineFacts.status == StaticCatalogueResolver::Status::Prepared &&
                offlineFacts.result.toObject() == expectedFacts && gets == beforeFactsOffline,
            "fresh offline resolver restores compact facts without HTTP");
    publishFacts("facts-two");
    facts->refresh();
    const auto switchedFacts = resolve(facts, "titleFacts", factsArgs);
    require(switchedFacts.status == StaticCatalogueResolver::Status::Prepared &&
                switchedFacts.publication != factsCold.publication &&
                switchedFacts.result.toObject().value("tt0000001").toObject().value("rating") == 9.7,
            "compact facts revision switch drops old result for same IDs");
    for (const auto &fault : {QStringLiteral("corrupt-tuple"), QStringLiteral("bad-version"),
                              QStringLiteral("bad-declaration"), QStringLiteral("empty-directory"),
                              QStringLiteral("hash"), QStringLiteral("missing")}) {
        publishFacts("facts-fault-" + fault, fault);
        if (fault == "hash") files[factsPath] = "{}";
        if (fault == "missing") files.remove(factsPath);
        auto *invalid = StaticCatalogueResolver::shared(
            &nam, origin, temp.filePath("facts-invalid-" + fault), &app);
        const int beforePosts = posts;
        require(resolve(invalid, "titleFacts", factsArgs).status ==
                    StaticCatalogueResolver::Status::Failure && posts == beforePosts,
                "advertised invalid facts tuple/declaration/hash/missing shard fails hard");
    }
    publishFacts("facts-legacy", "absent");
    auto *legacy = StaticCatalogueResolver::shared(
        &nam, origin, temp.filePath("facts-legacy"), &app);
    require(resolve(legacy, "titleFacts", factsArgs).status ==
                StaticCatalogueResolver::Status::Prepared &&
                resolve(legacy, "titleFacts", factsArgs).result.toObject() == expectedFacts,
            "only absent optional facts directory falls back to generic identity lookup");
    publishFacts("facts-absent-field", "absent-field");
    auto *missingField = StaticCatalogueResolver::shared(
        &nam, origin, temp.filePath("facts-absent-field"), &app);
    require(resolve(missingField, "titleFacts", factsArgs).result.toObject() == expectedFacts,
            "missing facts.imdb field alone retains the generic fallback");
    for (const auto &mode : {QStringLiteral("nullable"), QStringLiteral("nullable-legacy")}) {
        publishFacts("facts-" + mode, mode);
        auto *nullableFacts = StaticCatalogueResolver::shared(
            &nam, origin, temp.filePath("facts-" + mode), &app);
        const auto nullableResult = resolve(nullableFacts, "titleFacts",
                                           factArguments({QStringLiteral("tt0000003")}));
        const QJsonObject expectedNulls{{"tt0000003", QJsonObject{
            {"rating", QJsonValue(QJsonValue::Null)}, {"votes", QJsonValue(QJsonValue::Null)},
            {"isAnime", false}}}};
        require(nullableResult.status == StaticCatalogueResolver::Status::Prepared &&
                    nullableResult.result.toObject() == expectedNulls,
                "compact and legacy facts preserve null rating/votes, not numeric zero");
    }
    publish("publication-one");
    auto first = resolve(r);
    require(first.status == StaticCatalogueResolver::Status::Prepared &&
                first.result.toArray().at(0).toObject().value("title") == "publication-one",
            "prepared exact result");
    require(posts == 0, "prepared uses no guest or Worker POST");
    {
        const auto originalTrace = qgetenv("COLOSSEUM_CATALOGUE_TRACE");
        qunsetenv("COLOSSEUM_CATALOGUE_TRACE");
        traceRows.clear();
        tracePreviousHandler = qInstallMessageHandler(captureCatalogueTrace);
        require(resolve(r).status == StaticCatalogueResolver::Status::Prepared && traceRows.isEmpty(),
                "catalogue timeline diagnostics are silent by default");
        qputenv("COLOSSEUM_CATALOGUE_TRACE", "1");
        auto* traced = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("trace"), &app);
        require(resolve(traced).status == StaticCatalogueResolver::Status::Prepared,
                "diagnostics preserve prepared result behavior");
        {
            CatalogRemote tracedRemote("imdb");
            tracedRemote.configure(&nam, origin, temp.filePath("trace/remote.json"));
            tracedRemote.read("titleCatalog", landingChildArgs, QVariantList());
            until([&] { return !tracedRemote.pending() && !tracedRemote.cacheWritePending(); });
            require(tracedRemote.queryState("titleCatalog", landingChildArgs, QVariantList()).value("status") == "success",
                    "trace-enabled native query retains exact success");
        }
        qInstallMessageHandler(tracePreviousHandler);
        if (originalTrace.isNull()) qunsetenv("COLOSSEUM_CATALOGUE_TRACE");
        else qputenv("COLOSSEUM_CATALOGUE_TRACE", originalTrace);
        const auto nativeKey = identity.queryKey("titleCatalog", landingChildArgs);
        const auto leafHash = hash(files[leaf]);
        const auto position = [&](const QString& name, const QString& component,
                                  const QString& fileHash = QString()) {
            for (qsizetype n = 0; n < traceRows.size(); ++n) {
                const auto& row = traceRows[n];
                if (row.value("event") == name && row.value("component") == component
                    && (row.value("queryKey") == nativeKey || row.value("queryKeys").toArray().contains(nativeKey))
                    && (fileHash.isEmpty() || row.value("fileHash") == fileHash)) return n;
            }
            return qsizetype(-1);
        };
        const auto ordered = [&](const QStringList& events, const QString& component,
                                 const QString& fileHash = QString()) {
            qsizetype previous = -1;
            for (const auto& name : events) {
                const auto current = position(name, component, fileHash);
                require(current > previous, "diagnostic phase events exist in causal order");
                previous = current;
            }
        };
        ordered({"enqueue", "dispatch", "result_delivery_begin", "result_delivery_end"}, "resolver");
        ordered({"network_queued", "network_start", "network_bytes_done", "json_parse_begin",
                 "json_parse_end", "disk_write_begin", "disk_write_end"}, "resolver", leafHash);
        ordered({"enqueue", "dispatch", "result_ready", "result_delivery_begin", "result_delivery_end"}, "remote");
        ordered({"cache_persist_queued", "cache_persist_begin", "cache_serialize_begin",
                 "cache_serialize_end", "disk_write_begin", "disk_write_end", "cache_persist_end"}, "remote");
        for (const auto& row : traceRows) {
            require(row.value("epochMs").toDouble() > 0 && row.value("monotonicMs").toDouble() > 0,
                    "timeline carries epoch and monotonic clocks");
            const auto raw = json(row);
            require(!row.contains("args") && !raw.contains("127.0.0.1") && !raw.contains("Bearer ")
                        && !raw.contains(temp.path().toUtf8()),
                    "diagnostics redact arguments, endpoints, credentials and local cache paths");
        }
    }
    const int coldGets = gets;
    auto warm = resolve(r);
    require(warm.status == StaticCatalogueResolver::Status::Prepared && gets == coldGets,
            "warm immutable reuse");
    r->refresh();
    require(resolve(r).status == StaticCatalogueResolver::Status::Prepared && gets == coldGets + 1,
            "conditional current.json 304 reuses validated index and shards");
    const int restoredGets = gets;
    auto *cached = StaticCatalogueResolver::shared(nullptr, origin, temp.path(), &app);
    auto restored = resolve(cached);
    require(restored.status == StaticCatalogueResolver::Status::Prepared && gets == restoredGets,
            "offline new resolver restores validated shards");
    r->refresh();
    publish("publication-two");
    auto changed = resolve(r);
    require(changed.publication != first.publication &&
                changed.result.toArray().at(0).toObject().value("title") == "publication-two",
            "revision switch invalidates old data");
    auto *bad = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("bad"), &app);
    const auto correct = files[leaf];
    files[leaf] = "[]";
    require(resolve(bad).status == StaticCatalogueResolver::Status::Failure,
            "hash mismatch fails, never successful empty");
    files[leaf] = correct;
    auto *missing = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("missing"), &app);
    files.remove(leaf);
    require(resolve(missing).status == StaticCatalogueResolver::Status::Failure,
            "missing referenced shard fails, not fallback");
    files[leaf] = correct;
    auto unprepared = resolve(r, "titleCatalog", {QVariantMap{{"genre", "unprepared"}}, 0, 24});
    require(unprepared.status == StaticCatalogueResolver::Status::NotPrepared,
            "rare query explicitly unprepared");
    const auto goodEntry = files["/catalogue/current.json"];
    auto wrongSize = QJsonDocument::fromJson(goodEntry).object();
    auto indexRef = wrongSize.value("index").toObject();
    indexRef["bytes"] = indexRef.value("bytes").toInt() + 1;
    wrongSize["index"] = indexRef;
    files["/catalogue/current.json"] = json(wrongSize);
    r->refresh();
    require(resolve(r).status == StaticCatalogueResolver::Status::Failure,
            "cached index still enforces the entry's declared byte length");
    r->refresh();
    files["/catalogue/current.json"] = "{}";
    require(resolve(r).status == StaticCatalogueResolver::Status::Failure,
            "corrupt refresh is a failure even when last-good exists");
    files["/catalogue/current.json"] = goodEntry;
    r->refresh();
    require(resolve(r).status == StaticCatalogueResolver::Status::Prepared,
            "valid entry recovers after corrupt refresh");
    CatalogRemote remote("imdb");
    remote.configure(&nam, origin, temp.filePath("remote.json"));
    const QVariantList preparedArgs{QVariantMap(), 0, 24};
    QStringList notices;
    QObject::connect(&remote, &CatalogRemote::queryChanged, &app,
                     [&](const QString &key) { notices.append(key); });
    const auto exactKey =
        remote.queryState("titleCatalog", preparedArgs, QVariantList()).value("key");
    remote.read("titleCatalog", preparedArgs, QVariantList());
    require(notices.isEmpty(), "F2 initial notification remains deferred");
    until([&] { return !remote.pending(); });
    require(posts == 0 && remote.read("titleCatalog", preparedArgs, QVariantList())
                                  .toList()
                                  .at(0)
                                  .toMap()
                                  .value("title") == "publication-two",
            "CatalogRemote prepared query avoids lease and Worker");
    require(notices.size() == 2 && notices.at(0) == exactKey && notices.at(1) == exactKey,
            "F2 exact-key pending/success notifications preserved");
    files["/catalogue/current.json"] = "{}";
    remote.read("titleCatalog", preparedArgs, QVariantList(), true);
    until([&] { return !remote.pending(); });
    const auto failed = remote.queryState("titleCatalog", preparedArgs, QVariantList());
    require(failed.value("status") == "failure" && failed.value("cached").toBool() &&
                failed.value("retryable").toBool() && posts == 0,
            "static error retains validated stale cache, no Worker fallback");
    files["/catalogue/current.json"] = goodEntry;
    require(remote.retry("titleCatalog", preparedArgs, QVariantList()),
            "explicit static key retry accepted");
    until([&] { return !remote.pending(); });
    require(remote.queryState("titleCatalog", preparedArgs, QVariantList()).value("status") ==
                    "success" &&
                posts == 0,
            "explicit retry revalidates metadata and clears only the failed key");
    TankobanCatalog tank("");
    tank.configureRemote(&nam, origin, temp.filePath("tank.json"));
    bool seriesDone = false;
    QObject::connect(&tank, &TankobanCatalog::seriesChanged, &app, [&](int id) {
        if (id == 13)
            seriesDone = true;
    });
    tank.requestSeries(13);
    until([&] { return seriesDone; });
    require(tank.volumes(13).size() == 113 && posts == 0,
            "Tankoban separate transport uses static lookup, no Worker");
    remote.read("titleCatalog", {QVariantMap{{"genre", "rare"}}, 0, 24}, QVariantList());
    until([&] { return !remote.pending(); });
    require(
        remote.queryState("titleCatalog", {QVariantMap{{"genre", "rare"}}, 0, 24}, QVariantList())
                    .value("status") == "success" &&
            posts == 2,
        "unprepared retains Worker lease and POST");
    CatalogRemote comics("comics");
    comics.configure(&nam, origin, temp.filePath("comics.json"));
    comics.read("search", {"batman", 20}, QVariantList());
    until([&] { return !comics.pending(); });
    const int afterSearch = posts;
    comics.read("search", {"batman", 20}, QVariantList());
    require(!comics.pending() && posts == afterSearch,
            "Comics Worker search cache does not spin on static refresh");
    bool cancelled = false;
    auto id =
        r->resolve("imdb", "titleCatalog", preparedArgs, &app, [&](auto) { cancelled = true; });
    r->cancel(id);
    QCoreApplication::processEvents();
    require(!cancelled, "cancelled query never calls consumer");
    publish("publication-three");
    remote.read("titleCatalog", preparedArgs, QVariantList(), true);
    until([&] { return !remote.pending(); });
    require(remote.read("titleCatalog", preparedArgs, QVariantList())
                    .toList()
                    .at(0)
                    .toMap()
                    .value("title") == "publication-three",
            "owner result cache invalidates on publication change with unchanged dataset source");
    QProcess restarted;
    restarted.start(
        QCoreApplication::applicationFilePath(),
        {"--offline", origin.toString(QUrl::FullyEncoded), temp.path(), "publication-three"});
    require(restarted.waitForFinished(15000) && restarted.exitStatus() == QProcess::NormalExit &&
                restarted.exitCode() == 0 &&
                restarted.readAllStandardOutput().contains(
                    "PASS separate-process static offline restoration"),
            "separate-process offline restoration completes cleanly");
    CatalogRemote diskOnly("imdb");
    diskOnly.configure(nullptr, origin, temp.filePath("offline-new-result.json"));
    diskOnly.read("titleCatalog", preparedArgs, QVariantList());
    const auto diskPending = diskOnly.queryState("titleCatalog", preparedArgs, QVariantList());
    require(diskPending.value("status") == "pending" && diskPending.value("enabled").toBool(),
            "disk-capable pending query stays enabled for unchanged F2 consumers");
    until([&] { return !diskOnly.pending(); });
    require(diskOnly.queryState("titleCatalog", preparedArgs, QVariantList()).value("status") ==
                "success",
            "offline owner assembles uncached result from persisted shards");
    // Cached files are verified again on an offline restart, rather than trusting disk.
    const auto cacheRoot =
        QDir(temp.path())
            .filePath("static-catalogue-v1/" + hash(origin.toString(QUrl::FullyEncoded).toUtf8()));
    QFile damaged(QDir(cacheRoot).filePath(hash(files[leaf]) + ".json"));
    require(damaged.open(QIODevice::WriteOnly), "cached shard available for corruption probe");
    damaged.write("[]");
    damaged.close();
    QObject offlineOwner;
    auto *corruptOffline =
        StaticCatalogueResolver::shared(nullptr, origin, temp.path(), &offlineOwner);
    require(resolve(corruptOffline).status == StaticCatalogueResolver::Status::Failure,
            "offline corrupt shard fails hash validation, never success or empty");
    // Cancellation after real I/O has begun must abort its owned request and suppress delivery.
    holdLeaf = true;
    auto *inflight = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("inflight"), &app);
    bool late = false;
    auto inflightId =
        inflight->resolve("imdb", "titleCatalog", preparedArgs, &app, [&](auto) { late = true; });
    until([&] { return !held.isEmpty(); });
    inflight->cancel(inflightId);
    until([&] {
        return !held.first() || held.first()->state() == QAbstractSocket::UnconnectedState;
    });
    require(!late, "in-flight cancelled shard never delivers a late result");
    held.clear();
    auto *context = new QObject;
    inflight->resolve("imdb", "titleCatalog", preparedArgs, context, [&](auto) { late = true; });
    until([&] { return !held.isEmpty(); });
    delete context;
    until([&] {
        return !held.first() || held.first()->state() == QAbstractSocket::UnconnectedState;
    });
    require(!late, "destroyed consumer cancels in-flight shard ownership");
    held.clear();
    CatalogRemote reconfigured("imdb");
    reconfigured.configure(&nam, origin, temp.filePath("reconfigure/remote.json"));
    reconfigured.read("titleCatalog", preparedArgs, QVariantList());
    until([&] { return !held.isEmpty(); });
    reconfigured.configure(nullptr, QUrl(), {});
    QCoreApplication::processEvents();
    require(
        !reconfigured.pending() &&
            reconfigured.queryState("titleCatalog", preparedArgs, QVariantList()).value("status") ==
                "missing",
        "reconfiguration cancels old-source work without result leakage");
    holdLeaf = false;
    // Rollback to a pre-static server must also work for a client that already
    // cached a publication. Only the optional entry may announce this absence;
    // a missing referenced shard remains the hard failure tested above.
    files.remove("/catalogue/current.json");
    const int beforeRollback = posts;
    remote.read("titleCatalog", preparedArgs, QVariantList(), true);
    until([&] { return !remote.pending(); });
    require(remote.queryState("titleCatalog", preparedArgs, QVariantList()).value("status") ==
                "success" && posts == beforeRollback + 1 &&
                remote.read("titleCatalog", preparedArgs, QVariantList()).toList().at(0)
                    .toMap().value("title") == "worker",
            "warm client falls back to legacy Worker when rollback removes public entry");
    publish("publication-before-hot-update");
    rotateMissing = true;
    auto *hot = StaticCatalogueResolver::shared(&nam, origin, temp.filePath("hot-update"), &app);
    const auto switched = resolve(hot);
    require(switched.status == StaticCatalogueResolver::Status::Prepared &&
                switched.result.toArray().at(0).toObject().value("title") == "publication-hot-update",
            "retired shard triggers one entry refresh and completes from the new publication");
    require(QJsonDocument::fromVariant(measured).isArray(), "measurement rows available");
    if (argc > 1) {
        QFile report(QString::fromLocal8Bit(argv[1]));
        require(report.open(QIODevice::WriteOnly), "evidence output opens");
        report.write(
            QJsonDocument(QJsonObject{{"measurements", QJsonArray::fromVariantList(measured)},
                                      {"gets", gets},
                                      {"workerPosts", posts}})
                .toJson());
    }
    std::cout << "PASS static catalogue resolver\n";
}
