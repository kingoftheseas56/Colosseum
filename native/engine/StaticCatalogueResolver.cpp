#include "StaticCatalogueResolver.h"
#include "CatalogueTrace.h"
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSharedPointer>
#include <QSet>
#include <QStringDecoder>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

Q_LOGGING_CATEGORY(staticCatalogueLog, "colosseum.catalogue.static")
namespace {
constexpr qint64 fileCap = 25 * 1024 * 1024, diskCap = 256 * 1024 * 1024, jobCap = 64 * 1024 * 1024,
                 allJobsCap = 128 * 1024 * 1024;
constexpr int queryCap = 128, filesCap = 512;
QString digest(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
bool hash(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[a-f0-9]{64}$"));
    return re.match(s).hasMatch();
}
bool imdbTitleId(const QString &id) {
    if (id.size() <= 2 || id.at(0) != QLatin1Char('t') || id.at(1) != QLatin1Char('t'))
        return false;
    for (qsizetype i = 2; i < id.size(); ++i)
        if (id.at(i) < QLatin1Char('0') || id.at(i) > QLatin1Char('9')) {
            // Preserve QRegularExpression's exact edge-case semantics (including
            // an end-of-input newline); valid published IDs take the fast path.
            static const QRegularExpression original(QStringLiteral("^tt[0-9]+$"));
            return original.match(id).hasMatch();
        }
    return true;
}
QByteArray json(const QJsonValue &v) {
    if (v.isObject())
        return QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    if (v.isArray())
        return QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
    const auto b = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return b.mid(1, b.size() - 2);
}
QJsonValue parse(const QByteArray &raw, const QVariantMap& trace = {}) {
    CatalogueTrace::Span span("json_parse", CatalogueTrace::merge({{"component", "resolver"}}, trace));
    QStringDecoder decoder(QStringDecoder::Utf8);
    const auto text = decoder(raw);
    Q_UNUSED(text);
    if (decoder.hasError())
        throw QStringLiteral("STATIC_INVALID_UTF8");
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(raw, &e);
    if (e.error == QJsonParseError::NoError) {
        span.finish("success", {{"bytes", raw.size()}});
        return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    }
    const auto scalar = QJsonDocument::fromJson("[" + raw + "]", &e);
    if (e.error != QJsonParseError::NoError || !scalar.isArray() || scalar.array().size() != 1)
        throw QStringLiteral("STATIC_INVALID_JSON");
    span.finish("success", {{"bytes", raw.size()}});
    return scalar.array().at(0);
}
QString text(const QJsonValue &v) {
    if (v.isNull() || v.isUndefined())
        return {};
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())
        return QString::fromUtf8(json(v));
    if (v.isArray()) {
        QStringList out;
        for (const auto &x : v.toArray())
            out.append(text(x));
        return out.join(',');
    }
    return QStringLiteral("[object Object]");
}
qint64 integer(const QJsonValue &v) {
    bool ok = false;
    double n = 0;
    if (v.isDouble()) {
        n = v.toDouble();
        ok = true;
    } else if (v.isBool()) {
        n = v.toBool() ? 1 : 0;
        ok = true;
    } else if (v.isNull() || v.isUndefined())
        ok = true;
    else {
        const auto s = text(v).trimmed();
        n = s.isEmpty() ? 0 : s.toDouble(&ok);
        if (s.isEmpty())
            ok = true;
    }
    if (!ok || !std::isfinite(n))
        return 0;
    return qint64(std::clamp(std::trunc(n), -9007199254740991.0, 9007199254740991.0));
}
int clamp(const QJsonValue &v, int fallback = 24) {
    return int(std::clamp<qint64>(v.isUndefined() ? fallback : integer(v), 1, 100));
}
bool truth(const QJsonValue &v) {
    return v.isBool()     ? v.toBool()
           : v.isDouble() ? v.toDouble() != 0
           : v.isString() ? !v.toString().isEmpty()
                          : v.isArray() || v.isObject();
}
QString fold(QString s) {
    for (auto &c : s)
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            c = QChar(c.unicode() + 32);
    return s;
}
QString selection(const QString &dataset, const QString &method, const QJsonArray &args) {
    return QString::fromUtf8(json(QJsonArray{dataset, method, args}));
}
QString bucket(const QString &key) {
    return digest(key.toUtf8()).left(2);
}
// Verify SHA256(UTF8(tt)) with no digest-hex allocation per validated tuple.
bool bucketMember(const QString &id, int expectedFirstByte) {
    const auto utf8 = id.toUtf8();
    std::array<uchar, 32> bytes{};
    QCryptographicHash::hashInto(QSpan<uchar>(bytes.data(), bytes.size()),
                                 QByteArrayView(utf8), QCryptographicHash::Sha256);
    return int(bytes[0]) == expectedFirstByte;
}
QJsonArray array(const QJsonValue &v) {
    if (!v.isArray())
        throw QStringLiteral("STATIC_INVALID_SHARD");
    return v.toArray();
}
QJsonObject object(const QJsonValue &v) {
    if (!v.isObject())
        throw QStringLiteral("STATIC_INVALID_SHARD");
    return v.toObject();
}
int desc(const QJsonValue &a, const QJsonValue &b) {
    const bool an = a.isNull() || a.isUndefined(), bn = b.isNull() || b.isUndefined();
    if (an)
        return bn ? 0 : 1;
    if (bn)
        return -1;
    const auto x = a.toDouble(), y = b.toDouble();
    return x > y ? -1 : x < y ? 1 : 0;
}
struct Need {
    QString path;
};
struct NeedBatch {
    QStringList paths;
    int selectedPrefixes = 0;
};
using Read = std::function<QJsonValue(const QString &)>;

// One result encoder for the V1 and hash-bucket paths: preserve 100-ID chunks,
// ordinal ordering, duplicate overwrites, unknown omission and JSON nulls.
QJsonObject factsResult(const QJsonArray &ids, const QHash<QString, QJsonArray> &matched) {
    QJsonObject result;
    for (qsizetype i = 0; i < ids.size(); i += 100) {
        QList<QPair<QString, QJsonArray>> found;
        for (qsizetype n = i; n < std::min(i + 100, ids.size()); ++n) {
            const auto id = text(ids.at(n));
            if (matched.contains(id))
                found.append({id, matched.value(id)});
        }
        std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
            return a.second.at(4).toDouble() < b.second.at(4).toDouble();
        });
        for (const auto &item : found)
            result[item.first] = QJsonObject{{"rating", item.second.at(1)},
                                             {"votes", item.second.at(2)},
                                             {"isAnime", item.second.at(3)}};
    }
    return result;
}

