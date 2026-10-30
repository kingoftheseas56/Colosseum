#include "VinylHomeListening.h"
#include <QCryptographicHash>
#include <QJSValue>
#include <QMetaObject>
#include <QRegularExpression>
#include <algorithm>

namespace {
QVariant plainListening(const QVariant &value) {
    return value.metaType().id() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

QString titleFor(const QString &kind) { return kind == "discovery" ? QStringLiteral("Discovery mix") : QStringLiteral("Personal mix"); }
quint32 choice(const QString &salt) {
    return QString::fromLatin1(QCryptographicHash::hash(salt.toUtf8(), QCryptographicHash::Sha256).toHex().left(8)).toUInt(nullptr, 16);
}
}

VinylHomeListening::~VinylHomeListening() { cancel(); }

VinylHomeListening::VinylHomeListening(QObject *parent, Changed changed, Batch radioBatch)
    : QObject(parent), m_changed(std::move(changed)), m_batch(std::move(radioBatch)) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        bool radioPending = false;
        for (const auto &work : m_active) if (work.job.role.startsWith("radio-")) {m_failedRadioJob=work.job;m_hasFailedRadioJob=true;radioPending=true;break;}
        if (!radioPending) for (const auto &job : m_waiting) if (job.role.startsWith("radio-")) {m_failedRadioJob=job;m_hasFailedRadioJob=true;radioPending=true;break;}
        for (const auto &work : m_active) if (!work.job.mix.isEmpty()) m_errors[work.job.mix] = QStringLiteral("Music could not finish loading. Try again.");
        for (const auto &job : m_waiting) if (!job.mix.isEmpty()) m_errors[job.mix] = QStringLiteral("Music could not finish loading. Try again.");
        cancel();
        if (m_radioActive && radioPending) m_radioState = "error";
        finishMixes(); m_changed();
    });
}

void VinylHomeListening::cancel(const QString &prefix) {
    for (const auto &lane : m_active.keys()) {
        if (!prefix.isEmpty() && !m_active[lane].job.role.startsWith(prefix)) continue;
        if (m_source) QMetaObject::invokeMethod(m_source, "cancel", Qt::DirectConnection, Q_ARG(QString, lane));
        m_active.remove(lane);
    }
    QQueue<Job> retained;
    while (!m_waiting.isEmpty()) { auto job = m_waiting.dequeue(); if (!prefix.isEmpty() && !job.role.startsWith(prefix)) retained.enqueue(job); }
    m_waiting = retained;
    if (m_active.isEmpty() && m_waiting.isEmpty()) m_deadline.stop();
}

void VinylHomeListening::clear() {
    m_hasFailedRadioJob=false;
    cancel(); m_songs.clear(); m_errors.clear(); m_started.clear(); m_openMix.clear(); m_view.clear();
    m_seeds.clear(); m_sections.clear(); m_selection.clear(); m_scope.clear(); m_generation = -1;
    m_excludedArtistProviderIds.clear();
    m_radioActive = false; m_radioSeed.clear(); m_radioSeen.clear(); m_radioAlbumSeen.clear(); m_radioArtistSeen.clear();
    m_radioAlbums.clear(); m_radioRelated.clear(); m_radioBatch.clear(); m_radioState = "idle"; m_changed();
}

void VinylHomeListening::setInputs(QObject *source, const QString &scope, int generation,
                                  const QVariantList &seeds, const QVariantList &sections, const QString &selection,
                                  const QStringList &excludedArtistProviderIds) {
    if (source != m_source || scope != m_scope || generation != m_generation) {
        clear(); m_source = source; m_scope = scope; m_generation = generation;
    }
    QSet<QString> excluded;
    for (const auto &id : excludedArtistProviderIds) if (!id.isEmpty()) excluded.insert(id);
    const bool exclusionsChanged = excluded != m_excludedArtistProviderIds;
    m_excludedArtistProviderIds = std::move(excluded);
    if (exclusionsChanged) stopRadio();
    if (selection != m_selection || exclusionsChanged) {
        cancel("mix-"); m_selection = selection; m_songs.clear(); m_errors.clear(); m_started.clear();
    }
    m_selection = selection;
    m_seeds = seeds; m_sections = sections;
    if (!m_source || m_scope.isEmpty() || seeds.size() < 5) return;
    prepareMix("personal"); prepareMix("discovery"); updateView(); m_changed();
}

