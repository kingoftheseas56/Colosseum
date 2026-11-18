#include "AudiobookTorrents.h"

#include "BookTorrentRanker.h"
#include "TankorentSearchService.h"

#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>

namespace {
QString normalized(QString text)
{
    static const QRegularExpression separators(QStringLiteral(R"([^\p{L}\p{M}\p{N}]+)"));
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .replace(separators, QStringLiteral(" ")).trimmed();
}

QString workAuthor(const QVariantMap& work)
{
    QStringList names;
    for (const auto& value : work.value("authors").toList()) {
        const auto name = value.toMap().value("name").toString().simplified();
        if (normalized(name).isEmpty())
            return {};
        names.append(name);
    }
    return names.join(QStringLiteral(", "));
}

QList<QStringList> knownNarrations(const QVariantList& recordings)
{
    QList<QStringList> groups;
    for (const auto& recording : recordings) {
        QStringList names;
        for (const auto& narrator : recording.toMap().value("narrators").toList()) {
            const auto name = narrator.toMap().value("name").toString().simplified();
            if (!name.isEmpty())
                names.append(name);
        }
        if (!names.isEmpty() && !groups.contains(names))
            groups.append(names);
    }
    return groups;
}

// The worker supplies listing titles and categories. Require the complete
// catalogue title and author to be adjacent, and accept only audio release
// descriptors or a narration credit after them. This prevents a longer title
// (for example a sequel or an upload containing a second work) from matching.
bool workEvidence(const TorrentResult& result, const QString& title, const QString& author,
                  const QList<QStringList>& known, QStringList& narrators)
{
    const QString listing = normalized(result.title);
    const QStringList prefixes{
        title + QStringLiteral(" by ") + author,
        title + QStringLiteral(" ") + author,
        author + QStringLiteral(" ") + title
    };
    const auto prefix = std::find_if(prefixes.cbegin(), prefixes.cend(), [&](const auto& value) {
        return listing == value || listing.startsWith(value + QLatin1Char(' '));
    });
    if (prefix == prefixes.cend())
        return false;

    QString remainder = listing.mid(prefix->size());
    const auto category = normalized(result.category);
    if (result.sourceKey == QStringLiteral("piratebay") && !result.categoryId.isEmpty()
        && !result.categoryId.startsWith(QLatin1Char('1')))
        return false;
    if (category == QStringLiteral("movies") || category == QStringLiteral("tv")
        || category == QStringLiteral("documentaries"))
        return false;
    const bool audiobookCategory = (result.sourceKey == QStringLiteral("piratebay")
        && result.categoryId == QStringLiteral("102")) || category == QStringLiteral("audiobooks");
    TorrentResult releaseEvidence;
    releaseEvidence.title = remainder;
    if (!audiobookCategory && !BookTorrentRanker::isAudiobook(releaseEvidence)
        && BookTorrentRanker::guessAudioFormat(remainder).isEmpty())
        return false;

    static const QRegularExpression audioDetails(QStringLiteral(
        R"(\b(?:audio books?|audiobooks?|m4b|mp3|m4a|flac|ogg|opus|aac|wav|unabridged|abridged|english|stereo|mono|\d{2,3} ?kbps)\b)"));
    remainder = remainder.remove(audioDetails).simplified();
    if (remainder.isEmpty())
        return true;

    static const QRegularExpression credit(QStringLiteral(R"(^(?:narrated by|read by|narrator) )"));
    const bool explicitCredit = credit.match(remainder).hasMatch();
    remainder.remove(credit);
    for (const auto& names : known) {
        if (remainder == normalized(names.join(QStringLiteral(" ")))
            || remainder == normalized(names.join(QStringLiteral(" and ")))) {
            narrators = names;
            return true;
        }
    }
    // Unknown explicit credits stay in the original listing title. Never
    // attach catalogue narrator metadata to an unproven version.
    static const QRegularExpression nameLike(QStringLiteral(R"(^[\p{L}\p{M} ]{2,100}$)"));
    static const QRegularExpression nonCredit(QStringLiteral(
        R"(\b(?:another|anthology|bonus|books?|bundle|collection|complete|edition|franchise|movie|novel|omnibus|part|preview|sample|series|stor(?:y|ies)|trailer|video|vol|volumes?)\b)"));
    return explicitCredit && nameLike.match(remainder).hasMatch()
        && !nonCredit.match(remainder).hasMatch();
}

QVariantList buildRows(const QVariantMap& work, const QString& pairKey,
                       const QList<TorrentResult>& results)
{
    const auto title = work.value("title").toString();
    const auto author = workAuthor(work);
    const auto normalizedTitle = normalized(title);
    const auto normalizedAuthor = normalized(author);
    const auto known = knownNarrations(work.value("recordings").toList());

    QList<TorrentResult> ordered = results;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.seeders > b.seeders;
    });
    QSet<QString> seen;
    QVariantList rows;
    for (const auto& result : ordered) {
        const auto hash = canonicalizeInfoHash(result.infoHash);
        if (hash.isEmpty() || seen.contains(hash) || BookTorrentRanker::looksLikeBundle(result.title))
            continue;
        QStringList narrators;
        if (!workEvidence(result, normalizedTitle, normalizedAuthor, known, narrators))
            continue;

        const auto format = BookTorrentRanker::guessAudioFormat(result.title).toUpper();
        // AAX is not supported by the native audiobook torrent file picker.
        if (format == QStringLiteral("AAX"))
            continue;
        seen.insert(hash);

        const QVariantMap request{
            {"sourceKind", QStringLiteral("tankorent")},
            {"sourceRef", result.sourceKey + QLatin1Char(':') + hash},
            {"pairKey", pairKey},
            {"recordingKey", pairKey + QStringLiteral("#src:") + hash},
            {"workId", work.value("id")},
            {"title", title},
            {"author", author},
            {"infoHash", hash},
            {"narrators", narrators},
            {"format", format}
        };
        rows.append(QVariantMap{
            {"title", result.title},
            {"author", author},
            {"narrators", narrators},
            {"format", format},
            {"size", humanSize(result.sizeBytes)},
            {"sourceLabel", QStringLiteral("Tankorent")},
            {"seeders", result.seeders},
            {"unresolved", true},
            {"reason", AudiobookTorrents::tr("Edition unconfirmed")},
            {"request", request}
        });
    }
    return rows;
}
}