// Additive IMDb facts directory. Absence alone permits the legacy identity route;
// an advertised but invalid directory or shard is an exact-query hard failure.
std::optional<QJsonValue> compactImdbFacts(const QJsonObject &index, const QJsonArray &ids,
                                           const Read &read, quint64 *validatedTuples,
                                           QSet<QString> *validatedBlocks, bool bucketsReady) {
    if (!index.contains("facts"))
        return std::nullopt;
    const auto root = index.value("facts");
    if (!root.isObject())
        throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
    const auto facts = root.toObject();
    if (!facts.contains("imdb"))
        return std::nullopt;
    if (!facts.value("imdb").isObject())
        throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
    const auto imdb = facts.value("imdb").toObject();
    if (imdb.value("version") != 1 || !imdb.value("blocks").isArray())
        throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
    const auto refs = imdb.value("blocks").toArray();
    const auto sourceDirectory = index.value("search").toObject().value("imdb");
    if (!sourceDirectory.isObject() ||
        !sourceDirectory.toObject().value("keys").isArray())
        throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
    const auto sourceRefs = sourceDirectory.toObject().value("keys").toArray();
    if (refs.size() != sourceRefs.size() || refs.size() > filesCap)
        throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
    const auto prefix = "/catalogue/" + index.value("revision").toString() + "/shared/";
    static const QRegularExpression leaf(QStringLiteral("^[a-f0-9]{64}\\.json$"));
    struct Block { QString path, first, last; };
    QList<Block> blocks;
    QString previousLast;
    for (qsizetype blockNo = 0; blockNo < refs.size(); ++blockNo) {
        const auto refValue = refs.at(blockNo);
        if (!refValue.isObject() || !sourceRefs.at(blockNo).isObject())
            throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        const auto ref = refValue.toObject();
        const auto path = ref.value("path").toString();
        const auto first = ref.value("first").toString(), last = ref.value("last").toString();
        const auto original = sourceRefs.at(blockNo).toObject();
        if (original.value("first").toString() != first ||
            original.value("last").toString() != last ||
            !imdbTitleId(first) || !imdbTitleId(last) ||
            first > last || (!previousLast.isEmpty() && previousLast >= first) ||
            !path.startsWith(prefix) || !leaf.match(path.mid(prefix.size())).hasMatch())
            throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        blocks.append({path, first, last});
        previousLast = last;
    }
    QSet<QString> wanted;
    for (const auto &v : ids)
        wanted.insert(text(v));  // Mirrors legacy byId's text coercion.
    if (imdb.contains("hashBuckets")) {
        const auto hashValue = imdb.value("hashBuckets");
        if (!hashValue.isObject())
            throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        const auto declaration = hashValue.toObject();
        if (declaration.value("version") != 1 ||
            declaration.value("keyHash") != "sha256-utf8" ||
            declaration.value("prefixChars") != 2 ||
            !declaration.value("paths").isObject())
            throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        const auto paths = declaration.value("paths").toObject();
        if (paths.size() != 256)
            throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        // Validate the entire advertised directory, not just requested prefixes.
        // Do not access even an unselected path until this check has passed.
        for (int n = 0; n < 256; ++n) {
            const auto key = QString::number(n, 16).rightJustified(2, QLatin1Char('0'));
            const auto value = paths.value(key);
            if (!value.isString())  // Missing key, wrong type or extra key.
                throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
            const auto path = value.toString();
            if (!path.startsWith(prefix) ||
                !leaf.match(path.mid(prefix.size())).hasMatch())
                throw QStringLiteral("STATIC_INVALID_FACTS_DECLARATION");
        }
        QHash<QString, QStringList> idsByPrefix;
        for (const auto &id : wanted)
            if (imdbTitleId(id))
                idsByPrefix[bucket(id)].append(id);
        auto requested = idsByPrefix.keys();
        std::sort(requested.begin(), requested.end());  // Deterministic error order.
        QStringList distinctPaths;
        QSet<QString> seenPaths;
        for (const auto &key : requested) {
            const auto path = paths.value(key).toString();
            if (!seenPaths.contains(path)) {
                distinctPaths.append(path);
                seenPaths.insert(path);
            }
        }
        if (!bucketsReady && !distinctPaths.isEmpty())
            throw NeedBatch{distinctPaths, int(requested.size())};

        QHash<QString, QJsonArray> matched;
        for (const auto &key : requested) {
            const auto path = paths.value(key).toString();
            const auto rows = array(read(path));
            const int firstHashByte = key.toInt(nullptr, 16);
            const auto validationKey = key + QLatin1Char('\n') + path;
            if (!validatedBlocks || !validatedBlocks->contains(validationKey)) {
                // Hash-prefix occupancy is independent of the aligned V1
                // source block size. Existing file/job byte limits still bound it.
                QString prior;
                for (const auto &value : rows) {
                    if (!value.isArray())
                        throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
                    const auto tuple = value.toArray();
                    if (tuple.size() != 5)
                        throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
                    const auto name = tuple.at(0), rating = tuple.at(1),
                               votes = tuple.at(2), anime = tuple.at(3),
                               ordinal = tuple.at(4);
                    if (!name.isString())
                        throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
                    const auto id = name.toString();
                    const double count = votes.toDouble(), order = ordinal.toDouble();
                    if (!imdbTitleId(id) || !bucketMember(id, firstHashByte) ||
                        (!prior.isEmpty() && prior >= id) ||
                        !(rating.isNull() ||
                          (rating.isDouble() && std::isfinite(rating.toDouble()))) ||
                        !(votes.isNull() ||
                          (votes.isDouble() && std::isfinite(count) &&
                           count >= 0 && count <= 9007199254740991.0 &&
                           std::trunc(count) == count)) ||
                        !anime.isBool() || !ordinal.isDouble() ||
                        !std::isfinite(order) || order < 0 || order > 9007199254740991.0 ||
                        std::trunc(order) != order)
                        throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
                    if (validatedTuples)
                        ++*validatedTuples;
                    prior = id;
                }
                if (validatedBlocks)
                    validatedBlocks->insert(validationKey);
            }
            // Pre-group requested IDs once, rather than rehashing every ID
            // for every selected shard while resolving a large cold query.
            for (const auto &id : idsByPrefix.value(key)) {
                qsizetype lo = 0, hi = rows.size();
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (rows.at(mid).toArray().at(0).toString() < id)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                if (lo < rows.size()) {
                    const auto tuple = rows.at(lo).toArray();
                    if (tuple.at(0).toString() == id)
                        matched.insert(id, tuple);
                }
            }
        }
        return factsResult(ids, matched);
    }
    // Bound semantic-validation progress to this exact job's hash-verified files.
    // A missing shard still suspends asynchronously, but an earlier validated
    // block is never scanned again; malformed earlier blocks fail before later I/O.
    QList<Block> required;
    for (const auto &block : blocks) {
        for (const auto &id : wanted) {
            if (id >= block.first && id <= block.last) {
                required.append(block);
                break;
            }
        }
    }
    for (const auto &block : required) {
        const auto rows = array(read(block.path));
        if (validatedBlocks && validatedBlocks->contains(block.path))
            continue;
        if (rows.isEmpty() || rows.size() > 2048)
            throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
        QString prior;
        for (const auto &value : rows) {
            if (!value.isArray())
                throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
            const auto tuple = value.toArray();
            if (tuple.size() != 5 || !tuple.at(0).isString())
                throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
            const auto id = tuple.at(0).toString();
            if (!imdbTitleId(id) ||
                (!prior.isEmpty() && prior >= id) ||
                id < block.first || id > block.last ||
                !(tuple.at(1).isNull() ||
                  (tuple.at(1).isDouble() && std::isfinite(tuple.at(1).toDouble()))) ||
                !(tuple.at(2).isNull() ||
                  (tuple.at(2).isDouble() && std::isfinite(tuple.at(2).toDouble()) &&
                   tuple.at(2).toDouble() >= 0 &&
                   tuple.at(2).toDouble() <= 9007199254740991.0 &&
                   std::trunc(tuple.at(2).toDouble()) == tuple.at(2).toDouble())) ||
                !tuple.at(3).isBool() || !tuple.at(4).isDouble() ||
                !std::isfinite(tuple.at(4).toDouble()) || tuple.at(4).toDouble() < 0 || tuple.at(4).toDouble() > 9007199254740991.0 ||
                std::trunc(tuple.at(4).toDouble()) != tuple.at(4).toDouble())
                throw QStringLiteral("STATIC_INVALID_FACTS_TUPLE");
            if (validatedTuples)
                ++*validatedTuples;
            prior = id;
        }
        if (rows.first().toArray().at(0).toString() != block.first || prior != block.last)
            throw QStringLiteral("STATIC_INVALID_FACTS_RANGE");
        if (validatedBlocks)
            validatedBlocks->insert(block.path);
    }
    // All required blocks are now semantically valid. Recover only matching
    // tuples by binary search within their validated, sorted existing JSON rows;
    // keep no extra result map between suspensions or after query completion.
    QHash<QString, QJsonArray> matched;
    for (const auto &block : required) {
        const auto rows = array(read(block.path));
        for (const auto &id : wanted) {
            if (id < block.first || id > block.last)
                continue;
            qsizetype lo = 0, hi = rows.size();
            while (lo < hi) {
                const auto mid = lo + (hi - lo) / 2;
                if (rows.at(mid).toArray().at(0).toString() < id)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo < rows.size()) {
                const auto tuple = rows.at(lo).toArray();
                if (tuple.at(0).toString() == id)
                    matched.insert(id, tuple);
            }
        }
    }
    return factsResult(ids, matched);
}

