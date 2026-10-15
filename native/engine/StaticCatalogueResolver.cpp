#include "StaticCatalogueResolver.h"
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
QByteArray json(const QJsonValue &v) {
    if (v.isObject())
        return QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    if (v.isArray())
        return QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
    const auto b = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return b.mid(1, b.size() - 2);
}
QJsonValue parse(const QByteArray &raw) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    const auto text = decoder(raw);
    Q_UNUSED(text);
    if (decoder.hasError())
        throw QStringLiteral("STATIC_INVALID_UTF8");
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(raw, &e);
    if (e.error == QJsonParseError::NoError)
        return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    const auto scalar = QJsonDocument::fromJson("[" + raw + "]", &e);
    if (e.error != QJsonParseError::NoError || !scalar.isArray() || scalar.array().size() != 1)
        throw QStringLiteral("STATIC_INVALID_JSON");
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
using Read = std::function<QJsonValue(const QString &)>;

// Pure I01 resolver. A missing local file suspends this evaluation; it is resumed
// only after asynchronous verified loading. No nested event loops or GUI blocking.
std::optional<QJsonValue> evaluate(const QJsonObject &index, const QString &ds,
                                   const QString &method, const QJsonArray &args,
                                   const Read &read) {
    const auto at = [&](int n) {
        return n < args.size() ? args.at(n) : QJsonValue(QJsonValue::Undefined);
    };
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
    };
    using AssetCallback = std::function<void(Loaded, QString)>;
    struct Job {
        quint64 id = 0;
        QString dataset, method;
        QJsonArray args;
        QPointer<QObject> context;
        Callback callback;
        QJsonObject index;
        QHash<QString, Loaded> files;
        QVariantList events;
        QElapsedTimer clock;
        qint64 bytes = 0;
        int restarts = 0;
        QPointer<QTimer> deadline;
        QMetaObject::Connection destroyed;
    };
    struct Transfer {
        QString path, etag;
        int cap = 0;
        std::function<void(int, QByteArray, QString, QString, double)> callback;
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
    QSet<quint64> rejected;
    QList<Loaded> bootstrap;
    QQueue<Transfer> transfers;
    QList<QPointer<QNetworkReply>> replies;
    bool checking = false, unavailable = false, hardFailure = false;
    QString entryError;
    quint64 nextId = 0;
    qint64 workingBytes = 0;
    int active = 0;
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
                  const QString &where, double ms) {
        if (raw.size() > fileCap || (size >= 0 && raw.size() != size) || digest(raw) != expected)
            throw QStringLiteral("STATIC_HASH_MISMATCH");
        return {path, where, parse(raw), raw.size(), ms};
    }
    void loadLastGood() {
        if (directory.isEmpty())
            return;
        QElapsedTimer clock;
        clock.start();
        QFile file(QDir(directory).filePath("last-good.json"));
        if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16384)
            return;
        try {
            const auto wrapper = object(parse(file.readAll()));
            if (wrapper.value("endpoint") != endpoint.toString(QUrl::FullyEncoded))
                return;
            const auto candidate = wrapper.value("entry").toObject();
            if (!validEntry(candidate))
                return;
            const auto ref = candidate.value("index").toObject();
            QFile data(pathFor(ref.value("sha256").toString()));
            if (QFileInfo(data).isSymLink() || !data.open(QIODevice::ReadOnly) ||
                data.size() > fileCap)
                return;
            auto loaded =
                decode(ref.value("path").toString(), data.readAll(), ref.value("sha256").toString(),
                       qint64(ref.value("bytes").toDouble()), "disk", 0);
            if (!loaded.value.isObject() || !validIndex(loaded.value.toObject(), candidate))
                return;
            loaded.ms = clock.nsecsElapsed() / 1e6;
            bootstrap.append(Loaded{"/catalogue/current.json", "disk", {}, file.size(), 0});
            bootstrap.append(loaded);
            entry = candidate;
            index = loaded.value.toObject();
            currentEtag = wrapper.value("etag").toString();
            if (currentEtag.size() > 1024 || currentEtag.contains('\r') ||
                currentEtag.contains('\n'))
                currentEtag.clear();
        } catch (const QString &) {
        }
    }
    void atomic(const QString &path, const QByteArray &raw) {
        QSaveFile out(path);
        out.setDirectWriteFallback(false);
        if (out.open(QIODevice::WriteOnly) && out.write(raw) == raw.size())
            out.commit();
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
                                {"etag", currentEtag}}));
        prune();
    }
    void pump() {
        while (active < 4 && !transfers.isEmpty()) {
            auto item = transfers.dequeue();
            if (item.path != "/catalogue/current.json" && !loading.contains(item.path))
                continue;
            if (!nam) {
                item.callback(0, {}, QStringLiteral("STATIC_OFFLINE"), {}, 0);
                continue;
            }
            ++active;
            QNetworkRequest request(endpoint.resolved(QUrl(item.path)));
            request.setRawHeader("Accept-Encoding", "identity");
            request.setRawHeader("User-Agent", "Colosseum/1.1.9 StaticCatalogue/1");
            if (!item.etag.isEmpty())
                request.setRawHeader("If-None-Match", item.etag.toLatin1());
            request.setTransferTimeout(15000);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            auto *reply = nam->get(request);
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
        transfers.enqueue(std::move(t));
        pump();
    }
    void asset(const QString &path, const QString &expected, qint64 size, AssetCallback callback,
               quint64 owner = 0) {
        // Each reference must validate its own length, including memory hits and
        // shared in-flight loads whose first waiter may declare another length.
        auto checked = [size, callback = std::move(callback)](Loaded value, QString error) {
            if (error.isEmpty() && size >= 0 && value.bytes != size)
                error = QStringLiteral("STATIC_HASH_MISMATCH");
            callback(std::move(value), std::move(error));
        };
        callback = std::move(checked);
        if (auto *cached = memory.object(path)) {
            auto value = *cached;
            value.where = "memory";
            value.ms = 0;
            QTimer::singleShot(0, q, [callback, value] { callback(value, {}); });
            return;
        }
        if (!directory.isEmpty()) {
            QElapsedTimer t;
            t.start();
            QFile file(pathFor(expected));
            if (!QFileInfo(file).isSymLink() && file.open(QIODevice::ReadOnly) &&
                file.size() <= fileCap) {
                try {
                    auto value = decode(path, file.readAll(), expected, size, "disk", 0);
                    value.ms = t.nsecsElapsed() / 1e6;
                    file.setFileTime(QDateTime::currentDateTimeUtc(),
                                     QFileDevice::FileModificationTime);
                    memory.insert(path, new Loaded(value), int(value.bytes));
                    QTimer::singleShot(0, q, [callback, value] { callback(value, {}); });
                    return;
                } catch (const QString &) {
                    file.close();
                    QFile::remove(pathFor(expected));
                }
            }
        }
        assetOwners[path].insert(owner);
        if (loading.contains(path)) {
            loading[path].append(std::move(callback));
            return;
        }
        loading[path].append(std::move(callback));
        network({path,
                 {},
                 int(fileCap),
                 [this, path, expected, size](int status, QByteArray raw, QString error, QString,
                                              double ms) {
                     Loaded value{path, nam ? "network" : "offline", {}, raw.size(), ms};
                     if (error.isEmpty() && status != 200)
                         error = "STATIC_HTTP_" + QString::number(status);
                     if (error.isEmpty())
                         try {
                             QElapsedTimer clock;
                             clock.start();
                             value = decode(path, raw, expected, size, "network", ms);
                             value.ms += clock.nsecsElapsed() / 1e6;
                             memory.insert(path, new Loaded(value), int(value.bytes));
                             if (!directory.isEmpty() && QDir().mkpath(directory)) {
                                 atomic(pathFor(expected), raw);
                                 prune();
                             }
                         } catch (const QString &why) {
                             error = why;
                         }
                     const auto callbacks = loading.take(path);
                     assetOwners.remove(path);
                     bool first = true;
                     for (const auto &cb : callbacks) {
                         auto row = value;
                         if (!first)
                             row.where = "shared";
                         first = false;
                         cb(row, error);
                     }
                 }});
    }
    void record(const QSharedPointer<Job> &j, const Loaded &f) {
        j->events.append(QVariantMap{
            {"path", f.path}, {"source", f.where}, {"bytes", f.bytes}, {"readMs", f.ms}});
    }
    void cancelIO(quint64 id) {
        const auto paths = assetOwners.keys();
        for (const auto &path : paths) {
            assetOwners[path].remove(id);
            if (assetOwners[path].isEmpty()) {
                assetOwners.remove(path);
                loading.remove(path);
                const auto activeReplies = replies;
                for (const auto &r : activeReplies)
                    if (r && r->property("staticPath") == path)
                        r->abort();
            }
        }
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
                       {"elapsedMs", j->clock.nsecsElapsed() / 1e6},
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
            if (j->context)
                j->callback(std::move(out));
        }
    }
    void run(quint64 id) {
        const auto j = jobs.value(id);
        if (!j)
            return;
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
            j->index = index;
        }
        try {
            const auto read = [&](const QString &path) -> QJsonValue {
                const auto prefix =
                    "/catalogue/" + j->index.value("revision").toString() + "/shared/";
                static const QRegularExpression leaf(QStringLiteral("^[a-f0-9]{64}\\.json$"));
                if (!path.startsWith(prefix) || !leaf.match(path.mid(prefix.size())).hasMatch())
                    throw QStringLiteral("STATIC_INVALID_PATH");
                if (j->files.contains(path))
                    return j->files.value(path).value;
                throw Need{path};
            };
            const auto value = evaluate(j->index, j->dataset, j->method, j->args, read);
            if (!value) {
                finish(id, Status::NotPrepared);
                return;
            }
            if (json(*value).size() > 4 * 1024 * 1024)
                throw QStringLiteral("STATIC_RESULT_TOO_LARGE");
            finish(id, Status::Prepared, *value);
        } catch (const Need &need) {
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
            j->index = index;
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
        if (checked.isValid() && checked.secsTo(QDateTime::currentDateTimeUtc()) < 60) {
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
                         releaseWaiters({current});
                         return;
                     }
                     if (!error.isEmpty() || status != 200) {
                         checking = false;
                         entryError =
                             error.isEmpty() ? "STATIC_HTTP_" + QString::number(status) : error;
                         hardFailure =
                             error == "STATIC_FILE_TOO_LARGE" || (status >= 300 && status < 400);
                         unavailable = index.isEmpty() && (status == 401 || status == 404 ||
                                                           status == 410 || status == 501);
                         releaseWaiters({current});
                         return;
                     }
                     QJsonObject candidate;
                     try {
                         candidate = object(parse(raw));
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
    j->args = QJsonArray::fromVariantList(args);
    j->context = context;
    j->callback = std::move(cb);
    j->clock.start();
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
    return !d->checked.isValid() || d->checked.secsTo(QDateTime::currentDateTimeUtc()) >= 60;
}
bool StaticCatalogueResolver::hasLastGood() const {
    return !d->index.isEmpty();
}
QString StaticCatalogueResolver::publication() const {
    return d->pub();
}