bool VinylHomeListening::hasArtistIdentity(const QVariantMap &artists) const {
    const auto refsValue = artists.value("artistRefs");
    auto refs = refsValue.toList();
    if (refsValue.metaType().id() == QMetaType::QVariantMap) refs.append(refsValue);
    for (const auto &value : refs) if (!artistRef(value.toMap()).isEmpty()) return true;
    const auto idsValue = artists.value("artistIds");
    QStringList ids=idsValue.toStringList();
    for(const auto &value:idsValue.toList())ids.append(value.toString());
    if(ids.isEmpty()&&idsValue.metaType().id()==QMetaType::QString)ids.append(idsValue.toString());
    for(auto id:ids){
        if(id.startsWith("ytm:artist:"))id=id.mid(11);
        else if(id.startsWith("youtube-music:artist:"))id=id.mid(21);
        if(!artistRef({{"provider","youtube-music"},{"providerId",id}}).isEmpty())return true;
    }
    return false;
}

bool VinylHomeListening::hasExcludedArtist(const QVariantMap &artists) const {
    const auto refsValue = artists.value("artistRefs");
    auto refs = refsValue.toList();
    if (refsValue.metaType().id() == QMetaType::QVariantMap) refs.append(refsValue);
    for (const auto &value : refs) {
        const auto ref = value.toMap();
        const auto kind = ref.value("kind").toString();
        const auto provider = ref.value("provider").toString();
        if ((!kind.isEmpty() && kind != "artist") || (!provider.isEmpty() && provider != "youtube-music")) continue;
        if (m_excludedArtistProviderIds.contains(ref.value("providerId").toString())) return true;
    }
    const auto idsValue = artists.value("artistIds");
    QStringList ids = idsValue.toStringList();
    for (const auto &value : idsValue.toList()) ids.append(value.toString());
    if (ids.isEmpty() && idsValue.metaType().id() == QMetaType::QString) ids.append(idsValue.toString());
    for (auto id : ids) {
        if (id.startsWith("ytm:artist:")) id = id.mid(11);
        else if (id.startsWith("youtube-music:artist:")) id = id.mid(21);
        if (m_excludedArtistProviderIds.contains(id)) return true;
    }
    return false;
}

QString VinylHomeListening::albumKey(const QVariantMap &card) {
    const auto kind = card.value("kind").toString();
    if (!kind.isEmpty() && kind != "album" && kind != "albums") return {};
    const auto destination = card.value("destination").toMap();
    if (!destination.isEmpty() && destination.value("kind") != "album") return {};
    QString id = destination.value("providerId").toString();
    if (id.isEmpty()) id = card.value("providerId", card.value("browseId", card.value("id"))).toString();
    if (id.startsWith("ytm:album:")) id = id.mid(10);
    return id;
}

QVariantMap VinylHomeListening::artistRef(const QVariantMap &card) {
    const auto provider=card.value("provider",card.value("destination").toMap().value("provider")).toString();
    if(!provider.isEmpty() && provider!="youtube-music")return {};
    const auto destination = card.value("destination").toMap();
    QString id = destination.value("providerId", card.value("providerId", card.value("artistId", card.value("id")))).toString();
    if (id.startsWith("ytm:artist:")) id = id.mid(11);
    static const QRegularExpression channel(QStringLiteral("^UC[A-Za-z0-9_-]{22}$"));
    if (!channel.match(id).hasMatch()) return {};
    return {{"kind", "artist"}, {"provider", "youtube-music"}, {"providerId", id},
            {"name", card.value("name", card.value("title")).toString()}, {"image", card.value("image", card.value("cover")).toString()}};
}