// Pure I01 resolver. A missing local file suspends this evaluation; it is resumed
// only after asynchronous verified loading. No nested event loops or GUI blocking.
std::optional<QJsonValue> evaluate(const QJsonObject &index, const QString &ds,
                                   const QString &method, const QJsonArray &args,
                                   const Read &read, quint64 *validatedTuples,
                                   QSet<QString> *validatedBlocks, bool bucketsReady) {
    const auto at = [&](int n) {
        return n < args.size() ? args.at(n) : QJsonValue(QJsonValue::Undefined);
    };
    if (ds == "imdb" && method == "titleFacts") {
        const auto compact = compactImdbFacts(index, at(0).toArray(), read, validatedTuples,
                                            validatedBlocks, bucketsReady);
        if (compact)
            return compact;
    }
    if (ds == "content" && method == "bundle" && args.isEmpty()) {
        const auto path =
            index.value("content").toObject().value("bundle").toObject().value("path").toString();
        if (path.isEmpty())
            return std::nullopt;
        const auto bundle = object(read(path));
        if (bundle.value("apiVersion") != 1 || bundle.value("method") != "bundle" ||
            !hash(bundle.value("revision").toString()) ||
            bundle.value("result").toObject().value("manifest").toObject().value("revision") !=
                bundle.value("revision"))
            throw QStringLiteral("STATIC_INVALID_CONTENT");
        return bundle.value("result");
    }
    QJsonArray fixedArgs = args;
    if (method == "discoverFilters" || method == "cachedDiscoverFilters")
        fixedArgs = {text(at(0)), ds == "comics" ? false : truth(at(1))};
    const auto fixed = index.value("fixed").toObject();
    const auto fixedPath = fixed.value(selection(ds, method, fixedArgs)).toString();
    if (!fixedPath.isEmpty())
        return read(fixedPath);
    if (ds == "comics" && method == "curatedGenreShelves") {
        const auto path = fixed.value(selection(ds, method, {-1})).toString();
        if (path.isEmpty())
            return std::nullopt;
        QJsonArray out;
        const qint64 n = integer(at(0));
        for (const auto &v : array(read(path))) {
            auto shelf = object(v);
            const auto covers = array(shelf.value("covers"));
            QJsonArray chosen;
            for (qsizetype i = 0; i < covers.size() && (n < 0 || i < n); ++i)
                chosen.append(covers.at(i));
            shelf["covers"] = chosen;
            out.append(shelf);
        }
        return out;
    }
    const QStringList lookupMethods = {"series",          "downloadsFor",  "curatedSeries",
                                       "curatedCoverage", "curatedByNorm", "curatedHasDownloadable",
                                       "genreCount"};
    if (index.contains("lookups") && lookupMethods.contains(method)) {
        QJsonArray lookupArgs;
        if (method == "series" || method == "downloadsFor")
            lookupArgs = {integer(at(0))};
        else if (method == "genreCount")
            lookupArgs = {text(at(0)), text(at(1))};
        else
            lookupArgs = {method == "curatedByNorm" ? text(at(0)).trimmed() : text(at(0))};
        const auto key = selection(ds, method, lookupArgs),
                   path = index.value("lookups").toObject().value(bucket(key)).toString();
        if (!path.isEmpty()) {
            const auto values = object(read(path));
            if (values.contains(key))
                return values.value(key);
        }
        if (method == "genreCount")
            return QJsonValue(0);
        if (method == "curatedHasDownloadable")
            return QJsonValue(false);
        if (method == "downloadsFor")
            return QJsonArray();
        if (ds == "tankoban")
            return QJsonObject{{"apiVersion", 1},
                               {"sourceHash", index.value("tankobanSourceHash")},
                               {"ready", true},
                               {"seriesInfo", QJsonObject()},
                               {"volumes", QJsonArray()}};
        return QJsonObject();
    }
    std::optional<QJsonArray> selected;
    qint64 offset = 0, limit = 24;
    QString listMethod = method, catalog;
    auto emptyPage = [&](const QJsonArray &items, bool exhausted) {
        QJsonObject out{{"items", items},
                        {"nextOffset", offset + items.size()},
                        {"exhausted", exhausted},
                        {"freshness", "bundled"}};
        if (ds == "mal")
            out["fallbackCatalog"] = catalog == "trending" ? "popular" : "";
        return out;
    };
    if ((ds == "mal" || ds == "comics") &&
        (method == "discoverPage" || method == "discoverPageFiltered")) {
        catalog = text(at(0));
        QJsonArray filters;
        bool explicitContent;
        if (method == "discoverPage") {
            if (truth(at(1)) || truth(at(2)))
                filters.append(QJsonObject{{"axis", text(at(1))}, {"key", text(at(2))}});
            explicitContent = truth(at(3));
            offset = std::max<qint64>(0, integer(at(4)));
            limit = clamp(at(5));
        } else {
            filters = at(1).toArray();
            explicitContent = truth(at(2));
            offset = std::max<qint64>(0, integer(at(3)));
            limit = clamp(at(4));
        }
        const QStringList axes =
            ds == "mal" ? QStringList{"", "genre", "demographic"}
                        : QStringList{"", "genre", "publisher", "format", "availability"};
        const QStringList catalogs =
            ds == "mal" ? QStringList{"popular", "trending", "top-rated", "new-releases"}
                        : QStringList{"popular",       "new-releases",         "most-stocked",
                                      "all",           "recently-available",   "complete-runs",
                                      "near-complete", "community-collections"};
        if (!catalogs.contains(catalog))
            return emptyPage({}, true);
        QJsonArray normalized;
        for (const auto &f : filters) {
            const auto filter = f.toObject();
            const auto axis = text(filter.value("axis")), key = text(filter.value("key"));
            if (!axes.contains(axis))
                return emptyPage({}, true);
            if (!axis.isEmpty() && !key.isEmpty())
                normalized.append(QJsonObject{{"axis", axis}, {"key", fold(key)}});
        }
        if (normalized.size() <= 1) {
            selected = QJsonArray{catalog, normalized, explicitContent};
            listMethod = "discoverPageFiltered";
        }
    } else if ((ds == "imdb" && method == "titleCatalog") ||
               (ds == "mal" && method == "animeCatalog")) {
        selected = QJsonArray{at(0).isObject() ? at(0) : QJsonValue(QJsonObject())};
        offset = std::max<qint64>(0, integer(at(1)));
        limit = clamp(at(2), ds == "imdb" ? 100 : 24);
    } else if (ds == "mal" && method == "genreEntries") {
        selected =
            QJsonArray{text(at(0)), text(at(1)), text(at(2)) == "score" ? "score" : "members"};
        limit = at(3).isUndefined() ? 24 : integer(at(3));
    }
    if (selected) {
        QList<QJsonArray> variants{*selected};
        if ((ds == "imdb" && method == "titleCatalog") ||
            (ds == "mal" && method == "animeCatalog")) {
            auto q = selected->at(0).toObject();
            const QString def = ds == "imdb" ? "votes" : "members";
            if (text(q.value("order")).isEmpty() || text(q.value("order")) == def) {
                q["order"] = def;
                variants.append(QJsonArray{q});
                q.remove("order");
                variants.append(QJsonArray{q});
            }
        }
        QJsonObject list;
        for (const auto &variant : variants) {
            const auto key = selection(ds, listMethod, variant);
            auto value = index.value("lists").toObject().value(key);
            const auto path = index.value("listBuckets").toObject().value(bucket(key)).toString();
            if (value.isUndefined() && !path.isEmpty())
                value = object(read(path)).value(key);
            if (!value.isUndefined()) {
                list = object(value);
                break;
            }
        }
        if (!list.isEmpty()) {
            const auto now = QDateTime::currentDateTimeUtc();
            const QString period = ds == "comics" ? now.toString("yyyy")
                                   : ds == "mal" && catalog == "new-releases"
                                       ? now.toString("yyyy-MM-dd")
                                       : "stable";
            if (list.value("period") != "stable" && list.value("period") != period)
                return std::nullopt;
            if (list.contains("notBefore") &&
                now.toString("yyyy-MM-dd") < list.value("notBefore").toString())
                return std::nullopt;
            const qint64 length = integer(list.value("length")),
                         chunk = integer(list.value("chunkSize"));
            const auto parts = array(list.value("parts"));
            const auto shape = list.value("shape").toString();
            if (length < 0 || length > 10000000 || chunk < 1 || chunk > 1000000 ||
                parts.size() != (length + chunk - 1) / chunk ||
                !QStringList{"array", "mal", "comics"}.contains(shape))
                throw QStringLiteral("STATIC_INVALID_LIST");
            const qint64 end = limit < 0 ? length : std::min(length, offset + limit);
            QJsonArray items;
            for (qint64 n = offset / chunk; n < (end + chunk - 1) / chunk; ++n) {
                const auto block = array(read(parts.at(n).toString()));
                const auto expected = std::min(chunk, length - n * chunk);
                if (block.size() != expected)
                    throw QStringLiteral("STATIC_INVALID_LIST");
                for (qint64 i = std::max<qint64>(0, offset - n * chunk);
                     i < std::min(chunk, end - n * chunk); ++i)
                    items.append(block.at(i));
            }
            if (shape == "array")
                return items;
            if (shape == "comics") {
                QJsonArray original;
                if (method == "discoverPage")
                    original.append(QJsonObject{{"axis", text(at(1))}, {"key", text(at(2))}});
                else
                    original = at(1).toArray();
                for (const auto &v : original)
                    if (v.toObject().value("axis") == "format") {
                        for (qsizetype i = 0; i < items.size(); ++i) {
                            auto row = items.at(i).toObject();
                            row["format"] = text(v.toObject().value("key"));
                            items.replace(i, row);
                        }
                        break;
                    }
            }
            return emptyPage(items, shape == "comics" ? offset + items.size() >= length
                                                      : items.size() < limit);
        }
        if ((ds == "mal" || ds == "comics") && listMethod == "discoverPageFiltered")
            return emptyPage({}, true);
        if (ds == "mal" && method == "genreEntries")
            return QJsonArray();
    }
    QStringList families;
    if (ds == "imdb")
        families = {"imdb"};
    else if (ds == "mal")
        families = text(at(2)).isEmpty() ? QStringList{"anime", "manga"} : QStringList{text(at(2))};
    else if (ds == "comics")
        families = {"comics"};
    const auto directories = index.value("search").toObject();
    if (method == "search" || method == "matchByTitle" || method == "exactMatches") {
        if (ds == "comics" && method == "search")
            return std::nullopt; // Explicit I05 review decision: count real Worker usage first.
        const QString norm = ds == "comics" ? text(at(0)).trimmed() : text(at(0));
        if (norm.isEmpty())
            return QJsonArray();
        if (ds == "mal")
            for (const auto &f : families)
                if (f != "anime" && f != "manga")
                    return QJsonArray();
        QJsonArray out;
        const bool exact = method != "search", wildcard = norm.contains('%') || norm.contains('_');
        const auto target = fold(norm);
        for (const auto &family : families) {
            if (!directories.contains(family))
                return std::nullopt;
            const auto directory = object(directories.value(family));
            QHash<QString, QJsonArray> found;
            for (const auto &value : array(directory.value("names"))) {
                const auto ref = object(value);
                if (!wildcard &&
                    (fold(text(ref.value("last"))) < target ||
                     fold(text(ref.value("first"))) > (exact ? target : target + QChar(0xffff))))
                    continue;
                for (const auto &item : array(read(ref.value("path").toString()))) {
                    const auto e = array(item);
                    if (e.size() < 8)
                        throw QStringLiteral("STATIC_INVALID_NAME");
                    const auto name = e.at(0).toString();
                    const bool match = ds == "comics" ? fold(name) == target
                                       : exact        ? name == norm
                                                      : name.startsWith(norm);
                    if (match && (!exact || ds == "comics" || integer(at(1)) == 0 ||
                                  e.at(5) == QJsonValue(integer(at(1)))))
                        found[text(e.at(1))] = e;
                }
            }
            auto rows = found.values();
            std::sort(rows.begin(), rows.end(), [&](const auto &a, const auto &b) {
                if (ds == "comics") {
                    const int d = desc(a.at(6), b.at(6));
                    return d ? d < 0 : integer(a.at(7)) < integer(b.at(7));
                }
                if (!exact) {
                    const int aExact = a.at(4) == norm ? 0 : 1, bExact = b.at(4) == norm ? 0 : 1;
                    if (aExact != bExact)
                        return aExact < bExact;
                }
                for (const int column : ds == "imdb" ? QList<int>{5, 6, 7} : QList<int>{5, 6}) {
                    const int d = desc(a.at(column), b.at(column));
                    if (d)
                        return d < 0;
                }
                return ds == "imdb" ? text(a.at(1)) < text(b.at(1))
                                    : integer(a.at(1)) < integer(b.at(1));
            });
            const qint64 n = ds == "comics" ? (at(1).isUndefined() ? 30 : integer(at(1)))
                                            : clamp(at(1), ds == "imdb" ? 100 : 20);
            qsizetype emitted = 0;
            for (const auto &e : rows) {
                if (!exact && n >= 0 && emitted >= n)
                    break;
                const auto records = array(read(e.at(2).toString()));
                const auto ix = integer(e.at(3));
                if (ix < 0 || ix >= records.size())
                    throw QStringLiteral("STATIC_INVALID_RECORD");
                out.append(object(records.at(ix)).value("identity"));
                ++emitted;
            }
        }
        return out;
    }
    auto byId = [&](const QString &family, const QJsonValue &id) -> std::optional<QJsonValue> {
        if (!directories.contains(family))
            return std::nullopt;
        const auto directory = object(directories.value(family));
        for (const auto &value : array(directory.value("keys"))) {
            const auto ref = object(value);
            const bool inside = id.isString() ? (text(ref.value("first")) <= id.toString() &&
                                                 text(ref.value("last")) >= id.toString())
                                              : (ref.value("first").toDouble() <= id.toDouble() &&
                                                 ref.value("last").toDouble() >= id.toDouble());
            if (!inside)
                continue;
            for (const auto &item : array(read(ref.value("path").toString()))) {
                const auto e = array(item);
                if (e.size() < 3)
                    throw QStringLiteral("STATIC_INVALID_KEY");
                if (e.at(0) != id)
                    continue;
                if (e.size() > 3 && !e.at(3).isNull())
                    return e.at(3);
                const auto records = array(read(e.at(1).toString()));
                const auto ix = integer(e.at(2));
                if (ix < 0 || ix >= records.size())
                    throw QStringLiteral("STATIC_INVALID_RECORD");
                return records.at(ix);
            }
            return QJsonValue(QJsonValue::Null);
        }
        return QJsonValue(QJsonValue::Null);
    };
    if (ds == "mal" && method == "mangaById") {
        const auto row = byId("manga", integer(at(0)));
        if (!row)
            return std::nullopt;
        return row->isNull() ? QJsonValue(QJsonObject()) : object(*row).value("normal");
    }
    if (ds == "imdb" && method == "titlePresentation") {
        const auto row = byId("imdb", text(at(0)));
        if (!row)
            return std::nullopt;
        if (row->isNull())
            return QJsonObject();
        const auto identity = object(*row).value("identity").toObject();
        return QJsonObject{{"title", identity.value("title")}, {"type", identity.value("type")}};
    }
    if (ds == "imdb" && method == "titleFacts") {
        QJsonObject result;
        const auto ids = at(0).toArray();
        for (qsizetype i = 0; i < ids.size(); i += 100) {
            QList<QPair<QString, QJsonObject>> found;
            for (qsizetype n = i; n < std::min(i + 100, ids.size()); ++n) {
                const auto id = text(ids.at(n));
                const auto row = byId("imdb", id);
                if (!row)
                    return std::nullopt;
                if (!row->isNull())
                    found.append({id, object(*row)});
            }
            std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
                return integer(a.second.value("ordinal")) < integer(b.second.value("ordinal"));
            });
            for (const auto &pair : found) {
                const auto id = pair.second.value("identity").toObject();
                result[pair.first] = QJsonObject{{"rating", id.value("rating")},
                                                 {"votes", id.value("votes")},
                                                 {"isAnime", id.value("isAnime")}};
            }
        }
        return result;
    }
    return std::nullopt;
}
} // namespace

