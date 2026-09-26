#include "ColosseumTitleIdentityRegistry.h"
#include <QCryptographicHash>
#include <QResource>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace {
bool exactKeys(const QJsonObject &object, const QSet<QString> &wanted) {
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());
    return actual == wanted;
}
}

ColosseumTitleIdentityRegistry::ColosseumTitleIdentityRegistry(
    const QString &path, QObject *parent) : QObject(parent) {
    setObjectName(QStringLiteral("colosseumTitleIdentityRegistry"));
    load(path.isEmpty() ? defaultResourcePath() : path);
}
// Slice 4 identity registry.
bool ColosseumTitleIdentityRegistry::ready() const { return m_ready; }
QString ColosseumTitleIdentityRegistry::errorCode() const { return m_errorCode; }
QString ColosseumTitleIdentityRegistry::sourcePath() const { return m_sourcePath; }
QString ColosseumTitleIdentityRegistry::defaultResourcePath() {
    return QStringLiteral(":/ratings-reviews/title-identities-v1.json");
}
bool ColosseumTitleIdentityRegistry::isCanonicalMediaId(const QString &mediaId) {
    if (!mediaId.startsWith(QStringLiteral("ct1:")) || mediaId.size() != 40)
        return false;
    for (int i = 4; i < mediaId.size(); ++i) {
        const bool hyphen = i == 12 || i == 17 || i == 22 || i == 27;
        if (hyphen) { if (mediaId.at(i) != QLatin1Char('-')) return false; continue; }
        const QChar ch = mediaId.at(i);
        if (!ch.isDigit() && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))) return false;
    }
    return true;
}

bool ColosseumTitleIdentityRegistry::isJoinedFixtureIdentity(
    const QString &world, const QString &kind, const QString &mediaId) {
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    return qEnvironmentVariable("COLOSSEUM_APPDATA_TAG")
               == QLatin1String("ratings-reviews-delivery-fixture")
        && world == QLatin1String("theatre")
        && kind == QLatin1String("series")
        && mediaId == QLatin1String("fixture-provider-read-series");
#else
    Q_UNUSED(world)
    Q_UNUSED(kind)
    Q_UNUSED(mediaId)
    return false;
#endif
}

bool ColosseumTitleIdentityRegistry::admittedPair(const QString &world, const QString &kind) {
    return (world == QLatin1String("theatre") && (kind == QLatin1String("movie") || kind == QLatin1String("series")))
        || (world == QLatin1String("biblio") && kind == QLatin1String("book"))
        || (world == QLatin1String("tankoban") && kind == QLatin1String("manga"))
        || (world == QLatin1String("vault") && kind == QLatin1String("film"));
}
// Pair tags occupy the first byte of a derived id. 0x49 is theatre/series so the
// frozen Frieren seed id (49f1…) self-identifies as the pair it belongs to.
// theatre/movie and vault/film share 0x4a: one identified IMDb film is one
// title in both worlds, so its derived id must be identical from either door.
int ColosseumTitleIdentityRegistry::pairTag(const QString &world, const QString &kind) {
    if (world == QLatin1String("theatre") && kind == QLatin1String("series"))
        return 0x49;
    if ((world == QLatin1String("theatre") && kind == QLatin1String("movie"))
        || (world == QLatin1String("vault") && kind == QLatin1String("film")))
        return 0x4a;
    if (world == QLatin1String("biblio") && kind == QLatin1String("book"))
        return 0x4b;
    if (world == QLatin1String("tankoban") && kind == QLatin1String("manga"))
        return 0x4c;
    return -1;
}
// Derived ids are RFC 4122 version 5 (name-based); hand-seeded ids are version 4.
bool ColosseumTitleIdentityRegistry::isDerivedMediaId(const QString &mediaId) {
    return isCanonicalMediaId(mediaId)
        && mediaId.size() == 40
        && mediaId.at(18) == QLatin1Char('5');
}
int ColosseumTitleIdentityRegistry::derivedMediaIdTag(const QString &mediaId) {
    if (!isDerivedMediaId(mediaId))
        return -1;
    bool ok = false;
    const int tag = mediaId.mid(4, 2).toInt(&ok, 16);
    return ok ? tag : -1;
}
QString ColosseumTitleIdentityRegistry::deriveMediaId(
    const QString &world, const QString &kind,
    const QString &nameSpace, const QString &value) const {
    return deriveNamedMediaId(world, kind, nameSpace, value, pairTag(world, kind));
}

