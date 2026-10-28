#include "ArtistServiceBridge.h"

#include <QRegularExpression>
#include <QSet>

namespace vinyl::artist {
namespace {
int key(Channel channel) { return static_cast<int>(channel); }
bool isTerminal(State state) {
    return state == State::Ready || state == State::Empty ||
           state == State::NotFound || state == State::Unavailable || state == State::Error;
}
} // namespace

ArtistServiceBridge::ArtistServiceBridge(IArtistReadPort &reader, ISavedArtistPort &saved,
                                         IPlaybackCommandPort &playback, QObject *parent)
    : QObject(parent), m_reader(reader), m_savedPort(saved), m_playbackPort(playback) {
    clearSlots();
}
ArtistServiceBridge::~ArtistServiceBridge() { cancelPending(); }

bool ArtistServiceBridge::validMbid(const QString &id) {
    static const QRegularExpression uuid(QStringLiteral(
        "^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
    return uuid.match(id).hasMatch() && id.compare(QStringLiteral("00000000-0000-0000-0000-000000000000"), Qt::CaseInsensitive) != 0;
}
bool ArtistServiceBridge::validProfile(const QString &profile) {
    if (profile.isEmpty() || profile.size() > 128 || profile != profile.trimmed()) return false;
    for (const QChar ch : profile)
        if (ch.unicode() < 32 || ch.unicode() == 127) return false;
    return true;
}
bool ArtistServiceBridge::validContext() const {
    return m_active && validProfile(m_profile) && validMbid(m_artistId);
}
void ArtistServiceBridge::cancelPending() {
    const auto tickets = m_pending.values();
    m_pending.clear(); // even a synchronously delivered cancel callback is now stale
    for (const auto &ticket : tickets) m_reader.cancel(ticket.request.token);
}
void ArtistServiceBridge::clearSlots() {
    m_slots.clear();
    for (const auto channel : {Channel::Artist, Channel::Discography, Channel::Media, Channel::Artwork})
        m_slots.insert(key(channel), Slot{});
}
void ArtistServiceBridge::resetContext() {
    ++m_generation;
    cancelPending();
    m_lastIntent.clear();
    clearSlots();
    m_artistId.clear();
    m_filter = QStringLiteral("all");
    m_active = false;
    m_savedState = State::Idle;
    m_saved = false;
    m_savedError.clear();
    m_playbackError.clear();
    emit contextChanged();
    emit resultsChanged();
    emit savedChanged();
    emit playbackErrorChanged();
}
bool ArtistServiceBridge::enterArtist(const QString &profile, const QString &id) {
    if (!validProfile(profile) || !validMbid(id)) {
        emit rejected(Channel::Artist, QStringLiteral("invalid_identity"));
        return false;
    }
    resetContext();
    m_profile = profile;
    m_artistId = id.toLower();
    m_active = true;
    emit contextChanged();
    refreshSavedArtist(); // profile owner decides membership, never artist name
    return true;
}
void ArtistServiceBridge::leaveArtist() {
    resetContext();
    // Deliberately no playback command; the active queue and player survive browsing.
}
bool ArtistServiceBridge::setProfile(const QString &profile) {
    if (!validProfile(profile)) {
        emit rejected(Channel::Artist, QStringLiteral("invalid_profile"));
        return false;
    }
    resetContext();
    m_profile = profile;
    emit contextChanged();
    return true; // re-enterArtist is required even for an identical artist ID
}
const ArtistServiceBridge::Slot &ArtistServiceBridge::slot(Channel channel) const {
    static const Slot empty;
    const auto it = m_slots.constFind(key(channel));
    return it == m_slots.cend() ? empty : it.value();
}
ArtistServiceBridge::Slot &ArtistServiceBridge::slot(Channel channel) {
    return m_slots[key(channel)];
}
State ArtistServiceBridge::artistState() const { return slot(Channel::Artist).state; }
State ArtistServiceBridge::discographyState() const { return slot(Channel::Discography).state; }
State ArtistServiceBridge::mediaState() const { return slot(Channel::Media).state; }
State ArtistServiceBridge::artworkState() const { return slot(Channel::Artwork).state; }
QVariantMap ArtistServiceBridge::artist() const { return slot(Channel::Artist).entity; }
QVariantList ArtistServiceBridge::discography() const { return slot(Channel::Discography).items; }
QVariantMap ArtistServiceBridge::media() const { return slot(Channel::Media).entity; }
QVariantMap ArtistServiceBridge::artwork() const { return slot(Channel::Artwork).entity; }
QString ArtistServiceBridge::nextCursor() const { return slot(Channel::Discography).nextCursor; }

void ArtistServiceBridge::setError(Channel channel, const QString &code, const QString &message) {
    auto &s = slot(channel);
    s.state = State::Error;
    s.errorCode = code.isEmpty() ? QStringLiteral("unknown_error") : code;
    s.message = message.isEmpty() ? QStringLiteral("Request failed. Try again.") : message;
    emit resultsChanged();
}
bool ArtistServiceBridge::issue(Channel channel, const QString &cursor,
                                const QString &requestedFilter, bool continuation) {
    if (!validContext()) {
        emit rejected(channel, QStringLiteral("inactive_context"));
        return false;
    }
    QString filter;
    if (channel == Channel::Discography) {
        filter = requestedFilter.isEmpty() ? m_filter : requestedFilter;
        static const QRegularExpression legal(QStringLiteral("^[a-z0-9_-]{1,40}$"));
        if (!legal.match(filter).hasMatch()) {
            emit rejected(channel, QStringLiteral("invalid_query"));
            return false;
        }
        if (continuation) {
            const Slot &prior = slot(channel);
            if (cursor.isEmpty() || prior.nextCursor != cursor || filter != m_filter ||
                !prior.source.valid() || prior.items.isEmpty()) {
                emit rejected(channel, QStringLiteral("invalid_page"));
                return false;
            }
        } else if (!cursor.isEmpty()) {
            emit rejected(channel, QStringLiteral("invalid_page"));
            return false;
        }
    } else if (continuation || !cursor.isEmpty() || !requestedFilter.isEmpty()) {
        emit rejected(channel, QStringLiteral("invalid_query"));
        return false;
    }

    auto old = m_pending.take(key(channel));
    if (old.request.token != 0) m_reader.cancel(old.request.token);
    auto &s = slot(channel);
    SourceStamp anchor;
    if (continuation) anchor = s.source;
    else s = Slot{};
    if (channel == Channel::Discography && !continuation) m_filter = filter;
    s.state = State::Loading;
    s.errorCode.clear();
    s.message.clear();

    ReadRequest req;
    req.channel = channel;
    req.token = ++m_serial;
    req.generation = m_generation;
    req.profileScopeId = m_profile;
    req.artistMbid = m_artistId;
    req.cursor = cursor;
    req.filter = filter;
    req.pageSource = anchor;
    const Ticket ticket{req, continuation};
    m_pending.insert(key(channel), ticket);
    m_lastIntent.insert(key(channel), ticket);
    emit resultsChanged();

    // Port replies must run on this QObject's thread. QPointer also protects a late
    // callback after bridge destruction; generation/token independently fence late data.
    QPointer<ArtistServiceBridge> self(this);
    m_reader.request(req, [self](ReadReply reply) {
        if (self) self->accept(reply);
    });
    return true;
}
bool ArtistServiceBridge::requestArtist(const QString &id) {
    if (!validContext() || !validMbid(id) || id.toLower() != m_artistId) {
        emit rejected(Channel::Artist, QStringLiteral("artist_mismatch"));
        return false;
    }
    return issue(Channel::Artist);
}
bool ArtistServiceBridge::requestDiscography(const QString &cursor, const QString &filter) {
    return issue(Channel::Discography, cursor, filter, !cursor.isEmpty());
}
bool ArtistServiceBridge::requestMedia() { return issue(Channel::Media); }
bool ArtistServiceBridge::requestArtwork() { return issue(Channel::Artwork); }
bool ArtistServiceBridge::retry(Channel channel) {
    if (!validContext() || !m_lastIntent.contains(key(channel))) {
        emit rejected(channel, QStringLiteral("no_retry"));
        return false;
    }
    const auto previous = m_lastIntent.value(key(channel));
    if (m_pending.contains(key(channel))) {
        emit rejected(channel, QStringLiteral("already_loading"));
        return false;
    }
    if (slot(channel).state != State::Error && slot(channel).state != State::Unavailable &&
        slot(channel).state != State::NotFound) {
        emit rejected(channel, QStringLiteral("not_retryable"));
        return false;
    }
    return issue(channel, previous.request.cursor, previous.request.filter, previous.continuation);
}

void ArtistServiceBridge::accept(const ReadReply &reply) {
    const int k = key(reply.channel);
    const auto found = m_pending.constFind(k);
    if (found == m_pending.cend() || found->request.token != reply.token) return;
    const Ticket ticket = found.value();
    // Check ALL fences before mutating any view state.
    if (!m_active || ticket.request.generation != m_generation ||
        ticket.request.artistMbid != m_artistId ||
        ticket.request.profileScopeId != m_profile) return;
    m_pending.remove(k);
    if (reply.profileScopeId != m_profile || reply.artistMbid.toLower() != m_artistId) {
        setError(reply.channel, QStringLiteral("identity_mismatch"),
                 QStringLiteral("The response belongs to another artist or profile. Retry."));
        return;
    }
    if (reply.channel == Channel::Discography &&
        (reply.cursor != ticket.request.cursor || reply.filter != ticket.request.filter)) {
        setError(reply.channel, QStringLiteral("invalid_page"),
                 QStringLiteral("The continuation does not match this query. Retry."));
        return;
    }
    if (!isTerminal(reply.state)) {
        setError(reply.channel, QStringLiteral("invalid_state"), QStringLiteral("Invalid service response."));
        return;
    }
    Slot &s = slot(reply.channel);
    if (reply.state == State::Error || reply.state == State::Unavailable ||
        reply.state == State::NotFound) {
        s.state = reply.state;
        s.errorCode = reply.errorCode.isEmpty()
            ? (reply.state == State::Unavailable ? QStringLiteral("unavailable")
               : reply.state == State::NotFound ? QStringLiteral("not_found")
               : QStringLiteral("error"))
            : reply.errorCode;
        s.message = reply.message;
        emit resultsChanged();
        return;
    }
    if (ticket.continuation && (!reply.source.valid() || !(ticket.request.pageSource == reply.source))) {
        setError(reply.channel, QStringLiteral("invalid_page"),
                 QStringLiteral("The catalogue revision changed. Restart this collection."));
        return;
    }
    if (reply.state == State::Empty) {
        if (!reply.nextCursor.isEmpty() || !reply.items.isEmpty() || !reply.entity.isEmpty()) {
            setError(reply.channel, QStringLiteral("invalid_payload"), QStringLiteral("Empty response carried content."));
            return;
        }
        if (ticket.continuation) {
            s.state = State::Ready; // earlier pages remain visible
            s.nextCursor.clear();
        } else {
            s.state = State::Empty;
            s.nextCursor.clear();
        }
        s.errorCode.clear();
        s.message.clear();
        emit resultsChanged();
        return;
    }
    if (!reply.source.valid()) {
        setError(reply.channel, QStringLiteral("missing_source"),
                 QStringLiteral("Source identity is missing. Retry."));
        return;
    }
    if (reply.channel == Channel::Discography) {
        if (ticket.continuation && !(ticket.request.pageSource == reply.source)) {
            setError(reply.channel, QStringLiteral("invalid_page"),
                     QStringLiteral("The catalogue revision changed. Restart this collection."));
            return;
        }
        if (reply.items.isEmpty() || reply.items.size() > 100 ||
            (!reply.nextCursor.isEmpty() && reply.nextCursor == reply.cursor)) {
            setError(reply.channel, QStringLiteral("invalid_page"),
                     QStringLiteral("The catalogue returned an invalid page."));
            return;
        }
        QSet<QString> seen;
        for (const auto &row : s.items) seen.insert(row.toMap().value("releaseGroupMbid").toString().toLower());
        for (const auto &row : reply.items) {
            const auto id = row.toMap().value("releaseGroupMbid").toString().toLower();
            if (!validMbid(id) || seen.contains(id)) {
                setError(reply.channel, QStringLiteral("invalid_payload"),
                         QStringLiteral("A release group is missing or duplicated."));
                return;
            }
            seen.insert(id);
        }
        if (ticket.continuation) s.items.append(reply.items);
        else s.items = reply.items;
        s.source = reply.source;
        s.nextCursor = reply.nextCursor;
    } else {
        // QML receives projections, not a guess based on titles, providers, or covers.
        if (reply.entity.value("artistMbid").toString().toLower() != m_artistId ||
            (reply.channel == Channel::Artist &&
             reply.entity.value("name").toString().trimmed().isEmpty())) {
            setError(reply.channel, QStringLiteral("identity_mismatch"),
                     QStringLiteral("Returned content has no matching Artist MBID."));
            return;
        }
        s.entity = reply.entity;
        s.source = reply.source;
    }
    s.state = State::Ready;
    s.errorCode.clear();
    s.message.clear();
    emit resultsChanged();
}

bool ArtistServiceBridge::refreshSaved(bool writing, bool target) {
    if (!validContext()) return false;
    const auto profile = m_profile;
    const auto artist = m_artistId;
    const auto generation = m_generation;
    const SavedReply reply = writing ? m_savedPort.write(profile, artist, target)
                                    : m_savedPort.read(profile, artist);
    if (!m_active || m_generation != generation || m_profile != profile || m_artistId != artist)
        return false; // reentrancy/profile change: never project old-profile state
    if (reply.profileScopeId != profile || reply.artistMbid.toLower() != artist) {
        m_savedState = State::Error;
        m_savedError = QStringLiteral("identity_mismatch");
    } else if (reply.state != State::Ready) {
        m_savedState = reply.state == State::Unavailable ? State::Unavailable : State::Error;
        m_savedError = reply.errorCode.isEmpty() ? QStringLiteral("saved_unavailable") : reply.errorCode;
    } else {
        m_savedState = State::Ready;
        m_saved = reply.saved;
        m_savedError.clear();
    }
    emit savedChanged();
    return m_savedState == State::Ready;
}
bool ArtistServiceBridge::refreshSavedArtist() { return refreshSaved(false, false); }
bool ArtistServiceBridge::toggleSavedArtist() {
    if (!validContext() || m_savedState != State::Ready) {
        emit rejected(Channel::Artist, QStringLiteral("saved_not_ready"));
        return false;
    }
    return refreshSaved(true, !m_saved);
}

bool ArtistServiceBridge::validateSelection(const QString &artist, const QString &group,
                                             const QString &release, const QString &recording) {
    auto fail = [this](const QString &message) {
        m_playbackError = message;
        emit playbackErrorChanged();
        return false;
    };
    if (!validContext() || !validMbid(artist) || artist.toLower() != m_artistId)
        return fail(QStringLiteral("Artist identity does not match the active page."));
    if (!validMbid(group) || !validMbid(release) ||
        (!recording.isEmpty() && !validMbid(recording)))
        return fail(QStringLiteral("Selection requires canonical Release Group, Release and Recording MBIDs."));
    const auto pending = m_pending.constFind(key(Channel::Discography));
    const bool usableDiscography = discographyState() == State::Ready
        || (discographyState() == State::Loading && pending != m_pending.cend()
            && pending->continuation && slot(Channel::Discography).source.valid());
    if (artistState() != State::Ready || !usableDiscography)
        return fail(QStringLiteral("Catalogue identity is not ready. Retry the artist and discography."));
    bool found = false;
    for (const auto &v : discography()) {
        if (v.toMap().value("releaseGroupMbid").toString().compare(group, Qt::CaseInsensitive) == 0) {
            found = true;
            break;
        }
    }
    if (!found) return fail(QStringLiteral("Release Group is not in the active artist's catalogue."));
    return true;
}
bool ArtistServiceBridge::dispatchSelection(ExactSelection selection, bool recording) {
    m_playbackError.clear();
    const CommandReply result = recording ? m_playbackPort.selectRecording(selection)
                                          : m_playbackPort.selectRelease(selection);
    if (!result.accepted)
        m_playbackError = result.message.isEmpty()
            ? QStringLiteral("Exact selection unavailable. Retry.") : result.message;
    emit playbackErrorChanged();
    return result.accepted;
}
bool ArtistServiceBridge::selectRecording(const QString &artist, const QString &group,
                                          const QString &release, const QString &recording) {
    if (recording.isEmpty()) {
        m_playbackError = QStringLiteral("Recording MBID is required. Provider video IDs are not valid.");
        emit playbackErrorChanged();
        return false;
    }
    if (!validateSelection(artist, group, release, recording)) return false;
    return dispatchSelection({{m_profile}, {m_artistId}, {group.toLower()},
                              {release.toLower()}, {recording.toLower()}}, true);
}
bool ArtistServiceBridge::selectRelease(const QString &artist, const QString &group,
                                        const QString &release) {
    if (!validateSelection(artist, group, release, {})) return false;
    return dispatchSelection({{m_profile}, {m_artistId}, {group.toLower()}, {release.toLower()}, {}}, false);
}

QString ArtistServiceBridge::stateName(State state) {
    switch (state) {
    case State::Idle: return QStringLiteral("Idle");
    case State::Loading: return QStringLiteral("Loading");
    case State::Ready: return QStringLiteral("Ready");
    case State::Empty: return QStringLiteral("Empty");
    case State::NotFound: return QStringLiteral("NotFound");
    case State::Unavailable: return QStringLiteral("Unavailable");
    case State::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Error");
}
QVariantMap ArtistServiceBridge::result(Channel channel) const {
    const auto &s = slot(channel);
    return {{"state", stateName(s.state)}, {"errorCode", s.errorCode},
            {"message", s.message}, {"source", s.source.toMap()}, {"entity", s.entity},
            {"items", s.items}, {"nextCursor", s.nextCursor},
            {"artistMbid", m_artistId}, {"profileScopeId", m_profile},
            {"generation", QVariant::fromValue<qulonglong>(m_generation)}};
}
} // namespace vinyl::artist