struct StaticCatalogueResolver::Impl {
    struct Loaded {
        QString path, where;
        QJsonValue value;
        qint64 bytes = 0;
        double ms = 0;
        double diskIoMs = 0, diskDecodeMs = 0, diskTouchMs = 0;
        QString workerThreadId;  // Populated only by actual background file work.
    };
    using AssetCallback = std::function<void(Loaded, QString)>;
    // File work is immutable once admitted. Only the resolver thread owns the
    // queue, coalesced waiters, cache, generations and network transfer state.
    struct FileWork {
        QString path, expected, filename, source;
        QByteArray raw;
        QVariantMap trace;
        quint64 generation = 0;
        double networkMs = 0;
    };
    struct FileResult {
        Loaded loaded;
        QByteArray raw;
        QString error;
        bool diskMiss = false, diskInvalid = false;
    };
    struct Job {
        quint64 id = 0;
        QString dataset, method, nativeKey;
        QJsonArray args;
        QPointer<QObject> context;
        Callback callback;
        QJsonObject index;
        QHash<QString, Loaded> files;
        QVariantList events;
        QElapsedTimer clock;
        qint64 bytes = 0;
        int restarts = 0;
        int evaluationRuns = 0;
        double evaluationMs = 0;
        quint64 validatedFactsTuples = 0;
        QSet<QString> validatedFactsBlocks;
        QStringList bucketPaths;
        QSet<QString> bucketPending;
        QHash<QString, QString> bucketErrors;
        int bucketCursor = 0;
        int bucketSelectedPrefixes = 0;
        int bucketUniquePaths = 0;
        int bucketAdmissions = 0;
        int bucketPeakPending = 0;
        int bucketPrefetchWaves = 0;
        quint64 bucketGeneration = 0;
        bool bucketPrefetching = false, bucketPrefetched = false;
        bool refreshedMissing = false;
        QPointer<QTimer> deadline;
        QMetaObject::Connection destroyed;
    };
    struct Transfer {
        QString path, etag;
        int cap = 0;
        std::function<void(int, QByteArray, QString, QString, double)> callback;
        quint64 generation = 0;  // 0 is reserved for /catalogue/current.json.
    };
    StaticCatalogueResolver *q;
    QPointer<QNetworkAccessManager> nam;
    QUrl endpoint;
    QString directory, currentEtag;
    QJsonObject entry, index;
    QDateTime checked;
    QHash<quint64, QSharedPointer<Job>> jobs;
    QSet<quint64> entryWaiters;
    QCache<QString, Loaded> memory{32 * 1024 * 1024};
    QHash<QString, QList<AssetCallback>> loading;
    QHash<QString, QSet<quint64>> assetOwners;
    QHash<QString, quint64> fileGenerations;
    QQueue<FileWork> fileQueue;
    quint64 nextFileGeneration = 0;
    int activeFileWork = 0, peakFileWork = 0, peakCombinedWork = 0;
    QSet<quint64> rejected;
    QList<Loaded> bootstrap;
    QQueue<Transfer> transfers;
    QList<QPointer<QNetworkReply>> replies;
    bool checking = false, unavailable = false, hardFailure = false;
    QString entryError;
    quint64 nextId = 0;
    qint64 workingBytes = 0;
    int active = 0;
    QVariantMap traceJob(const QSharedPointer<Job>& job) const {
        if (!CatalogueTrace::enabled()) return {};
        QVariantMap fields{{"component", "resolver"}, {"resolverId", CatalogueTrace::ownerId(q)}};
        if (job) fields = CatalogueTrace::merge(fields, {{"dataset", job->dataset}, {"method", job->method},
            {"queryKey", job->nativeKey}, {"jobId", QVariant::fromValue(job->id)}});
        return fields;
    }
    QVariantMap traceFile(const QString& path, quint64 owner = 0) const {
        if (!CatalogueTrace::enabled()) return {};
        auto ids = assetOwners.value(path);
        if (owner) ids.insert(owner);
        if (path == "/catalogue/current.json" || ids.remove(0)) ids.unite(entryWaiters);
        QVariantMap fields = traceJob({});
        QVariantList owners;
        for (const auto id : ids) if (auto job = jobs.value(id)) {
            const auto context = traceJob(job); owners.append(context);
            if (owners.size() == 1) fields = context;
        }
        fields = CatalogueTrace::merge(fields, CatalogueTrace::file(path));
        fields.insert("owners", owners); // Shared transfers retain every exact-query correlation.
        return fields;
    }
    Impl(StaticCatalogueResolver *owner, QNetworkAccessManager *n, QUrl u, QString root)
        : q(owner), nam(n), endpoint(std::move(u)) {
        if (!endpoint.isValid() || endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty() ||
            !endpoint.query().isEmpty() || !endpoint.fragment().isEmpty() ||
            (endpoint.scheme() != "https" &&
             !(endpoint.scheme() == "http" &&
               (endpoint.host() == "127.0.0.1" || endpoint.host() == "localhost")))) {
            endpoint = {};
            nam = nullptr;
        }
        if (!root.isEmpty() && !endpoint.isEmpty())
            directory = QDir(root).filePath("static-catalogue-v1/" +
                                            digest(endpoint.toString(QUrl::FullyEncoded).toUtf8()));
        loadLastGood();
    }
    ~Impl() {
        for (const auto &r : replies)
            if (r) {
                r->disconnect(q);
                r->abort();
                r->deleteLater();
            }
    }
    QString pub() const {
        return entry.value("revision").toString();
    }
    QString pathFor(const QString &expected) const {
        return QDir(directory).filePath(expected + ".json");
    }
    bool validEntry(const QJsonObject &e) const {
        const auto revision = e.value("revision").toString(),
                   source = e.value("sourceRevision").toString();
        const auto ref = e.value("index").toObject();
        const auto expected = ref.value("sha256").toString();
        const auto size = ref.value("bytes").toDouble(-1);
        return e.value("schemaVersion") == 1 && hash(revision) && hash(source) && hash(expected) &&
               size > 0 && size <= fileCap && size == qint64(size) &&
               ref.value("path") == "/catalogue/" + revision + "/index-" + expected + ".json";
    }
    bool validIndex(const QJsonObject &v, const QJsonObject &e) const {
        if (v.value("schemaVersion") != 1 || v.value("version") != 1 ||
            v.value("revision") != e.value("revision") ||
            v.value("sourceRevision") != e.value("sourceRevision"))
            return false;
        for (const auto &name : {"lists", "fixed", "search"})
            if (!v.value(QLatin1String(name)).isObject())
                return false;
        return true;
    }
    Loaded decode(const QString &path, const QByteArray &raw, const QString &expected, qint64 size,
                  const QString &where, double ms, quint64 owner = 0) {
        if (raw.size() > fileCap || (size >= 0 && raw.size() != size) || digest(raw) != expected)
            throw QStringLiteral("STATIC_HASH_MISMATCH");
        return {path, where, parse(raw, CatalogueTrace::merge(traceFile(path, owner), {{"source", where}})), raw.size(), ms};
    }
    void loadLastGood() {
        if (directory.isEmpty())
            return;
        QElapsedTimer clock;
        clock.start();
        QFile file(QDir(directory).filePath("last-good.json"));
        const auto wrapperTrace = CatalogueTrace::merge(traceJob({}), CatalogueTrace::file(file.fileName()));
        CatalogueTrace::Span wrapperRead("disk_read", CatalogueTrace::merge(wrapperTrace, {{"source", "disk"}}));
        if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16384)
            return;
        try {
            const auto wrapperRaw = file.readAll();
            wrapperRead.finish("success", {{"bytes", wrapperRaw.size()}});
            const auto wrapper = object(parse(wrapperRaw, CatalogueTrace::merge(wrapperTrace, {{"source", "disk"}})));
            if (wrapper.value("endpoint") != endpoint.toString(QUrl::FullyEncoded))
                return;
            const auto candidate = wrapper.value("entry").toObject();
            if (!validEntry(candidate))
                return;
            const auto ref = candidate.value("index").toObject();
            QFile data(pathFor(ref.value("sha256").toString()));
            CatalogueTrace::Span indexRead("disk_read", CatalogueTrace::merge(traceFile(ref.value("path").toString()), {{"source", "disk"}}));
            if (QFileInfo(data).isSymLink() || !data.open(QIODevice::ReadOnly) ||
                data.size() > fileCap)
                return;
            const auto indexRaw = data.readAll(); indexRead.finish("success", {{"bytes", indexRaw.size()}});
            auto loaded =
                decode(ref.value("path").toString(), indexRaw, ref.value("sha256").toString(),
                       qint64(ref.value("bytes").toDouble()), "disk", 0);
            if (!loaded.value.isObject() || !validIndex(loaded.value.toObject(), candidate))
                return;
            loaded.ms = clock.nsecsElapsed() / 1e6;
            bootstrap.append(Loaded{"/catalogue/current.json", "disk", {}, file.size(), 0});
            bootstrap.append(loaded);
            entry = candidate;
            index = loaded.value.toObject();
            const auto validatedAt = QDateTime::fromString(wrapper.value("validatedAt").toString(), Qt::ISODateWithMs);
            const auto age = validatedAt.msecsTo(QDateTime::currentDateTimeUtc());
            // Preserve the existing 60-second revalidation window across process
            // restarts, but never trust a future or unvalidated timestamp.
            if (validatedAt.isValid() && age >= 0 && age < 60000) checked = validatedAt;
            currentEtag = wrapper.value("etag").toString();
            if (currentEtag.size() > 1024 || currentEtag.contains('\r') ||
                currentEtag.contains('\n'))
                currentEtag.clear();
        } catch (const QString &) {
        }
    }
    void atomic(const QString &path, const QByteArray &raw, const QVariantMap& context = {}) {
        CatalogueTrace::Span trace("disk_write", context.isEmpty() ? CatalogueTrace::merge(traceJob({}), CatalogueTrace::file(path)) : context);
        QSaveFile out(path);
        out.setDirectWriteFallback(false);
        if (out.open(QIODevice::WriteOnly) && out.write(raw) == raw.size()) {
            const bool committed = out.commit();
            trace.finish(committed ? "success" : "failure", {{"bytes", raw.size()}});
        }
    }
    void prune() {
        auto files = QDir(directory).entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks,
                                                   QDir::Time | QDir::Reversed);
        qint64 total = 0;
        for (const auto &f : files)
            if (hash(f.completeBaseName()))
                total += f.size();
        const auto pin = entry.value("index").toObject().value("sha256").toString();
        for (const auto &f : files) {
            if (total <= diskCap)
                break;
            if (hash(f.completeBaseName()) && f.completeBaseName() != pin &&
                QFile::remove(f.absoluteFilePath()))
                total -= f.size();
        }
    }
    void saveLastGood() {
        if (directory.isEmpty() || !QDir().mkpath(directory))
            return;
        atomic(QDir(directory).filePath("last-good.json"),
               json(QJsonObject{{"endpoint", endpoint.toString(QUrl::FullyEncoded)},
                                {"entry", entry},
                                {"etag", currentEtag},
                                {"validatedAt", checked.toString(Qt::ISODateWithMs)}}));
        prune();
    }
    // This function is the entire background execution boundary: the input
    // contains only immutable values, never Impl, QObject, job, queue or cache.
    static FileResult processFile(FileWork work) {
        FileResult out;
        QElapsedTimer timer;
        timer.start();
        QByteArray raw;
        double ioMs = 0;
        std::optional<QFile> diskFile;
        if (work.source == "disk") {
            diskFile.emplace(work.filename);
            CatalogueTrace::Span diskTrace("disk_read", work.trace);
            if (QFileInfo(*diskFile).isSymLink() ||
                !diskFile->open(QIODevice::ReadOnly) ||
                diskFile->size() < 0 || diskFile->size() > fileCap) {
                out.diskMiss = true;
                diskTrace.finish("miss");
                return out;
            }
            raw = diskFile->readAll();
            ioMs = timer.nsecsElapsed() / 1e6;
            diskTrace.finish("success", {{"bytes", raw.size()}});
        } else {
            raw = std::move(work.raw);
        }
        try {
            if (raw.size() > fileCap || digest(raw) != work.expected)
                throw QStringLiteral("STATIC_HASH_MISMATCH");
            const auto value = parse(raw, work.trace);
            const double decodedAtMs = timer.nsecsElapsed() / 1e6;
            out.loaded = {work.path, work.source, value, raw.size(), 0};
            out.loaded.workerThreadId =
                QString::number(quintptr(QThread::currentThreadId()), 16);
            if (work.source == "disk") {
                diskFile->setFileTime(QDateTime::currentDateTimeUtc(),
                                      QFileDevice::FileModificationTime);
                out.loaded.ms = timer.nsecsElapsed() / 1e6;
                out.loaded.diskIoMs = ioMs;
                out.loaded.diskDecodeMs = decodedAtMs - ioMs;
                out.loaded.diskTouchMs = out.loaded.ms - decodedAtMs;
            } else {
                out.loaded.ms = work.networkMs + timer.nsecsElapsed() / 1e6;
                out.raw = std::move(raw);  // Persist only after owner accepts token.
            }
        } catch (const QString &error) {
            out.error = error;
            out.diskInvalid = work.source == "disk";
        }
        return out;
    }
    bool isLive(const QString &path, quint64 token) const {
        return token != 0 && fileGenerations.value(path, 0) == token
               && loading.contains(path);
    }
    void completeAsset(const QString &path, quint64 token, Loaded loaded, QString error) {
        if (!isLive(path, token))
            return;
        const auto callbacks = loading.take(path);
        fileGenerations.remove(path);
        assetOwners.remove(path);
        bool first = true;
        for (const auto &callback : callbacks) {
            auto row = loaded;
            if (!first) row.where = "shared";
            first = false;
            callback(row, error);
        }
    }
    void beginNetworkAsset(const QString &path, const QString &expected, quint64 token) {
        if (!isLive(path, token))
            return;
        network({path, {}, int(fileCap),
                 [this, path, expected, token](int status, QByteArray raw, QString error,
                                              QString, double ms) {
                     if (!isLive(path, token))
                         return;
                     Loaded empty{path, nam ? "network" : "offline", {}, raw.size(), ms};
                     if (error.isEmpty() && status != 200)
                         error = "STATIC_HTTP_" + QString::number(status);
                     if (!error.isEmpty()) {
                         completeAsset(path, token, empty, error);
                         return;
                     }
                     FileWork work;
                     work.path = path;
                     work.expected = expected;
                     work.source = "network";
                     work.raw = std::move(raw);
                     work.trace = CatalogueTrace::merge(traceFile(path), {{"source", "network"}});
                     work.generation = token;
                     work.networkMs = ms;
                     fileQueue.enqueue(std::move(work));
                     pump();
                 }, token});
    }
    void startFileWork() {
        // Count running/submitted work together with the original four network
        // slots. Queued disk work is descriptors only; network payloads cannot
        // accumulate because each completed download occupies its slot as a
        // decode before another download is admitted.
        while (active + activeFileWork < 4 && !fileQueue.isEmpty()) {
            auto work = fileQueue.dequeue();
            if (!isLive(work.path, work.generation))
                continue;
            const auto path = work.path, expected = work.expected;
            const auto token = work.generation;
            const auto source = work.source;
            ++activeFileWork;
            peakFileWork = std::max(peakFileWork, activeFileWork);
            peakCombinedWork = std::max(peakCombinedWork, activeFileWork + active);
            auto *watcher = new QFutureWatcher<FileResult>(q);
            QObject::connect(watcher, &QFutureWatcher<FileResult>::finished, q,
                             [this, watcher, path, expected, token, source] {
                const auto output = watcher->result();
                watcher->deleteLater();
                --activeFileWork;
                if (isLive(path, token)) {
                    if (source == "disk" && (output.diskMiss || output.diskInvalid)) {
                        if (output.diskInvalid)
                            QFile::remove(pathFor(expected));
                        beginNetworkAsset(path, expected, token);
                    } else {
                        if (output.error.isEmpty()) {
                            memory.insert(path, new Loaded(output.loaded),
                                          int(output.loaded.bytes));
                            if (source == "network" && !directory.isEmpty() &&
                                QDir().mkpath(directory)) {
                                atomic(pathFor(expected), output.raw, traceFile(path));
                                prune();
                            }
                        }
                        completeAsset(path, token, output.loaded, output.error);
                    }
                }
                pump();
            });
            // Future completion is observed by a QObject owned by the resolver.
            // Destroying the resolver disconnects the watcher without waiting
            // for the Qt global-pool worker or exposing its raw address to it.
            auto promise = QSharedPointer<QPromise<FileResult>>::create();
            watcher->setFuture(promise->future());
            QThreadPool::globalInstance()->start(
                [input = std::move(work), promise]() mutable {
                    promise->start();
                    promise->addResult(processFile(std::move(input)));
                    promise->finish();
                });
        }
    }
    void pump() {
        startFileWork();
        while (active + activeFileWork < 4 && !transfers.isEmpty()) {
            auto item = transfers.dequeue();
            if (item.generation && !isLive(item.path, item.generation))
                continue;
            if (item.path != "/catalogue/current.json" && !loading.contains(item.path))
                continue;
            if (!nam) {
                CatalogueTrace::event("network_bytes_done", CatalogueTrace::merge(traceFile(item.path), {{"source", "offline"}, {"httpStatus", 0}, {"bytes", 0}, {"status", "STATIC_OFFLINE"}}));
                item.callback(0, {}, QStringLiteral("STATIC_OFFLINE"), {}, 0);
                continue;
            }
            ++active;
            peakCombinedWork = std::max(peakCombinedWork, activeFileWork + active);
            CatalogueTrace::event("network_start", CatalogueTrace::merge(traceFile(item.path), {{"source", "network"}, {"activeTransfers", active}}));
            QNetworkRequest request(endpoint.resolved(QUrl(item.path)));
            request.setRawHeader("Accept-Encoding", "identity");
            request.setRawHeader("User-Agent", "Colosseum/1.1.9 StaticCatalogue/1");
            if (!item.etag.isEmpty())
                request.setRawHeader("If-None-Match", item.etag.toLatin1());
            request.setTransferTimeout(15000);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            auto *reply = nam->get(request);
            QObject::connect(reply, &QNetworkReply::socketStartedConnecting, q, [this, path=item.path] {
                CatalogueTrace::event("network_socket_connecting", traceFile(path));
            });
            QObject::connect(reply, &QNetworkReply::requestSent, q, [this, path=item.path] {
                CatalogueTrace::event("network_request_sent", traceFile(path));
            });
            reply->setProperty("staticPath", item.path);
            replies.append(reply);
            reply->setReadBufferSize(item.cap + 1);
            auto bytes = QSharedPointer<QByteArray>::create();
            auto clock = QSharedPointer<QElapsedTimer>::create();
            clock->start();
            QObject::connect(reply, &QNetworkReply::readyRead, q, [reply, bytes, cap = item.cap] {
                if (reply->isOpen())
                    bytes->append(reply->readAll());
                if (bytes->size() > cap)
                    reply->abort();
            });
            QObject::connect(reply, &QNetworkReply::finished, q, [this, reply, bytes, clock, item] {
                replies.removeAll(reply);
                reply->deleteLater();
                if (reply->isOpen())
                    bytes->append(reply->readAll());
                --active;
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                CatalogueTrace::event("network_bytes_done", CatalogueTrace::merge(traceFile(item.path),
                    {{"source", "network"}, {"httpStatus", status}, {"bytes", bytes->size()}, {"elapsedMs", clock->nsecsElapsed() / 1e6}}));
                QString error;
                if (bytes->size() > item.cap)
                    error = "STATIC_FILE_TOO_LARGE";
                else if (reply->error() != QNetworkReply::NoError && status != 304)
                    error = "STATIC_HTTP_" + QString::number(status);
                const auto etag = QString::fromLatin1(reply->rawHeader("ETag"));
                item.callback(status, *bytes, error, etag, clock->nsecsElapsed() / 1e6);
                pump();
            });
        }
    }
    void network(Transfer t) {
        CatalogueTrace::event("network_queued", CatalogueTrace::merge(traceFile(t.path), {{"queueDepth", transfers.size()}, {"activeTransfers", active}}));
        transfers.enqueue(std::move(t));
        pump();
    }
    void asset(const QString &path, const QString &expected, qint64 size, AssetCallback callback,
               quint64 owner = 0) {
        // Check each reference independently, even when requests coalesce.
        auto checked = [size, callback = std::move(callback)](Loaded value, QString error) {
            if (error.isEmpty() && size >= 0 && value.bytes != size)
                error = QStringLiteral("STATIC_HASH_MISMATCH");
            callback(std::move(value), std::move(error));
        };
        callback = std::move(checked);
        if (auto *cached = memory.object(path)) {
            CatalogueTrace::event("file_cache_hit", CatalogueTrace::merge(traceFile(path, owner),
                {{"source", "memory"}, {"bytes", cached->bytes}}));
            auto value = *cached;
            value.where = "memory";
            value.ms = 0;
            QTimer::singleShot(0, q, [callback, value] { callback(value, {}); });
            return;
        }
        assetOwners[path].insert(owner);
        if (loading.contains(path)) {
            CatalogueTrace::event("file_cache_hit", CatalogueTrace::merge(traceFile(path, owner),
                {{"source", "shared-inflight"}}));
            loading[path].append(std::move(callback));
            return;
        }
        loading[path].append(std::move(callback));
        const auto token = ++nextFileGeneration;
        fileGenerations.insert(path, token);
        if (directory.isEmpty()) {
            beginNetworkAsset(path, expected, token);
        } else {
            FileWork work;
            work.path = path;
            work.expected = expected;
            work.filename = pathFor(expected);
            work.source = "disk";
            work.trace = CatalogueTrace::merge(traceFile(path), {{"source", "disk"}});
            work.generation = token;
            fileQueue.enqueue(std::move(work));
            pump();
        }
    }
    void record(const QSharedPointer<Job> &j, const Loaded &f) {
        CatalogueTrace::event("file_use", CatalogueTrace::merge(traceJob(j), CatalogueTrace::merge(CatalogueTrace::file(f.path), {{"source", f.where}, {"bytes", f.bytes}})));
        j->events.append(QVariantMap{
            {"path", f.path}, {"source", f.where}, {"bytes", f.bytes}, {"readMs", f.ms},
            {"diskIoMs", f.diskIoMs}, {"diskDecodeMs", f.diskDecodeMs},
            {"diskTouchMs", f.diskTouchMs}, {"workerThreadId", f.workerThreadId}});
    }
    void resetBuckets(const QSharedPointer<Job> &j) {
        ++j->bucketGeneration;  // Fence every old callback, including disk timers.
        j->bucketPaths.clear();
        j->bucketPending.clear();
        j->bucketErrors.clear();
        j->bucketCursor = 0;
        j->bucketSelectedPrefixes = 0;
        j->bucketUniquePaths = 0;
        j->bucketPrefetching = false;
        j->bucketPrefetched = false;
    }
    void admitBuckets(quint64 id, quint64 generation) {
        const auto j = jobs.value(id);
        if (!j || !j->bucketPrefetching || j->bucketGeneration != generation)
            return;
        while (j->bucketPending.size() < 4 && j->bucketCursor < j->bucketPaths.size()) {
            const auto path = j->bucketPaths.at(j->bucketCursor++);
            if (j->files.contains(path) || j->bucketErrors.contains(path))
                continue;
            if (j->files.size() + j->bucketPending.size() >= filesCap) {
                finish(id, Status::Failure, {}, "STATIC_QUERY_FILE_LIMIT");
                return;
            }
            j->bucketPending.insert(path);
            ++j->bucketAdmissions;
            j->bucketPeakPending = std::max(j->bucketPeakPending, int(j->bucketPending.size()));
            asset(path, path.mid(path.size() - 69, 64), -1,
                  [this, id, generation, path](Loaded loaded, QString error) {
                      const auto job = jobs.value(id);
                      if (!job || job->bucketGeneration != generation || !job->bucketPrefetching)
                          return;
                      job->bucketPending.remove(path);
                      if (job->index.value("revision") != index.value("revision")) {
                          QTimer::singleShot(0, q, [this, id] { run(id); });
                          return;
                      }
                      // A shared empty array is one asset for this job, but its
                      // semantic validation remains independent per prefix.
                      if (!job->files.contains(path) && !job->bucketErrors.contains(path)) {
                          record(job, loaded);
                          if (error.isEmpty() &&
                              (job->bytes + loaded.bytes > jobCap ||
                               workingBytes + loaded.bytes > allJobsCap))
                              error = QStringLiteral("STATIC_QUERY_BYTE_LIMIT");
                          if (error.isEmpty()) {
                              job->bytes += loaded.bytes;
                              workingBytes += loaded.bytes;
                              job->files.insert(path, loaded);
                          } else {
                              job->bucketErrors.insert(path, error);
                              // Paths were admitted in prefix order. Remaining
                              // unadmitted paths are later than this failure:
                              // let already-pending earlier files settle, but
                              // do not spend reads on a query that cannot succeed.
                              job->bucketCursor = job->bucketPaths.size();
                          }
                      }
                      // Yield before refilling to let other jobs enter the
                      // existing FIFO global four-transfer scheduler.
                      QTimer::singleShot(0, q, [this, id, generation] {
                          admitBuckets(id, generation);
                      });
                  }, id);
        }
        if (j->bucketCursor == j->bucketPaths.size() && j->bucketPending.isEmpty()) {
            j->bucketPrefetching = false;
            j->bucketPrefetched = true;
            QTimer::singleShot(0, q, [this, id] { run(id); });
        }
    }
    void cancelIO(quint64 id) {
        const auto paths = assetOwners.keys();
        for (const auto &path : paths) {
            assetOwners[path].remove(id);
            if (assetOwners[path].isEmpty()) {
                assetOwners.remove(path);
                loading.remove(path);
                fileGenerations.remove(path);
                const auto activeReplies = replies;
                for (const auto &r : activeReplies)
                    if (r && r->property("staticPath") == path)
                        r->abort();
            }
        }
        // Drop obsolete queued inputs immediately, particularly downloaded
        // network byte arrays, rather than waiting for a pool slot to free.
        for (auto it = fileQueue.begin(); it != fileQueue.end(); ) {
            if (!isLive(it->path, it->generation))
                it = fileQueue.erase(it);
            else
                ++it;
        }
        pump();
    }
    void finish(quint64 id, Status status, QJsonValue value = {}, QString error = {}) {
        const auto j = jobs.take(id);
        if (!j)
            return;
        QObject::disconnect(j->destroyed);
        if (j->deadline) {
            j->deadline->stop();
            j->deadline->deleteLater();
        }
        cancelIO(id);
        entryWaiters.remove(id);
        workingBytes -= j->bytes;
        Outcome out;
        out.status = status;
        out.result = value;
        out.error = error;
        out.publication = j->index.value("revision").toString();
        out.sourceRevision = j->index.value("sourceRevision").toString();
        if (j->dataset == "content" && status == Status::Prepared)
            out.sourceRevision =
                value.toObject().value("manifest").toObject().value("revision").toString();
        if (j->dataset == "tankoban")
            out.sourceRevision = j->index.value("tankobanSourceHash").toString();
        qint64 networkBytes = 0, diskBytes = 0, maxBytes = 0;
        int networkFiles = 0, diskFiles = 0, memoryFiles = 0, sharedFiles = 0;
        int networkWorkerLoads = 0, diskWorkerLoads = 0;
        QSet<QString> workerThreadIds;
        double maxMs = 0, largestMs = 0;
        for (const auto &v : j->events) {
            const auto e = v.toMap();
            const auto n = e.value("bytes").toLongLong();
            const auto from = e.value("source").toString();
            if (from == "network") {
                ++networkFiles;
                networkBytes += n;
            } else if (from == "disk") {
                ++diskFiles;
                diskBytes += n;
            } else if (from == "memory")
                ++memoryFiles;
            else if (from == "shared")
                ++sharedFiles;
            const auto worker = e.value("workerThreadId").toString();
            if (!worker.isEmpty() && (from == "disk" || from == "network")) {
                workerThreadIds.insert(worker);
                if (from == "disk") ++diskWorkerLoads;
                else ++networkWorkerLoads;
            }
            const double readMs = e.value("readMs").toDouble();
            if (n > maxBytes) {
                maxBytes = n;
                largestMs = readMs;
            }
            maxMs = std::max(maxMs, readMs);
        }
        out.metrics = {{"logicalFiles", j->events.size()},
                       {"networkFiles", networkFiles},
                       {"networkBytes", networkBytes},
                       {"diskFiles", diskFiles},
                       {"diskBytes", diskBytes},
                       {"memoryFiles", memoryFiles},
                       {"sharedFiles", sharedFiles},
                       {"largestFileBytes", maxBytes},
                       {"largestFileReadMs", largestMs},
                       {"slowestReadMs", maxMs},
                       {"fileWorkerDiskLoads", diskWorkerLoads},
                       {"fileWorkerNetworkLoads", networkWorkerLoads},
                       {"fileWorkerThreadIds", workerThreadIds.values()},
                       {"fileWorkPeak", peakFileWork},
                       {"combinedWorkPeak", peakCombinedWork},
                       {"ownerThreadId", QString::number(quintptr(QThread::currentThreadId()), 16)},
                       {"elapsedMs", j->clock.nsecsElapsed() / 1e6},
                       {"evaluationRuns", j->evaluationRuns},
                       {"evaluationMs", j->evaluationMs},
                       {"validatedFactsTuples", QVariant::fromValue(j->validatedFactsTuples)},
                       {"factsMode", j->dataset == "imdb" && j->method == "titleFacts"
                          ? (j->index.value("facts").toObject().value("imdb").toObject()
                              .contains("hashBuckets") ? "hashBuckets-v1" : "aligned-v1")
                          : "none"},
                       {"bucketSelectedPrefixes", j->bucketSelectedPrefixes},
                       {"bucketUniquePaths", j->bucketUniquePaths},
                       {"bucketAdmissions", j->bucketAdmissions},
                       {"bucketPeakPending", j->bucketPeakPending},
                       {"bucketPrefetchWaves", j->bucketPrefetchWaves},
                       {"files", j->events},
                       {"outcome", status == Status::Prepared      ? "prepared"
                                   : status == Status::NotPrepared ? "worker"
                                                                   : "failure"}};
        auto summary = out.metrics;
        summary.remove("files");
        summary["dataset"] = j->dataset;
        summary["method"] = j->method;
        qCInfo(staticCatalogueLog).noquote()
            << QString::fromUtf8(json(QJsonObject::fromVariantMap(summary)));
        if (j->context) {
            emit q->measured(j->dataset, j->method, out.metrics);
            if (j->context) {
                const auto context = CatalogueTrace::merge(traceJob(j), {{"status", out.metrics.value("outcome")}, {"reason", out.error}, {"logicalFiles", j->events.size()}});
                CatalogueTrace::event("result_delivery_begin", CatalogueTrace::merge(context,
                    {{"status", out.metrics.value("outcome")}, {"logicalFiles", j->events.size()}, {"elapsedMs", j->clock.nsecsElapsed() / 1e6}}));
                j->callback(std::move(out));
                CatalogueTrace::event("result_delivery_end", context);
            }
        }
    }
    void run(quint64 id) {
        const auto j = jobs.value(id);
        if (!j)
            return;
        CatalogueTrace::event("resolver_evaluate", traceJob(j));
        if (!j->context) {
            finish(id, Status::Failure, {}, "STATIC_CANCELLED");
            return;
        }
        if (j->index.value("revision") != index.value("revision")) {
            if (++j->restarts > 2) {
                finish(id, Status::Failure, {}, "STATIC_REVISION_CHANGED");
                return;
            }
            workingBytes -= j->bytes;
            j->bytes = 0;
            j->files.clear();
            j->validatedFactsBlocks.clear();
            resetBuckets(j);
            cancelIO(id);
            j->index = index;
        }
        if (j->bucketPrefetching)
            return;
        CatalogueTrace::Span evaluation("evaluate", traceJob(j));
        QElapsedTimer evalClock;
        evalClock.start();
        ++j->evaluationRuns;
        try {
            const auto read = [&](const QString &path) -> QJsonValue {
                const auto prefix =
                    "/catalogue/" + j->index.value("revision").toString() + "/shared/";
                static const QRegularExpression leaf(QStringLiteral("^[a-f0-9]{64}\\.json$"));
                if (!path.startsWith(prefix) || !leaf.match(path.mid(prefix.size())).hasMatch())
                    throw QStringLiteral("STATIC_INVALID_PATH");
                // Prefetch errors are staged, never delivered out of prefix order.
                if (j->bucketErrors.contains(path))
                    throw j->bucketErrors.value(path);
                if (j->files.contains(path))
                    return j->files.value(path).value;
                throw Need{path};
            };
            const auto value = evaluate(j->index, j->dataset, j->method, j->args, read,
                                        &j->validatedFactsTuples, &j->validatedFactsBlocks,
                                        j->bucketPrefetched);
            j->evaluationMs += evalClock.nsecsElapsed() / 1e6;
            if (!value) {
                evaluation.finish("not-prepared");
                finish(id, Status::NotPrepared);
                return;
            }
            CatalogueTrace::Span resultEncoding("result_encode", traceJob(j));
            const auto resultBytes = json(*value).size();
            resultEncoding.finish("success", {{"bytes", resultBytes}});
            if (resultBytes > 4 * 1024 * 1024)
                throw QStringLiteral("STATIC_RESULT_TOO_LARGE");
            evaluation.finish("prepared");
            finish(id, Status::Prepared, *value);
        } catch (const NeedBatch &batch) {
            j->evaluationMs += evalClock.nsecsElapsed() / 1e6;
            evaluation.finish("needs-bucket-prefetch");
            if (j->files.size() + batch.paths.size() > filesCap) {
                finish(id, Status::Failure, {}, "STATIC_QUERY_FILE_LIMIT");
                return;
            }
            j->bucketPaths = batch.paths;
            j->bucketCursor = 0;
            j->bucketPending.clear();
            j->bucketErrors.clear();
            j->bucketSelectedPrefixes = batch.selectedPrefixes;
            j->bucketUniquePaths = batch.paths.size();
            j->bucketPrefetching = true;
            ++j->bucketPrefetchWaves;
            const auto generation = ++j->bucketGeneration;
            admitBuckets(id, generation);
        } catch (const Need &need) {
            j->evaluationMs += evalClock.nsecsElapsed() / 1e6;
            evaluation.finish("needs-file");
            if (j->files.size() >= filesCap) {
                finish(id, Status::Failure, {}, "STATIC_QUERY_FILE_LIMIT");
                return;
            }
            asset(
                need.path, need.path.mid(need.path.size() - 69, 64), -1,
                [this, id](Loaded loaded, QString error) {
                    const auto job = jobs.value(id);
                    if (!job)
                        return;
                    record(job, loaded);
                    if (!error.isEmpty()) {
                        // A deployment may retire the last publication's paths
                        // while this client still has its short-lived entry.
                        // Revalidate once; an unchanged/malformed publication
                        // still fails, never becomes empty or SQL fallback.
                        if (!job->refreshedMissing &&
                            (error == "STATIC_HTTP_404" || error == "STATIC_HTTP_410")) {
                            job->refreshedMissing = true;
                            checked = {};
                            ensure(id);
                            return;
                        }
                        finish(id, Status::Failure, {}, error);
                        return;
                    }
                    if (job->bytes + loaded.bytes > jobCap ||
                        workingBytes + loaded.bytes > allJobsCap) {
                        finish(id, Status::Failure, {}, "STATIC_QUERY_BYTE_LIMIT");
                        return;
                    }
                    job->bytes += loaded.bytes;
                    workingBytes += loaded.bytes;
                    job->files.insert(loaded.path, loaded);
                    QTimer::singleShot(0, q, [this, id] { run(id); });
                },
                id);
        } catch (const QString &why) {
            j->evaluationMs += evalClock.nsecsElapsed() / 1e6;
            evaluation.finish("failure", {{"reason", why}});
            if (j->bucketPrefetched && !j->refreshedMissing &&
                (why == "STATIC_HTTP_404" || why == "STATIC_HTTP_410")) {
                j->refreshedMissing = true;
                checked = {};
                resetBuckets(j);
                ensure(id);
                return;
            }
            finish(id, Status::Failure, {}, why);
        }
    }
    void releaseWaiters(const QList<Loaded> &events = {}) {
        auto ids = entryWaiters.values();
        std::sort(ids.begin(), ids.end());
        entryWaiters.clear();
        bool first = true;
        const auto startup = std::exchange(bootstrap, {});
        for (auto id : ids) {
            const auto j = jobs.value(id);
            if (!j)
                continue;
            for (auto e : startup + events) {
                if (!first)
                    e.where = "shared";
                record(j, e);
            }
            first = false;
            if (!j->index.isEmpty() && j->index.value("revision") != index.value("revision")) {
                if (++j->restarts > 2) {
                    finish(id, Status::Failure, {}, "STATIC_REVISION_CHANGED");
                    continue;
                }
                workingBytes -= j->bytes;
                j->bytes = 0;
                j->files.clear();
                j->validatedFactsBlocks.clear();
                resetBuckets(j);
                cancelIO(id);
            }
            j->index = index;
            if (unavailable) {
                // An authoritative optional-entry absence announces a legacy-only
                // server (including rollback). Retain last-good disk data for
                // outages, but do not pin this online request to its old source.
                j->index = {};
                finish(id, Status::NotPrepared);
                continue;
            }
            if (index.isEmpty() || hardFailure)
                finish(id, unavailable ? Status::NotPrepared : Status::Failure, {}, entryError);
            else
                QTimer::singleShot(0, q, [this, id] { run(id); });
        }
    }
    void ensure(quint64 id) {
        entryWaiters.insert(id);
        if (checking)
            return;
        if (checked.isValid() && checked.msecsTo(QDateTime::currentDateTimeUtc()) >= 0
            && checked.msecsTo(QDateTime::currentDateTimeUtc()) < 60000) {
            releaseWaiters();
            return;
        }
        if (!nam) {
            checked = QDateTime::currentDateTimeUtc();
            entryError = index.isEmpty() ? "STATIC_OFFLINE" : "";
            releaseWaiters();
            return;
        }
        checking = true;
        network({"/catalogue/current.json", currentEtag, 16384,
                 [this](int status, QByteArray raw, QString error, QString etag, double ms) {
                     const Loaded current{"/catalogue/current.json", "network", {}, raw.size(), ms};
                     checked = QDateTime::currentDateTimeUtc();
                     unavailable = false;
                     hardFailure = false;
                     if (status == 304 && error.isEmpty() && !index.isEmpty()) {
                         checking = false;
                         entryError.clear();
                         saveLastGood();
                         releaseWaiters({current});
                         return;
                     }
                     if (!error.isEmpty() || status != 200) {
                         checking = false;
                         entryError =
                             error.isEmpty() ? "STATIC_HTTP_" + QString::number(status) : error;
                         hardFailure =
                             error == "STATIC_FILE_TOO_LARGE" || (status >= 300 && status < 400);
                         unavailable = status == 401 || status == 404 || status == 410 || status == 501;
                         releaseWaiters({current});
                         return;
                     }
                     QJsonObject candidate;
                     try {
                         candidate = object(parse(raw, CatalogueTrace::merge(traceFile("/catalogue/current.json"), {{"source", "network"}})));
                         if (!validEntry(candidate))
                             throw QStringLiteral("STATIC_INVALID_ENTRY");
                     } catch (const QString &why) {
                         checking = false;
                         hardFailure = true;
                         entryError = why;
                         releaseWaiters({current});
                         return;
                     }
                     const auto ref = candidate.value("index").toObject();
                     asset(ref.value("path").toString(), ref.value("sha256").toString(),
                           qint64(ref.value("bytes").toDouble()),
                           [this, candidate, etag, current](Loaded loaded, QString why) {
                               checking = false;
                               if (!why.isEmpty() || !loaded.value.isObject() ||
                                   !validIndex(loaded.value.toObject(), candidate)) {
                                   hardFailure = true;
                                   entryError = why.isEmpty() ? "STATIC_INVALID_INDEX" : why;
                                   releaseWaiters({current, loaded});
                                   return;
                               }
                               const auto previous = pub();
                               entry = candidate;
                               index = loaded.value.toObject();
                               entryError.clear();
                               currentEtag = etag.size() <= 1024 && !etag.contains('\r') &&
                                                     !etag.contains('\n')
                                                 ? etag
                                                 : QString();
                               saveLastGood();
                               if (previous != pub())
                                   emit q->publicationChanged(pub());
                               releaseWaiters({current, loaded});
                           });
                 }});
    }
};