// Shared derivation core: the name parts and the pair tag are the identity
// space. Film identities (theatre/movie + vault/film) deliberately share one
// space so the same identified IMDb film is one title everywhere.
QString ColosseumTitleIdentityRegistry::deriveNamedMediaId(
    const QString &world, const QString &kind,
    const QString &nameSpace, const QString &value, int tag) const {
    if (tag < 0 || nameSpace.isEmpty() || value.isEmpty())
        return QString();
    const QString name = QStringLiteral("ct1v5|%1|%2|%3|%4")
                             .arg(world, kind, nameSpace, value);
    const QByteArray digest =
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256)
            .left(16);
    uchar bytes[16];
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<uchar>(digest.at(i));
    bytes[0] = static_cast<uchar>(tag);
    bytes[6] = static_cast<uchar>((bytes[6] & 0x0f) | 0x50);
    bytes[8] = static_cast<uchar>((bytes[8] & 0x3f) | 0x80);
    static const char kHex[] = "0123456789abcdef";
    QString hex;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            hex.append(QLatin1Char('-'));
        hex.append(QLatin1Char(kHex[(bytes[i] >> 4) & 0xf]));
        hex.append(QLatin1Char(kHex[bytes[i] & 0xf]));
    }
    return QStringLiteral("ct1:") + hex;
}
// Identity-bearing alias namespaces per admitted pair, in derivation priority
// order. Edition-level biblio anchors (isbn) outrank work-level ones so two
// editions of the same work never collapse. Theatre prefers an IMDb (tt…) value
// when the caller offers several source ids; a persisted alias union makes the
// kitsu→imdb pivot converge even when only the provider id is offered again.
// Film identities (theatre/movie + vault/film) normalize to one shared imdb
// space so the same identified film derives one id from either world.
QString ColosseumTitleIdentityRegistry::deriveIdentity(
    const QString &world, const QString &kind, const QVariantList &aliases) const {
    QStringList namespaces;
    bool preferImdb = false;
    if (world == QLatin1String("theatre")) {
        namespaces = {QStringLiteral("theatre-source-id")};
        preferImdb = true;
    } else if (world == QLatin1String("biblio")) {
        namespaces = {QStringLiteral("isbn"), QStringLiteral("biblio-source-id"),
                      QStringLiteral("openlibrary-work")};
    } else if (world == QLatin1String("tankoban")) {
        namespaces = {QStringLiteral("tankoban-source-id")};
    } else if (world == QLatin1String("vault")) {
        namespaces = {QStringLiteral("vault-source-id")};
    } else {
        return QString();
    }

    const bool filmPair = (world == QLatin1String("theatre") && kind == QLatin1String("movie"))
                       || (world == QLatin1String("vault") && kind == QLatin1String("film"));
    if (filmPair) {
        // Shared film identity: theatre tt… and vault imdb:tt… normalize to the
        // same derivation name, so both doors produce the exact same id.
        for (const QVariant &candidateValue : aliases) {
            const QVariantMap candidate = candidateValue.toMap();
            const QString candidateNamespace =
                candidate.value(QStringLiteral("namespace")).toString();
            const QString value =
                candidate.value(QStringLiteral("value")).toString();
            QString imdb;
            if (candidateNamespace == QLatin1String("theatre-source-id")
                    && value.startsWith(QLatin1String("tt")))
                imdb = value;
            else if (candidateNamespace == QLatin1String("vault-source-id")
                     && value.startsWith(QLatin1String("imdb:tt")))
                imdb = value.mid(5);
            if (!imdb.isEmpty())
                return deriveNamedMediaId(QStringLiteral("film"),
                                          QStringLiteral("imdb"),
                                          QStringLiteral("imdb"), imdb, 0x4a);
        }
        // Vault admits only identified IMDb films; theatre movies without an
        // IMDb id fall through to the regular theatre derivation below.
        if (world == QLatin1String("vault"))
            return QString();
    }

    for (const QString &candidateNamespace : std::as_const(namespaces)) {
        QString fallback;
        QString imdbAnchor;
        for (const QVariant &candidateValue : aliases) {
            const QVariantMap candidate = candidateValue.toMap();
            if (candidate.value(QStringLiteral("namespace")).toString() != candidateNamespace)
                continue;
            const QString value =
                candidate.value(QStringLiteral("value")).toString();
            if (value.isEmpty())
                continue;
            if (preferImdb && value.startsWith(QLatin1String("tt"))) {
                imdbAnchor = value;
                break;
            }
            if (fallback.isEmpty())
                fallback = value;
        }
        if (!imdbAnchor.isEmpty()) {
            // Learn the pivot: provider ids seen beside an IMDb anchor union to
            // it so later provider-only lookups resolve to the same identity.
            if (m_aliasUnionsEnabled && preferImdb) {
                for (const QVariant &candidateValue : aliases) {
                    const QVariantMap candidate = candidateValue.toMap();
                    if (candidate.value(QStringLiteral("namespace")).toString() != candidateNamespace)
                        continue;
                    const QString other =
                        candidate.value(QStringLiteral("value")).toString();
                    if (other.isEmpty() || other == imdbAnchor
                            || other.startsWith(QLatin1String("tt")))
                        continue;
                    recordAliasUnion(candidateNamespace, other, imdbAnchor);
                }
            }
            return deriveMediaId(world, kind, candidateNamespace, imdbAnchor);
        }
        if (!fallback.isEmpty())
            return deriveMediaId(world, kind, candidateNamespace, fallback);
    }
    return QString();
}
QString ColosseumTitleIdentityRegistry::aliasKey(const QString &nameSpace, const QString &value) {
    return nameSpace + QChar(0x1f) + value;
}