void VinylHomeListening::prepareMix(const QString &kind) {
    if (m_started.contains(kind)) return;
    QVariantList candidates;
    for (const auto &value : m_sections) {
        const auto section = value.toMap(); const auto id = section.value("id").toString();
        if ((kind == "personal" && id == "from-your-artists") || (kind == "discovery" && id == "related-discoveries")) candidates += section.value("items").toList();
    }
    if (candidates.isEmpty()) return;
    if (kind == "personal") {
        addAlbumJobs(candidates, kind, 3);
    } else {
        QSet<QString> selected;
        for (const auto &seed : m_seeds) selected.insert(seed.toMap().value("providerId").toString());
        int count = 0;
        for (const auto &value : candidates) {
            const auto seed = artistRef(value.toMap()); const auto id = seed.value("providerId").toString();
            if (id.isEmpty() || m_excludedArtistProviderIds.contains(id) || selected.contains(id)) continue;
            selected.insert(id); enqueue({"mix-artist", kind, "artistCandidates", id, seed});
            if (++count == 2) break;
        }
    }
    if (hasJobs("mix-", kind)) { m_started.insert(kind); m_errors.remove(kind); pump(); }
}

void VinylHomeListening::addAlbumJobs(const QVariantList &cards, const QString &mix, int maximum) {
    QSet<QString> seen, artists;
    int count = 0;
    for (const auto &value : cards) {
        const auto card = value.toMap(); const auto id = albumKey(card);
        if (id.isEmpty() || seen.contains(id) || hasExcludedArtist(card)
            || card.value("kind") == "artist" || card.value("kind") == "artists") continue;
        seen.insert(id);
        const auto credits = card.value("artistRefs").toList();
        const auto artist = credits.isEmpty() ? QString() : credits.first().toMap().value("providerId").toString();
        if (!artist.isEmpty() && artists.contains(artist) && cards.size() > maximum) continue;
        artists.insert(artist); enqueue({"mix-album", mix, "album", id, {}, card});
        if (++count >= maximum) break;
    }
}

void VinylHomeListening::enqueue(const Job &job) {
    if(job.role.startsWith("radio-"))m_waiting.prepend(job);else m_waiting.enqueue(job);
}

void VinylHomeListening::pump() {
    while (m_source && m_active.size() < 2 && !m_waiting.isEmpty()) {
        const auto job = m_waiting.dequeue();
        const auto lane = m_active.contains(QStringLiteral("home-listening-0"))
            ? QStringLiteral("home-listening-1") : QStringLiteral("home-listening-0");
        int id = -1;
        const QVariantMap options = job.seed.isEmpty() ? QVariantMap() : QVariantMap{{"seed", job.seed}};
        const bool invoked = QMetaObject::invokeMethod(m_source, "request", Qt::DirectConnection, Q_RETURN_ARG(int, id),
            Q_ARG(QString, lane), Q_ARG(QString, job.operation), Q_ARG(QString, job.value), Q_ARG(QVariantMap, options));
        if (invoked && id >= 0) m_active.insert(lane, {id, job});
        else if (!job.mix.isEmpty()) m_errors[job.mix] = QStringLiteral("Music is unavailable. Try again.");
        else {m_radioState = "error";m_failedRadioJob=job;m_hasFailedRadioJob=true;}
    }
    if ((!m_active.isEmpty() || !m_waiting.isEmpty()) && !m_deadline.isActive()) m_deadline.start(15000);
    if (m_active.isEmpty() && m_waiting.isEmpty()) m_deadline.stop();
}

bool VinylHomeListening::hasJobs(const QString &role, const QString &mix) const {
    auto matches = [&](const Job &job) { return job.role.startsWith(role) && (mix.isEmpty() || job.mix == mix); };
    for (const auto &active : m_active) if (matches(active.job)) return true;
    for (const auto &job : m_waiting) if (matches(job)) return true;
    return false;
}

QString VinylHomeListening::trackKey(const QVariantMap &track) {
    const auto video=track.value("videoId").toString();
    return video.isEmpty()?track.value("id").toString():video;
}

