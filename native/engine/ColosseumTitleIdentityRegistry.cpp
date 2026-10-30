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
bool collectionTextValid(const QString &value, bool multiline = false) {
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch.isNull() || (ch.category() == QChar::Other_Control
                && !(multiline && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t')))))
            return false;
        if (ch.isHighSurrogate()) {
            if (i + 1 >= value.size() || !value.at(i + 1).isLowSurrogate()) return false;
            ++i;
        } else if (ch.isLowSurrogate()) return false;
    }
    return true;
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

QVariantMap ColosseumTitleIdentityRegistry::collectionReference(const QVariantMap &candidate) const {
    const auto fail = [](const QString &message) -> QVariantMap {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    if (!m_ready)
        return fail(QStringLiteral("Title identity is unavailable. Try again after the catalogue is ready."));
    const QMap<QString, int> scalarLimits{{"key",2048},{"mediaId",80},{"world",32},{"kind",32},
        {"source",64},{"sourceId",512},{"editionId",512},{"route",64},{"title",1024},
        {"cover",4096},{"creator",1024},{"year",32}};
    for (auto it = scalarLimits.cbegin(); it != scalarLimits.cend(); ++it) {
        const QString value = candidate.value(it.key()).toString();
        if (value.size() > it.value() || !collectionTextValid(value))
            return fail(QStringLiteral("This title contains invalid or oversized reference text."));
    }
    const auto text = [&candidate](const char *name, int limit) -> QString {
        const QString value = candidate.value(QLatin1String(name)).toString().trimmed();
        if (value.size() > limit || value.contains(QChar::Null)) return QString();
        return value;
    };
    const QString world = text("world", 32);
    const QString kind = text("kind", 32);
    const QString source = text("source", 64);
    const QString sourceId = text("sourceId", 512);
    QString editionId = text("editionId", 512);
    const QString route = text("route", 64);
    const QVariantMap supplied = candidate.value(QStringLiteral("payload")).toMap();
    QVariantMap payload;
    QVariantList aliases;
    QString mediaId;
    const auto token = [](const QString &value) {
        return !value.isEmpty() && value.size() <= 512
            && QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:@/+~-]*$")).match(value).hasMatch();
    };
    const auto localMusicToken = [](const QString &value) {
        // VinylLocalSource's manifest keys use QChar::isLetterOrNumber plus
        // '-'/'_', and are appended to its ASCII owner-generated ID prefixes.
        if (value.isEmpty() || value.size() > 512 || !value.startsWith(QLatin1String("vinyl:local-"))) return false;
        for (const QChar ch : value)
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_') && ch != QLatin1Char(':')) return false;
        return true;
    };
    const auto positive = [](const QString &value) {
        return QRegularExpression(QStringLiteral("^[1-9][0-9]{0,15}$")).match(value).hasMatch();
    };
    const auto copyText = [&supplied, &payload](const char *name, int limit = 512) {
        const QString value = supplied.value(QLatin1String(name)).toString();
        if (!value.isEmpty() && value.size() <= limit && !value.contains(QChar::Null))
            payload.insert(QLatin1String(name), value);
    };
    const auto addAlias = [&aliases](const QString &nameSpace, const QString &value) {
        aliases.append(QVariantMap{{QStringLiteral("namespace"), nameSpace}, {QStringLiteral("value"), value}});
    };
    const auto copyPosts = [&supplied, &payload, &positive]() -> bool {
        const QVariantList posts = supplied.value(QStringLiteral("posts")).toList();
        if (posts.isEmpty() || posts.size() > 500) return false;
        QVariantList accepted;
        QSet<QString> seen;
        for (const QVariant &post : posts) {
            const QString id = post.toString();
            if (!positive(id) || seen.contains(id)) return false;
            seen.insert(id);
            accepted.append(id);
        }
        payload.insert(QStringLiteral("posts"), accepted);
        return true;
    };

    if (world == QLatin1String("theatre")
            && (kind == QLatin1String("movie") || kind == QLatin1String("series"))) {
        if (source != QLatin1String("theatre") || route != QLatin1String("theatre-detail")
                || !token(sourceId) || !editionId.isEmpty())
            return fail(QStringLiteral("This Theatre title is waiting for an exact, resolved catalogue identity."));
        addAlias(QStringLiteral("theatre-source-id"), sourceId);
        const QString resolvedId = supplied.value(QStringLiteral("canonicalSourceId")).toString();
        // A provider door can pivot to IMDb during loadMeta. Do not create a
        // durable provider-only key during that intermediate/failed load state.
        // Direct IMDb doors already carry an authoritative stable anchor.
        if (!QRegularExpression(QStringLiteral("^tt[0-9]+$")).match(sourceId).hasMatch() && resolvedId.isEmpty())
            return fail(QStringLiteral("This Theatre title is waiting for its resolved catalogue identity. Try again when details are available."));
        if (!resolvedId.isEmpty()) {
            if (!token(resolvedId)) return fail(QStringLiteral("The resolved Theatre identity is invalid."));
            addAlias(QStringLiteral("theatre-source-id"), resolvedId);
            payload.insert(QStringLiteral("canonicalSourceId"), resolvedId);
        }
        copyText("art", 4096);
    } else if (world == QLatin1String("tankoban") && kind == QLatin1String("manga")) {
        if (source == QLatin1String("mal") && route == QLatin1String("manga-detail")) {
            const auto match = QRegularExpression(QStringLiteral("^mal:([1-9][0-9]*)(:color)?$")).match(sourceId);
            if (!match.hasMatch() || match.captured(1).toULongLong() > 2147483647ULL)
                return fail(QStringLiteral("This manga is waiting for its exact catalogue identity."));
            const QString expectedEdition = match.captured(2).isEmpty() ? QString() : QStringLiteral("color");
            if (editionId != expectedEdition)
                return fail(QStringLiteral("The manga edition does not match its source identity."));
            payload.insert(QStringLiteral("malId"), match.captured(1));
            copyText("sourceSearchTitle", 1024);
            for (const QString &name : {QStringLiteral("sourceSearchAliases"), QStringLiteral("sourceRequiredMarkers")}) {
                QStringList values;
                for (const QVariant &value : supplied.value(name).toList()) {
                    const QString item = value.toString();
                    if (!item.isEmpty() && item.size() <= 256 && values.size() < 20) values.append(item);
                }
                if (!values.isEmpty()) payload.insert(name, values);
            }
        } else if (source == QLatin1String("weebcentral") && route == QLatin1String("manga-weebcentral-detail")) {
            if (!token(sourceId) || !editionId.isEmpty())
                return fail(QStringLiteral("This manga is waiting for its exact source identity."));
            copyText("seriesUrl", 2048);
        } else {
            return fail(QStringLiteral("This manga source does not expose an exact reopen route."));
        }
        addAlias(QStringLiteral("tankoban-source-id"), sourceId);
    } else if (world == QLatin1String("biblio") && kind == QLatin1String("book")) {
        if (source != QLatin1String("apple-books") || route != QLatin1String("book-detail") || !positive(sourceId))
            return fail(QStringLiteral("This book is waiting for its Apple catalogue identity."));
        const QVariantMap book = supplied.value(QStringLiteral("book")).toMap();
        if (book.value(QStringLiteral("id")).toString() != sourceId)
            return fail(QStringLiteral("The saved book snapshot does not match its catalogue identity."));
        const bool audio = book.value(QStringLiteral("isAudiobook")).toBool();
        const QString expectedEdition = audio ? QStringLiteral("audiobook") : QStringLiteral("ebook");
        if (editionId != expectedEdition)
            return fail(QStringLiteral("The book edition does not match its catalogue reference."));
        QVariantMap cleanBook{{QStringLiteral("id"), sourceId}, {QStringLiteral("isAudiobook"), audio}};
        for (const QString &name : {QStringLiteral("title"), QStringLiteral("author"), QStringLiteral("year"),
                QStringLiteral("cover"), QStringLiteral("genreLine"), QStringLiteral("tagline"), QStringLiteral("synopsis")}) {
            const QString value = book.value(name).toString();
            if (value.size() <= 4096 && !value.contains(QChar::Null)) cleanBook.insert(name, value);
        }
        QStringList genres;
        for (const QVariant &genre : book.value(QStringLiteral("genres")).toList()) {
            const QString value = genre.toString();
            if (value.size() <= 128 && genres.size() < 20) genres.append(value);
        }
        cleanBook.insert(QStringLiteral("genres"), genres);
        payload.insert(QStringLiteral("book"), cleanBook);
        // Apple track/collection ID, not the display title/author pairing key.
        addAlias(QStringLiteral("biblio-source-id"), sourceId);
    } else if (world == QLatin1String("vault") && kind == QLatin1String("film")) {
        if (source != QLatin1String("imdb") || route != QLatin1String("vault-film-detail")
                || !QRegularExpression(QStringLiteral("^imdb:tt[0-9]+$")).match(sourceId).hasMatch()
                || !editionId.isEmpty())
            return fail(QStringLiteral("Identify this film before adding it to a collection."));
        copyText("vaultKey", 4096);
        if (!payload.contains(QStringLiteral("vaultKey")))
            return fail(QStringLiteral("This Vault film does not have a saved detail route."));
        addAlias(QStringLiteral("vault-source-id"), sourceId);
    } else if (world == QLatin1String("tankoban") && kind == QLatin1String("comic")) {
        if (!editionId.isEmpty()) return fail(QStringLiteral("This comic edition is not supported."));
        bool valid = false;
        if (source == QLatin1String("locg") && route == QLatin1String("comic-locg-detail")) {
            valid = QRegularExpression(QStringLiteral("^locg:[1-9][0-9]*$")).match(sourceId).hasMatch();
            const QVariantMap meta = supplied.value(QStringLiteral("locgMeta")).toMap();
            QVariantMap clean;
            for (const QString &name : {QStringLiteral("name"), QStringLiteral("publisher"), QStringLiteral("status"), QStringLiteral("released")}) {
                const QString value = meta.value(name).toString();
                if (value.size() <= 512) clean.insert(name, value);
            }
            clean.insert(QStringLiteral("startYear"), meta.value(QStringLiteral("startYear")).toInt());
            payload.insert(QStringLiteral("locgMeta"), clean);
        } else if (source == QLatin1String("gcd") && route == QLatin1String("comic-gcd-detail")) {
            valid = QRegularExpression(QStringLiteral("^gcd:[1-9][0-9]*$")).match(sourceId).hasMatch();
            if (supplied.contains(QStringLiteral("posts")) && !copyPosts()) valid = false;
        } else if (source == QLatin1String("getcomics") && route == QLatin1String("comic-posts-detail")) {
            valid = QRegularExpression(QStringLiteral("^gc:posts:[1-9][0-9]*$")).match(sourceId).hasMatch() && copyPosts();
            if (valid) valid = payload.value(QStringLiteral("posts")).toList().first().toString() == sourceId.mid(9);
        } else if (source == QLatin1String("getcomics") && route == QLatin1String("comic-tag-detail")) {
            const QString tag = supplied.value(QStringLiteral("tag")).toString();
            valid = sourceId == QStringLiteral("gc:") + tag && !tag.startsWith(QLatin1String("posts:"))
                && QRegularExpression(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_-]*$")).match(tag).hasMatch();
            if (valid) payload.insert(QStringLiteral("tag"), tag);
            const QString tagId = supplied.value(QStringLiteral("tagId")).toString();
            if (positive(tagId)) payload.insert(QStringLiteral("tagId"), tagId);
        } else if (source == QLatin1String("comic-packs") && route == QLatin1String("comic-pack-detail")) {
            valid = token(sourceId) && !sourceId.startsWith(QLatin1String("gc:posts:"));
        }
        if (!valid) return fail(QStringLiteral("This comic is waiting for an exact series or release reference."));
        // Collection-only tags deliberately do not widen ratings/reviews admission.
        mediaId = deriveNamedMediaId(world, kind, source, sourceId, 0x4d);
    } else if (world == QLatin1String("vinyl")
            && (kind == QLatin1String("album") || kind == QLatin1String("song") || kind == QLatin1String("artist"))) {
        const bool localSource = source == QLatin1String("vinyl-local");
        const bool sourceTokenValid = localSource && kind != QLatin1String("song") ? localMusicToken(sourceId) : token(sourceId);
        if ((source != QLatin1String("vinyl-local") && source != QLatin1String("youtube-music")) || !sourceTokenValid)
            return fail(QStringLiteral("This music item is waiting for its exact source identity."));
        if (kind == QLatin1String("artist")) {
            if (route != QLatin1String("vinyl-artist-detail") || !editionId.isEmpty())
                return fail(QStringLiteral("This artist does not have an exact reopen route."));
            if (source == QLatin1String("youtube-music")
                    && !QRegularExpression(QStringLiteral("^ytm:artist:UC[A-Za-z0-9_-]{22}$")).match(sourceId).hasMatch())
                return fail(QStringLiteral("This artist is waiting for its exact channel identity."));
            if (source == QLatin1String("vinyl-local")
                    && (!sourceId.startsWith(QLatin1String("vinyl:local-artist:"))
                        || sourceId == QLatin1String("vinyl:local-artist:unknown")))
                return fail(QStringLiteral("This local artist does not have an exact source identity."));
        } else {
            if (route != (kind == QLatin1String("album") ? QLatin1String("vinyl-album-detail") : QLatin1String("vinyl-song-detail"))
                    || !(localSource ? localMusicToken(editionId) : token(editionId))) return fail(QStringLiteral("This music edition is not ready to save."));
            const QString albumId = supplied.value(QStringLiteral("albumId")).toString();
            const QString browseId = supplied.value(QStringLiteral("browseId")).toString();
            if (!(localSource ? localMusicToken(albumId) : token(albumId))
                    || !(localSource ? localMusicToken(browseId) : token(browseId))
                    || (kind == QLatin1String("album") && albumId != sourceId))
                return fail(QStringLiteral("This music item does not have an exact album route."));
            const bool local = source == QLatin1String("vinyl-local");
            const bool localAlbum = albumId.startsWith(QLatin1String("vinyl:local-album:")) || albumId.startsWith(QLatin1String("vinyl:local-release:"));
            const bool localBrowse = browseId.startsWith(QLatin1String("vinyl:local-album:")) || browseId.startsWith(QLatin1String("vinyl:local-release:"));
            if (local != albumId.startsWith(QLatin1String("vinyl:local-"))
                    || local != browseId.startsWith(QLatin1String("vinyl:local-"))
                    || (local && (!localAlbum || !localBrowse))
                    || (local && !editionId.startsWith(albumId + QLatin1Char(':')) && editionId != albumId)
                    || (kind == QLatin1String("song") && local != sourceId.startsWith(QLatin1String("vault:"))))
                return fail(QStringLiteral("The music source does not match its album, edition, or track identity."));
            if (!local) {
                QString providerAlbum = browseId;
                if (providerAlbum.startsWith(QLatin1String("ytm:album:"))) providerAlbum = providerAlbum.mid(10);
                // Existing provider.py's album operation accepts only a real
                // release browse key. ytm:track:, canonical recording IDs, and
                // ytm:playlist: need other owner routes that are not implemented
                // here. An in-memory source cache does not prove cold reopen.
                if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,160}$")).match(providerAlbum).hasMatch())
                    return fail(kind == QLatin1String("song")
                        ? QStringLiteral("This standalone or playlist song has no exact collection reopen route yet. Open it from an album release to add it.")
                        : QStringLiteral("This source does not expose an exact album release route for Collections."));
            }
            payload.insert(QStringLiteral("albumId"), albumId);
            payload.insert(QStringLiteral("browseId"), browseId);
            if (kind == QLatin1String("song")) payload.insert(QStringLiteral("trackId"), sourceId);
        }
        mediaId = deriveNamedMediaId(world, kind, source, sourceId,
            kind == QLatin1String("album") ? 0x4e : kind == QLatin1String("song") ? 0x4f : 0x50);
    } else {
        return fail(QStringLiteral("This item does not have a supported title route yet."));
    }

    if (mediaId.isEmpty()) {
        const QVariantMap identity = resolve(world, kind, QString(), aliases);
        if (!identity.value(QStringLiteral("available")).toBool())
            return fail(QStringLiteral("This title's exact identity is unavailable. The saved item can be retried later."));
        mediaId = identity.value(QStringLiteral("mediaId")).toString();
    }
    if (candidate.value(QStringLiteral("mediaId")).toString().size() > 80
            || candidate.value(QStringLiteral("key")).toString().size() > 2048)
        return fail(QStringLiteral("This saved reference has an invalid identity key."));
    const QString previousId = text("mediaId", 80);
    if (!previousId.isEmpty() && previousId != mediaId)
        return fail(QStringLiteral("The source now resolves to a different identity. The saved item has been kept unchanged."));
    const QString key = QString::fromUtf8(QJsonDocument(QJsonArray{mediaId, editionId}).toJson(QJsonDocument::Compact));
    const QString previousKey = text("key", 2048);
    if (!previousKey.isEmpty() && previousKey != key)
        return fail(QStringLiteral("This saved reference has an inconsistent identity key."));
    if (QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact).size() > 32768)
        return fail(QStringLiteral("The source reference is too large to save."));
    QList<QVariant> remaining{payload};
    while (!remaining.isEmpty()) {
        const QVariant value = remaining.takeLast();
        if (value.metaType().id() == QMetaType::QString) {
            if (!collectionTextValid(value.toString(), true))
                return fail(QStringLiteral("The source reference contains invalid text."));
        } else if (value.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap map = value.toMap();
            for (auto it = map.cbegin(); it != map.cend(); ++it) remaining.append(it.value());
        } else if (value.metaType().id() == QMetaType::QVariantList || value.metaType().id() == QMetaType::QStringList) {
            remaining.append(value.toList());
        }
    }
    QVariantMap reference{{QStringLiteral("key"), key}, {QStringLiteral("world"), world},
        {QStringLiteral("kind"), kind}, {QStringLiteral("mediaId"), mediaId},
        {QStringLiteral("source"), source}, {QStringLiteral("sourceId"), sourceId},
        {QStringLiteral("editionId"), editionId}, {QStringLiteral("title"), text("title", 1024)},
        {QStringLiteral("cover"), text("cover", 4096)}, {QStringLiteral("creator"), text("creator", 1024)},
        {QStringLiteral("year"), text("year", 32)}, {QStringLiteral("route"), route},
        {QStringLiteral("payload"), payload}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}, {QStringLiteral("reference"), reference}};
}