AudiobookTorrents::AudiobookTorrents(QNetworkAccessManager* searchNam, QObject* parent)
    : QObject(parent), m_search(new TankorentSearchService(searchNam, this))
{
    wire();
}

AudiobookTorrents::AudiobookTorrents(TankorentSearchService* search, QObject* parent)
    : QObject(parent), m_search(search)
{
    if (m_search)
        m_search->setParent(this);
    wire();
}

void AudiobookTorrents::wire()
{
    connect(m_search, &TankorentSearchService::resultsReady, this, &AudiobookTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::searchFinished, this, &AudiobookTorrents::onSearchDone);
    connect(m_search, &TankorentSearchService::indexerError, this, [this](const QString& handle) {
        if (handle == m_handle)
            m_indexerFailed = true;
    });
}

bool AudiobookTorrents::isEligible(const QVariantMap& work) const
{
    const auto series = work.value("series");
    const auto recordings = work.value("recordings");
    const auto title = work.value("title").toString();
    return !work.value("id").toString().trimmed().isEmpty()
        && !normalized(title).isEmpty() && !workAuthor(work).isEmpty()
        && series.metaType().id() == QMetaType::QVariantList && series.toList().isEmpty()
        && recordings.metaType().id() == QMetaType::QVariantList && !recordings.toList().isEmpty()
        && !work.value("bundle").toBool() && !BookTorrentRanker::looksLikeBundle(title)
        && !work.value("franchise").toBool() && work.value("franchise").toMap().isEmpty()
        && work.value("franchiseId").toString().trimmed().isEmpty()
        && work.value("franchises").toList().isEmpty();
}

void AudiobookTorrents::search(const QString& token, const QVariantMap& work,
                               const QString& recordingId, const QString& pairKey)
{
    if (token.isEmpty())
        return;
    cancel(m_token);
    const auto workId = work.value("id").toString();
    const auto recordings = work.value("recordings").toList();
    const auto targetCount = std::count_if(recordings.cbegin(), recordings.cend(), [&](const auto& value) {
        const auto recording = value.toMap();
        return recording.value("id").toString() == recordingId
            && recording.value("workId").toString() == workId && !recording.value("bundle").toBool();
    });
    if (!m_search || !isEligible(work) || recordingId.trimmed().isEmpty()
        || targetCount != 1 || pairKey.trimmed().isEmpty()) {
        emit searchFinished(token, {}, tr("Tankorent requires a standalone audiobook."));
        return;
    }

    m_token = token;
    m_work = work;
    m_pairKey = pairKey;
    m_handle = m_search->startSearch(QStringLiteral("audiobooks"), QStringLiteral("all"),
        work.value("title").toString() + QLatin1Char(' ') + workAuthor(work), 30);
    if (m_handle.isEmpty()) {
        cancel(token);
        emit searchFinished(token, {}, tr("No Tankorent audiobook indexers are enabled."));
    }
}

void AudiobookTorrents::cancel(const QString& token)
{
    if (token.isEmpty() || m_token != token)
        return;
    auto handle = std::exchange(m_handle, QString());
    m_token.clear();
    m_work.clear();
    m_pairKey.clear();
    m_accum.clear();
    m_receivedResults = false;
    m_indexerFailed = false;
    if (!handle.isEmpty())
        m_search->cancelSearch(handle);
}

void AudiobookTorrents::onIndexerResults(const QString& handle, const QList<TorrentResult>& results)
{
    if (handle != m_handle)
        return;
    m_receivedResults = true;
    m_accum += results;
}

void AudiobookTorrents::onSearchDone(const QString& handle)
{
    if (handle != m_handle)
        return;
    const auto token = m_token;
    const auto rows = buildRows(m_work, m_pairKey, m_accum);
    QString error;
    if (!m_receivedResults)
        error = tr("Tankorent search is unavailable.");
    else if (rows.isEmpty() && m_indexerFailed)
        error = tr("Some Tankorent audiobook sources could not be reached.");
    m_handle.clear();
    cancel(token);
    emit searchFinished(token, rows, error);
}