QVariantList VinylHomeListening::albumSongs(const QVariantMap &album, const QVariantMap &context,
                                            int maximum, const QString &salt) const {
    QVariantList result; const auto tracks = album.value("tracks").toList();
    if (album.value("id").toString().isEmpty() || tracks.isEmpty()) return result;
    const int start = int(choice(salt + album.value("id").toString()) % tracks.size());
    QSet<QString> seen;
    for (int step = 0; step < tracks.size() && result.size() < maximum; ++step) {
        const int index = (start + step) % tracks.size(); const auto track = tracks[index].toMap();
        const auto key = trackKey(track);
        const bool hasTrackArtists = hasArtistIdentity(track);
        const bool excludedArtist = hasTrackArtists ? hasExcludedArtist(track)
            : hasExcludedArtist(album) || hasExcludedArtist(context);
        if (key.isEmpty() || seen.contains(key) || excludedArtist
            || track.value("available", true).toBool() == false || track.value("title").toString().isEmpty()) continue;
        seen.insert(key); result.append(QVariantMap{{"album", album}, {"index", index}, {"track", track}});
    }
    return result;
}

void VinylHomeListening::openMix(const QString &kind) {
    if (kind != "personal" && kind != "discovery") return;
    m_openMix = kind;
    if (!hasJobs("mix-", kind) && (m_songs[kind].isEmpty() || m_errors.contains(kind))) {
        m_started.remove(kind);m_songs[kind].clear();m_errors.remove(kind);prepareMix(kind);
    }
    // Opening a mix is explicit user work; do not inherit a nearly expired prewarm budget.
    if (hasJobs("mix-", kind)) m_deadline.start(15000);
    updateView(); m_changed();
}

void VinylHomeListening::updateView() {
    if (m_openMix.isEmpty()) return;
    const auto songs = m_songs.value(m_openMix);
    m_view = {{"id", m_openMix}, {"title", titleFor(m_openMix)}, {"songs", songs},
              {"status", hasJobs("mix-", m_openMix) ? "loading" : !songs.isEmpty() ? "ready" : "error"},
              {"error", m_errors.value(m_openMix, songs.isEmpty() ? QStringLiteral("No playable music is available for this mix yet.") : QString())}};
}

void VinylHomeListening::finishMixes() { updateView(); }

QVariantList VinylHomeListening::cards() const {
    QVariantList result;
    for (const auto &kind : {QStringLiteral("personal"), QStringLiteral("discovery")}) {
        if (!m_started.contains(kind)) continue;
        const auto songs = m_songs.value(kind);
        const bool loading = hasJobs("mix-", kind);
        const bool failed = m_errors.contains(kind);
        if (songs.isEmpty() && !loading && !failed) continue;
        const auto cover = songs.isEmpty() ? QString() : songs.first().toMap().value("album").toMap().value("cover").toString();
        result.append(QVariantMap{{"id", "home-mix:" + kind}, {"kind", "mixes"}, {"title", titleFor(kind)},
            {"subtitle", songs.isEmpty() ? (failed && !loading ? QStringLiteral("Unavailable") : QString()) : QStringLiteral("%1 songs").arg(songs.size())},
            {"status", loading ? "loading" : songs.isEmpty() ? "error" : "ready"}, {"cover", cover}, {"mixId", kind}});
    }
    for (const auto &value : m_seeds) {
        const auto seed = artistRef(value.toMap());
        if (seed.isEmpty() || m_excludedArtistProviderIds.contains(seed.value("providerId").toString())) continue;
        result.append(QVariantMap{{"id", "home-radio:" + seed.value("providerId").toString()}, {"kind", "radio"},
            {"title", seed.value("name").toString() + QStringLiteral(" radio")}, {"subtitle", QStringLiteral("Radio")},
            {"cover", seed.value("image", seed.value("cover"))}, {"seedArtist", seed}});
        break;
    }
    return result;
}