QString ColosseumTitleIdentityRegistry::knownTheatreAnchor(const QString &sourceId) const {
    if (!m_ready || sourceId.isEmpty()) return {};
    const QRegularExpression imdb(QStringLiteral("^tt[0-9]{1,20}$"));
    if (imdb.match(sourceId).hasMatch()) return sourceId;
    QSet<QString> anchors;
    const QString nameSpace = QStringLiteral("theatre-source-id");
    for (const Row &row : m_rows) {
        if (row.world != QLatin1String("theatre")
            || !row.aliases.contains(qMakePair(nameSpace, sourceId))) continue;
        for (const auto &alias : row.aliases)
            if (alias.first == nameSpace && imdb.match(alias.second).hasMatch())
                anchors.insert(alias.second);
    }
    const QString prefix = nameSpace + QChar(0x1f);
    const QString learned = m_aliasUnions.value(aliasKey(nameSpace, sourceId));
    if (!learned.isEmpty()) {
        if (!learned.startsWith(prefix) || !imdb.match(learned.mid(prefix.size())).hasMatch()) return {};
        anchors.insert(learned.mid(prefix.size()));
    }
    // This lookup is deliberately read-only, including unknown and conflicting
    // mappings. In particular it must not call resolve()/deriveIdentity().
    return anchors.size() == 1 ? *anchors.cbegin() : QString();
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