// ---- persisted alias unions (anime provider-id pivots) ----
// A union remembers that a provider id (kitsu:…, mal:…) was seen alongside an
// IMDb anchor for the same title, so a later provider-only lookup converges on
// the IMDb-anchored identity instead of splitting the private record. Unions
// are an accelerator, never authority: a missing or corrupt file silently
// degrades to stateless derivation.
void ColosseumTitleIdentityRegistry::setAliasUnionsPath(const QString &path) {
    m_aliasUnionsPath = path;
    m_aliasUnionsEnabled = !path.isEmpty();
    m_aliasUnions.clear();
    if (!m_aliasUnionsEnabled)
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonArray unions = document.object().value(QStringLiteral("unions")).toArray();
    for (const QJsonValue &value : unions) {
        const QJsonObject entry = value.toObject();
        const QString from = entry.value(QStringLiteral("from")).toString();
        const QString to = entry.value(QStringLiteral("to")).toString();
        if (!from.isEmpty() && !to.isEmpty() && from != to)
            m_aliasUnions.insert(from, to);
    }
}

void ColosseumTitleIdentityRegistry::recordAliasUnion(
    const QString &nameSpace, const QString &from, const QString &to) const {
    const QString fromKey = aliasKey(nameSpace, from);
    const QString toKey = aliasKey(nameSpace, to);
    if (!m_aliasUnionsEnabled || fromKey == toKey || m_aliasUnions.value(fromKey) == toKey)
        return;
    m_aliasUnions.insert(fromKey, toKey);
    QFile file(m_aliasUnionsPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray unions;
    for (auto it = m_aliasUnions.constBegin(); it != m_aliasUnions.constEnd(); ++it)
        unions.append(QJsonObject{{QStringLiteral("from"), it.key()},
                                  {QStringLiteral("to"), it.value()}});
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("unions"), unions}}).toJson(QJsonDocument::Compact));
}