QVariantList VinylHomeListening::quickPicks() const {
    QVariantList result; QSet<QString> seen;
    for (const auto &kind : {QStringLiteral("personal"), QStringLiteral("discovery")}) for (const auto &value : m_songs.value(kind)) {
        const auto song = value.toMap(), album = song.value("album").toMap(), track = song.value("track").toMap();
        const auto key = trackKey(track); if (seen.contains(key)) continue; seen.insert(key);
        const auto names = track.value("artists", album.value("artists")).toStringList();
        result.append(QVariantMap{{"id", track.value("id")}, {"kind", "songs"}, {"title", track.value("title")},
            {"subtitle", names.join(QStringLiteral(" · "))}, {"cover", album.value("cover")}, {"record", album},
            {"trackIndex", song.value("index")}, {"destination", QVariantMap{{"kind", "song"}, {"id", track.value("id")}, {"playable", true}}}});
        if (result.size() == 12) return result;
    }
    return result;
}

bool VinylHomeListening::accept(const QString &lane, int id, const QVariant &raw, const QString &failure) {
    if (!lane.startsWith("home-listening-")) return false;
    if (!m_active.contains(lane) || m_active[lane].id != id) return true;
    const auto job = m_active.take(lane).job; const auto data = plainListening(raw).toMap();
    QString providerFailure = failure;
    if (providerFailure.isEmpty() && (data.value("status") == "unavailable" || data.value("status") == "error")) {
        providerFailure = data.value("error").toString();
        if (providerFailure.isEmpty()) providerFailure = QStringLiteral("The music catalogue is unavailable.");
    }
    if (!providerFailure.isEmpty()) {
        if (!job.mix.isEmpty()) m_errors[job.mix] = providerFailure;
        else {m_radioState = "error";m_failedRadioJob=job;m_hasFailedRadioJob=true;}
    } else if (job.role == "mix-artist") {
        QVariantList albums;
        for (const auto &value : data.value("items").toList()) if (value.toMap().value("relation") == "own-release" && value.toMap().value("kind") == "album") albums.append(value);
        addAlbumJobs(albums, job.mix, 1);
    } else if (job.operation == "album") {
        const auto providerIds = data.value("providerIds").toMap();
        const bool matched = data.value("id").toString() == job.value || data.value("browseId").toString() == job.value || providerIds.value("youtube-music").toString() == job.value;
        if (!matched) {
            if (!job.mix.isEmpty()) m_errors[job.mix] = QStringLiteral("The album identity could not be verified.");
            else {m_radioState = "error";m_failedRadioJob=job;m_hasFailedRadioJob=true;}
        } else {
            const auto songs = albumSongs(data, job.context, job.mix.isEmpty() ? 6 : 4, m_selection + job.mix);
            if (!job.mix.isEmpty()) {
                QSet<QString> seen;
                for (const auto &value : m_songs[job.mix]) seen.insert(trackKey(value.toMap().value("track").toMap()));
                for (const auto &song : songs) if (m_songs[job.mix].size() < 12 && !seen.contains(trackKey(song.toMap().value("track").toMap()))) {
                    seen.insert(trackKey(song.toMap().value("track").toMap())); m_songs[job.mix].append(song);
                    if (m_songs[job.mix].size() >= 12) break;
                }
            } else if (m_radioActive) {
                for (const auto &song : songs) {
                    const auto key = trackKey(song.toMap().value("track").toMap());
                    if (m_radioSeen.contains(key)) continue;
                    m_radioSeen.insert(key); m_radioBatch.append(song);
                }
            }
        }
    } else if (job.role == "radio-artist" && m_radioActive) {
        for (const auto &value : data.value("items").toList()) {
            const auto item = value.toMap();
            if (item.value("relation") == "own-release" && item.value("kind") == "album") m_radioAlbums.append(item);
            else if (item.value("relation") == "related-artist") {
                const auto related = artistRef(item);
                if (!related.isEmpty() && !m_excludedArtistProviderIds.contains(related.value("providerId").toString()))
                    m_radioRelated.append(related);
            }
        }
    }
    pump(); finishMixes();
    if (m_radioActive && !hasJobs("radio-")) continueRadio();
    m_changed(); return true;
}

