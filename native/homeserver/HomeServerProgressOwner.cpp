#include "HomeServerProgressOwner.h"
#include "HomeServerCatalog.h"
#include <cmath>

namespace {
qint64 milliseconds(const QVariant &seconds) {
    const double n = seconds.toDouble();
    return std::isfinite(n) && n >= 0 && n <= 31536000 ? qRound64(n * 1000) : 0;
}
bool serverOnly(const QVariantMap &entry, const QString &connection = {}) {
    const auto resume = entry.value("resume").toMap();
    const auto key = resume.value("infoHash").toString();
    return resume.value("localPath").toString().isEmpty()
        && key.startsWith(connection.isEmpty() ? "home-server:" : "home-server:" + connection + ':');
}
QVariantMap entryFor(const QJsonObject &item, const QString &id, const QString &sourceKey,
                     const HomeServerProgressState &state, QVariantMap entry) {
    auto resume = entry.value("resume").toMap();
    entry.remove("_homeServerHidden");
    if (entry.isEmpty() || serverOnly(entry)) {
        resume.insert("infoHash", sourceKey);
        resume.insert("fileIdx", 0);
        resume.insert("localPath", QString());
        resume.insert("subType", item.value("kind") == "episode" ? "series" : "movie");
        resume.insert("subId", id);
    }
    resume.insert("position", state.positionMs / 1000.0);
    entry.insert("id", id); entry.insert("kind", "video");
    if (entry.value("title").toString().isEmpty()) entry.insert("title", item.value("title").toString());
    if (entry.value("caption").toString().isEmpty()) entry.insert("caption", item.value("title").toString());
    entry.insert("resume", resume);
    entry.insert("duration", state.durationMs / 1000.0);
    entry.insert("progress", state.played ? 1.0 : state.durationMs > 0 ? qBound(0.0, double(state.positionMs) / state.durationMs, 1.0) : 0.0);
    entry.insert("watched", state.played);
    entry.insert("updatedAt", state.updatedAt);
    auto identity = item.value("identity").toObject().toVariantMap();
    identity.insert("type", item.value("kind") == "movie" ? "movie" : "series");
    entry.insert("_homeServerIdentity", identity);
    return entry;
}
}
namespace HomeServerProgress {
IdentityAliases identityAliases(const QVariantList &entries) {
    IdentityAliases aliases;
    const auto add = [&aliases](const QString &alias, const QString &canonical) {
        if (alias.isEmpty()) return;
        if (aliases.contains(alias) && aliases.value(alias) != canonical) aliases[alias] = QString();
        else aliases.insert(alias, canonical);
    };
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        const auto identity = QJsonObject::fromVariantMap(entry.value("_homeServerIdentity").toMap());
        const auto kind = identity.value("type").toString();
        if (entry.value("kind") != "video" || (kind != "movie" && kind != "series")) continue;
        const QJsonObject reference{{"kind",kind},{"identity",identity}};
        const auto canonical = HomeServerCatalog::canonicalVideoId(reference);
        if (!canonical.startsWith("tt") || canonical != entry.value("id").toString()) continue;
        for (const auto *key : {"tmdbId", "tvdbId"}) {
            auto external = identity; external.remove("imdbId");
            external.remove(QString(key) == "tmdbId" ? "tvdbId" : "tmdbId");
            const auto alias = HomeServerCatalog::canonicalVideoId({{"kind",kind},{"identity",external}});
            add(alias, canonical);
            if (kind == "series") {
                external.remove("season"); external.remove("episode");
                add(HomeServerCatalog::canonicalVideoId({{"kind",kind},{"identity",external}}), canonical.section(':',0,0));
            }
        }
    }
    return aliases;
}
QJsonObject withAliases(QJsonObject item, const IdentityAliases &aliases) {
    auto identity = item.value("identity").toObject();
    if (!identity.value("imdbId").toString().isEmpty()) return item;
    for (const auto *key : {"tmdbId", "tvdbId"}) {
        auto external = identity;
        external.remove(QString(key) == "tmdbId" ? "tvdbId" : "tmdbId");
        auto reference = item; reference.insert("identity", external);
        auto canonical = aliases.value(HomeServerCatalog::canonicalVideoId(reference));
        if (canonical.isEmpty() && item.value("kind") == "episode") {
            external.remove("season"); external.remove("episode");
            reference.insert("kind", "series"); reference.insert("identity", external);
            canonical = aliases.value(HomeServerCatalog::canonicalVideoId(reference));
        }
        if (!canonical.startsWith("tt")) continue;
        identity.insert("imdbId", canonical.section(':', 0, 0));
        item.insert("identity", identity);
        break;
    }
    return item;
}
Decision reconcile(HomeServerProgressOwner &owner, const QJsonObject &item, const QString &sourceKey) {
    Decision result;
    if (item.value("kind") != "movie" && item.value("kind") != "episode") return result;
    result.canonicalId = HomeServerCatalog::canonicalVideoId(item);
    if (result.canonicalId.isEmpty()) return result;
    const auto progress = item.value("progress").toObject();
    const qint64 remoteAt = qMax<qint64>(0, progress.value("updatedAt").toInteger());
    auto local = owner.get(result.canonicalId);
    if (local.value("_homeServerHidden").toBool()) {
        local.remove("_homeServerHidden");
        auto resume = local.value("resume").toMap();
        if (serverOnly(local)) resume.insert("infoHash", sourceKey);
        local.insert("resume", resume);
        owner.apply(local);
    }
    const auto watch = owner.watch(result.canonicalId);
    const qint64 localAt = qMax<qint64>(0, local.value("updatedAt").toLongLong());
    const qint64 actionAt = watch.manual && watch.mark ? watch.updatedAt : 0;
    if ((!local.isEmpty() || actionAt > 0) && qMax(localAt, actionAt) >= remoteAt) {
        auto &state = result.state;
        state.positionMs = milliseconds(local.value("resume").toMap().value("position"));
        state.durationMs = milliseconds(local.value("duration"));
        state.updatedAt = qMax(localAt, actionAt);
        state.played = local.value("watched").toBool() || local.value("progress").toDouble() >= .9;
        state.writePlayed = state.played;
        if (watch.manual && watch.mark && actionAt >= localAt) {
            state.played = watch.mark > 0; state.writePlayed = true;
        } else if (!state.played) {
            // Watching an already-played item again is not an unwatch action.
            state.played = progress.value("played").toBool();
            state.writePlayed = false;
        }
        if (!state.durationMs) state.durationMs = qMax<qint64>(0, progress.value("durationMs").toInteger());
        if (state.durationMs > 0) state.positionMs = qMin(state.positionMs, state.durationMs);
        result.localWins = state.updatedAt > remoteAt;
        return result;
    }
    auto &state = result.state;
    state.positionMs = qMax<qint64>(0, progress.value("positionMs").toInteger());
    state.durationMs = qMax<qint64>(0, progress.value("durationMs").toInteger());
    if (state.durationMs > 0) state.positionMs = qMin(state.positionMs, state.durationMs);
    state.updatedAt = remoteAt; state.played = progress.value("played").toBool();
    if (local.isEmpty() && state.positionMs == 0 && !state.played && remoteAt == 0) return result;
    owner.apply(entryFor(item, result.canonicalId, sourceKey, state, local));
    // This existing mark owner is title-wide. Never apply an episode state to it.
    if (item.value("kind") == "movie" && (state.played || watch.mark != 0))
        owner.applyWatched(result.canonicalId, state.played, remoteAt);
    return result;
}
void hideConnection(HomeServerProgressOwner &owner, const QString &connectionId) {
    for (const auto &value : owner.entries()) {
        auto entry = value.toMap();
        if (entry.value("kind") == "video" && serverOnly(entry, connectionId)) {
            entry.insert("_homeServerHidden", true);
            owner.apply(entry);
        }
    }
}
void completed(HomeServerProgressOwner &owner, const QJsonObject &item,
               const QString &sourceKey, qint64 at) {
    const auto id = HomeServerCatalog::canonicalVideoId(item);
    if (id.isEmpty() || at <= 0 || (item.value("kind") != "movie" && item.value("kind") != "episode")) return;
    const auto previous = owner.get(id);
    if (previous.value("updatedAt").toLongLong() > at) return;
    HomeServerProgressState state;
    state.durationMs = item.value("progress").toObject().value("durationMs").toInteger();
    if (state.durationMs <= 0) state.durationMs = milliseconds(previous.value("duration"));
    state.positionMs = state.durationMs; state.updatedAt = at; state.played = true;
    owner.apply(entryFor(item, id, sourceKey, state, previous));
    if (item.value("kind") == "movie") owner.applyWatched(id, true, at);
}
}