QVariantList ColosseumTitleIdentityRegistry::expandAliasUnions(
    const QVariantList &aliases) const {
    if (!m_aliasUnionsEnabled || m_aliasUnions.isEmpty())
        return aliases;
    QVariantList expanded = aliases;
    for (const QVariant &candidateValue : aliases) {
        const QVariantMap candidate = candidateValue.toMap();
        const QString candidateNamespace =
            candidate.value(QStringLiteral("namespace")).toString();
        const QString value =
            candidate.value(QStringLiteral("value")).toString();
        if (candidateNamespace.isEmpty() || value.isEmpty())
            continue;
        const QString target = m_aliasUnions.value(aliasKey(candidateNamespace, value));
        if (target.isEmpty())
            continue;
        const int split = target.indexOf(QChar(0x1f));
        if (split <= 0 || split + 1 >= target.size())
            continue;
        const QVariantMap extra{
            {QStringLiteral("namespace"), target.left(split)},
            {QStringLiteral("value"), target.mid(split + 1)}};
        if (!expanded.contains(extra))
            expanded.append(extra);
    }
    return expanded;
}
bool ColosseumTitleIdentityRegistry::load(const QString &path) {
    m_sourcePath = path;
    m_rows.clear();
    m_mediaRows.clear();
    m_aliasRows.clear();
    m_ready = false;
    m_errorCode.clear();

    QByteArray bytes;
    if (path.startsWith(QLatin1String(":/"))) {
        QResource resource(path);
        if (!resource.isValid()) {
            m_errorCode = QStringLiteral("identity_registry_unavailable");
            return false;
        }
        bytes = resource.uncompressedData();
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_errorCode = QStringLiteral("identity_registry_unavailable");
            return false;
        }
        bytes = file.readAll();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_errorCode = QStringLiteral("identity_registry_malformed");
        return false;
    }
    const QJsonObject root = document.object();
    if (!exactKeys(root, {QStringLiteral("version"), QStringLiteral("titles")})
        || root.value(QStringLiteral("version")).toInt(-1) != 1
        || !root.value(QStringLiteral("titles")).isArray()) {
        m_errorCode = QStringLiteral("identity_registry_schema_invalid");
        return false;
    }

    const QJsonArray titles = root.value(QStringLiteral("titles")).toArray();
    for (const QJsonValue &titleValue : titles) {
        if (!titleValue.isObject()) {
            m_errorCode = QStringLiteral("identity_registry_row_invalid");
            return false;
        }
        const QJsonObject object = titleValue.toObject();
        if (!exactKeys(object, {QStringLiteral("world"), QStringLiteral("kind"),
                                QStringLiteral("media_id"), QStringLiteral("aliases")})
            || !object.value(QStringLiteral("world")).isString()
            || !object.value(QStringLiteral("kind")).isString()
            || !object.value(QStringLiteral("media_id")).isString()
            || !object.value(QStringLiteral("aliases")).isArray()) {
            m_errorCode = QStringLiteral("identity_registry_row_invalid");
            return false;
        }
        Row row;
        row.world = object.value(QStringLiteral("world")).toString();
        row.kind = object.value(QStringLiteral("kind")).toString();
        row.mediaId = object.value(QStringLiteral("media_id")).toString();
        if (!admittedPair(row.world, row.kind)
            || !isCanonicalMediaId(row.mediaId)
            || m_mediaRows.contains(row.mediaId)) {
            m_errorCode = QStringLiteral("identity_registry_identity_invalid");
            return false;
        }

        const QJsonArray aliases = object.value(QStringLiteral("aliases")).toArray();
        QSet<QString> rowAliases;
        for (const QJsonValue &aliasValue : aliases) {
            if (!aliasValue.isObject()) {
                m_errorCode = QStringLiteral("identity_registry_alias_invalid");
                return false;
            }
            const QJsonObject aliasObject = aliasValue.toObject();
            const QString aliasNamespace = aliasObject.value(QStringLiteral("namespace")).toString();
            const QString aliasText = aliasObject.value(QStringLiteral("value")).toString();
            const QString pairId = aliasKey(aliasNamespace, aliasText);
            if (aliasNamespace.isEmpty() || aliasText.isEmpty() || rowAliases.contains(pairId)) {
                m_errorCode = QStringLiteral("identity_registry_alias_invalid");
                return false;
            }
            rowAliases.insert(pairId);
            row.aliases.append(qMakePair(aliasNamespace, aliasText));
        }
        const int index = m_rows.size();
        m_rows.append(row);
        m_mediaRows.insert(row.mediaId, index);
        for (const auto &alias : row.aliases)
            m_aliasRows.insert(aliasKey(alias.first, alias.second), index);
    }
    m_ready = true;
    return true;
}

QVariantMap ColosseumTitleIdentityRegistry::unavailable(
    const QString &world, const QString &kind, const QString &code) const {
    return {{QStringLiteral("available"), false},
            {QStringLiteral("world"), world},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("mediaId"), QString()},
            {QStringLiteral("errorCode"), code}};
}

QVariantMap ColosseumTitleIdentityRegistry::resolved(const Row &row) const {
    return {{QStringLiteral("available"), true},
            {QStringLiteral("world"), row.world},
            {QStringLiteral("kind"), row.kind},
            {QStringLiteral("mediaId"), row.mediaId},
            {QStringLiteral("errorCode"), QString()}};
}

QVariantMap ColosseumTitleIdentityRegistry::resolvedMediaId(
    const QString &world, const QString &kind, const QString &mediaId) const {
    return {{QStringLiteral("available"), true},
            {QStringLiteral("world"), world},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("mediaId"), mediaId},
            {QStringLiteral("errorCode"), QString()}};
}