void VinylHomeListening::startRadio(const QVariantMap &artist) {
    stopRadio(); m_radioSeed = artistRef(artist);
    if (m_radioSeed.isEmpty() || !m_source
        || m_excludedArtistProviderIds.contains(m_radioSeed.value("providerId").toString())) { m_radioSeed.clear(); return; }
    m_radioActive = true; m_radioAppend = false; m_radioState = "loading";
    const auto id = m_radioSeed.value("providerId").toString(); m_radioArtistSeen.insert(id);
    enqueue({"radio-artist", {}, "artistCandidates", id, m_radioSeed}); pump();
    if(hasJobs("radio-"))m_deadline.start(15000);
    m_changed();
}

void VinylHomeListening::restoreRadio(const QVariantMap &context, const QStringList &queuedTracks) {
    if (context.value("kind") != "artist-radio") return;
    stopRadio(); m_radioSeed = artistRef(context.value("seed").toMap());
    if (m_radioSeed.isEmpty() || m_excludedArtistProviderIds.contains(m_radioSeed.value("providerId").toString())) { m_radioSeed.clear(); return; }
    m_radioActive = true; m_radioAppend = true; m_radioState = "ready";
    for (const auto &id : queuedTracks) if (!id.isEmpty()) m_radioSeen.insert(id);
    m_changed();
}

void VinylHomeListening::stopRadio() {
    m_hasFailedRadioJob = false;
    cancel("radio-"); m_radioActive = false; m_radioSeed.clear(); m_radioSeen.clear(); m_radioAlbumSeen.clear();
    m_radioArtistSeen.clear(); m_radioAlbums.clear(); m_radioRelated.clear(); m_radioBatch.clear(); m_radioState = "idle";
    m_changed();
}

QVariantMap VinylHomeListening::radioContext() const {
    return m_radioActive ? QVariantMap{{"kind", "artist-radio"}, {"seed", m_radioSeed}} : QVariantMap();
}

void VinylHomeListening::refillRadio(const QStringList &queuedTracks) {
    for (const auto &id : queuedTracks) if (!id.isEmpty()) m_radioSeen.insert(id);
    if (!m_radioActive || hasJobs("radio-") || m_radioState == "exhausted") return;
    if (m_radioState=="error" && m_hasFailedRadioJob) {
        m_radioState="loading";m_hasFailedRadioJob=false;enqueue(m_failedRadioJob);pump();m_changed();return;
    }
    m_radioState = "loading"; continueRadio(); m_changed();
}

void VinylHomeListening::continueRadio() {
    if (!m_radioActive || hasJobs("radio-")) return;
    if (m_radioState == "error") return;
    if (!m_radioBatch.isEmpty()) {
        const auto songs = m_radioBatch; m_radioBatch.clear(); m_radioState = "ready";
        const bool append = m_radioAppend; m_radioAppend = true; m_batch(songs, append); return;
    }
    while (!m_radioAlbums.isEmpty()) {
        const auto card = m_radioAlbums.takeFirst().toMap(); const auto id = albumKey(card);
        if (id.isEmpty() || m_radioAlbumSeen.contains(id)) continue;
        m_radioAlbumSeen.insert(id); enqueue({"radio-album", {}, "album", id, {}, card}); pump(); return;
    }
    while (!m_radioRelated.isEmpty()) {
        const auto seed = artistRef(m_radioRelated.takeFirst().toMap()); const auto id = seed.value("providerId").toString();
        if (id.isEmpty() || m_excludedArtistProviderIds.contains(id)
            || m_radioArtistSeen.contains(id) || m_radioArtistSeen.size() >= 12) continue;
        m_radioArtistSeen.insert(id); enqueue({"radio-artist", {}, "artistCandidates", id, seed}); pump(); return;
    }
    if (m_radioArtistSeen.isEmpty() && !m_radioSeed.isEmpty()) {
        const auto id = m_radioSeed.value("providerId").toString(); m_radioArtistSeen.insert(id);
        enqueue({"radio-artist", {}, "artistCandidates", id, m_radioSeed}); pump(); return;
    }
    m_radioState = "exhausted";
}