StaticCatalogueResolver::StaticCatalogueResolver(QNetworkAccessManager *n, QUrl u, QString c,
                                                 QObject *p)
    : QObject(p), d(std::make_unique<Impl>(this, n, std::move(u), std::move(c))) {}
StaticCatalogueResolver::~StaticCatalogueResolver() = default;
StaticCatalogueResolver *StaticCatalogueResolver::shared(QNetworkAccessManager *nam,
                                                         const QUrl &endpoint,
                                                         const QString &directory,
                                                         QObject *offlineOwner) {
    QObject *parent = nam ? static_cast<QObject *>(nam) : offlineOwner;
    const auto name = QStringLiteral("static-catalogue:") +
                      digest((endpoint.toString(QUrl::FullyEncoded) + "\n" + directory).toUtf8());
    if (parent)
        for (auto *r :
             parent->findChildren<StaticCatalogueResolver *>(name, Qt::FindDirectChildrenOnly))
            return r;
    auto *result = new StaticCatalogueResolver(nam, endpoint, directory, parent);
    result->setObjectName(name);
    return result;
}
quint64 StaticCatalogueResolver::resolve(const QString &dataset, const QString &method,
                                         const QVariantList &args, QObject *context, Callback cb) {
    const quint64 id = ++d->nextId;
    auto j = QSharedPointer<Impl::Job>::create();
    j->id = id;
    j->dataset = dataset;
    j->method = method;
    j->nativeKey = CatalogueTrace::enabled() ? CatalogueTrace::queryKey(method, args) : QString();
    j->args = QJsonArray::fromVariantList(args);
    j->context = context;
    j->callback = std::move(cb);
    j->clock.start();
    CatalogueTrace::event("enqueue", d->traceJob(j));
    if (d->jobs.size() >= queryCap) {
        d->rejected.insert(id);
        QPointer<QObject> guard = context;
        QTimer::singleShot(0, this, [this, id, guard, callback = j->callback] {
            if (!d->rejected.remove(id))
                return;
            if (guard) {
                Outcome out;
                out.error = "STATIC_PENDING_LIMIT";
                callback(out);
            }
        });
        return id;
    }
    d->jobs.insert(id, j);
    j->deadline = new QTimer(this);
    j->deadline->setSingleShot(true);
    connect(j->deadline, &QTimer::timeout, this,
            [this, id] { d->finish(id, Status::Failure, {}, "STATIC_QUERY_TIMEOUT"); });
    j->deadline->start(45000);
    if (context)
        j->destroyed = connect(context, &QObject::destroyed, this, [this, id] { cancel(id); });
    QTimer::singleShot(0, this, [this, id] {
        const auto job = d->jobs.value(id);
        if (!job)
            return;
        CatalogueTrace::event("dispatch", d->traceJob(job));
        if (d->endpoint.isEmpty()) {
            d->finish(id, Status::Failure, {}, "STATIC_INVALID_ORIGIN");
            return;
        }
        if (job->dataset == "comics" && job->method == "search") {
            job->index = d->index;
            d->finish(id, Status::NotPrepared);
            return;
        }
        d->ensure(id);
    });
    return id;
}
void StaticCatalogueResolver::cancel(quint64 id) {
    d->rejected.remove(id);
    const auto job = d->jobs.take(id);
    if (job) {
        QObject::disconnect(job->destroyed);
        if (job->deadline) {
            job->deadline->stop();
            job->deadline->deleteLater();
        }
        d->workingBytes -= job->bytes;
    }
    d->cancelIO(id);
    d->entryWaiters.remove(id);
}
void StaticCatalogueResolver::refresh() {
    d->checked = {};
}
bool StaticCatalogueResolver::needsRefresh() const {
    const auto age = d->checked.msecsTo(QDateTime::currentDateTimeUtc());
    return !d->checked.isValid() || age < 0 || age >= 60000;
}
bool StaticCatalogueResolver::hasLastGood() const {
    return !d->index.isEmpty();
}
QString StaticCatalogueResolver::publication() const {
    return d->pub();
}