QVariantMap ColosseumTitleIdentityRegistry::resolve(
    const QString &world, const QString &kind,
    const QString &directMediaId, QVariantList aliases) const {
    if (!m_ready)
        return unavailable(world, kind, m_errorCode);
    if (!admittedPair(world, kind))
        return unavailable(world, kind, QStringLiteral("identity_route_not_admitted"));
    // Learned provider-id pivots join the alias set before any matching, so a
    // provider-only lookup resolves to the IMDb-anchored identity.
    aliases = expandAliasUnions(aliases);
    if (isJoinedFixtureIdentity(world, kind, directMediaId)) {
        return resolvedMediaId(world, kind, directMediaId);
    }
    for (const QVariant &candidateValue : aliases) {
        const QVariantMap candidate = candidateValue.toMap();
        if (candidate.value(QStringLiteral("namespace")).toString()
                == QLatin1String("theatre-source-id")) {
            const QString value = candidate.value(QStringLiteral("value")).toString();
            if (isJoinedFixtureIdentity(world, kind, value)) {
                return resolvedMediaId(world, kind, value);
            }
        }
    }
    // Film seeds are pair-agnostic: a pinned theatre/movie row must answer a
    // vault lookup of the same IMDb film (and vice versa) with the same id.
    const bool filmPair = (world == QLatin1String("theatre") && kind == QLatin1String("movie"))
                       || (world == QLatin1String("vault") && kind == QLatin1String("film"));
    if (filmPair) {
        QString normalizedImdb;
        for (const QVariant &candidateValue : aliases) {
            const QVariantMap candidate = candidateValue.toMap();
            const QString candidateNamespace =
                candidate.value(QStringLiteral("namespace")).toString();
            const QString value =
                candidate.value(QStringLiteral("value")).toString();
            if (candidateNamespace == QLatin1String("theatre-source-id")
                    && value.startsWith(QLatin1String("tt")))
                normalizedImdb = value;
            else if (candidateNamespace == QLatin1String("vault-source-id")
                     && value.startsWith(QLatin1String("imdb:tt")))
                normalizedImdb = value.mid(5);
        }
        if (!normalizedImdb.isEmpty()) {
            QString pinned;
            for (const Row &row : std::as_const(m_rows)) {
                const bool rowFilmPair =
                    (row.world == QLatin1String("theatre") && row.kind == QLatin1String("movie"))
                    || (row.world == QLatin1String("vault") && row.kind == QLatin1String("film"));
                if (!rowFilmPair)
                    continue;
                for (const auto &rowAlias : row.aliases) {
                    QString rowImdb;
                    if (rowAlias.first == QLatin1String("theatre-source-id")
                            && rowAlias.second.startsWith(QLatin1String("tt")))
                        rowImdb = rowAlias.second;
                    else if (rowAlias.first == QLatin1String("vault-source-id")
                             && rowAlias.second.startsWith(QLatin1String("imdb:tt")))
                        rowImdb = rowAlias.second.mid(5);
                    if (rowImdb.isEmpty() || rowImdb != normalizedImdb)
                        continue;
                    if (!pinned.isEmpty() && pinned != row.mediaId)
                        return unavailable(world, kind, QStringLiteral("identity_conflict"));
                    pinned = row.mediaId;
                }
            }
            if (!pinned.isEmpty())
                return resolvedMediaId(world, kind, pinned);
        }
    }
    if (!directMediaId.isEmpty()) {
        if (m_mediaRows.contains(directMediaId)) {
            const Row &row = m_rows.at(m_mediaRows.value(directMediaId));
            if (row.world != world || row.kind != kind)
                return unavailable(world, kind, QStringLiteral("identity_conflict"));
            return resolved(row);
        }
        if (isDerivedMediaId(directMediaId)) {
            // A derived id is self-validating for its pair: the open() re-check
            // has no aliases, so the pair tag is the proof the id was minted for
            // exactly this world/kind.
            if (derivedMediaIdTag(directMediaId) == pairTag(world, kind))
                return resolvedMediaId(world, kind, directMediaId);
            return unavailable(world, kind, QStringLiteral("identity_conflict"));
        }
        return unavailable(world, kind, QStringLiteral("identity_unavailable"));
    }

    int match = -1;
    for (const QVariant &candidateValue : aliases) {
        const QVariantMap candidate = candidateValue.toMap();
        const QString candidateNamespace = candidate.value(QStringLiteral("namespace")).toString();
        const QString candidateText = candidate.value(QStringLiteral("value")).toString();
        if (candidateNamespace.isEmpty() || candidateText.isEmpty())
            continue;
        for (int i = 0; i < m_rows.size(); ++i) {
            const Row &row = m_rows.at(i);
            if (row.world != world || row.kind != kind)
                continue;
            for (const auto &rowAlias : row.aliases) {
                if (rowAlias.first != candidateNamespace || rowAlias.second != candidateText)
                    continue;
                if (match >= 0 && match != i)
                    return unavailable(world, kind, QStringLiteral("identity_conflict"));
                match = i;
            }
        }
    }
    if (match >= 0)
        return resolved(m_rows.at(match));
    const QString derived = deriveIdentity(world, kind, aliases);
    if (!derived.isEmpty())
        return resolvedMediaId(world, kind, derived);
    return unavailable(world, kind, QStringLiteral("identity_unavailable"));
}
