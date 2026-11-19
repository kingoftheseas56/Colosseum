#include "LibriVoxSearch.h"
#include "LibriVoxMedia.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>

namespace {
constexpr int requestGapMs = 3000;
constexpr int requestTimeoutMs = 25000;
constexpr int maxAttempts = 2;
constexpr int maxBooks = 20;
constexpr qsizetype maxBodyBytes = 4 * 1024 * 1024;

QString baseTitle(QString title)
{
    static const QRegularExpression versionSuffix(
        QStringLiteral("\\s*\\(version\\s+[0-9]+(?:,[^)]*)?\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return title.remove(versionSuffix).trimmed();
}

QString normalized(QString text)
{
    static const QRegularExpression punctuation(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return text.normalized(QString::NormalizationForm_C).toCaseFolded()
        .replace(QChar('&'), QStringLiteral(" and ")).replace(punctuation, QStringLiteral(" ")).trimmed();
}

QVariantList parseBooks(const QJsonArray& books, const QString& title, const QString& author)
{
    QVariantList rows;
    QSet<QString> projects;
    const QString titleKey = normalized(baseTitle(title));
    const QString authorKey = normalized(author);
    for (const auto bookValue : books) {
        const auto book = bookValue.toObject();
        const QString id = book.value(QStringLiteral("id")).toVariant().toString();
        const QString edition = book.value(QStringLiteral("title")).toString().trimmed().left(512);
        if (!LibriVoxMedia::validProjectId(id) || projects.contains(id)
            || normalized(baseTitle(edition)) != titleKey) continue;

        QStringList authors;
        for (const auto value : book.value(QStringLiteral("authors")).toArray()) {
            const auto person = value.toObject();
            const QString name = (person.value(QStringLiteral("first_name")).toString() + QChar(' ')
                + person.value(QStringLiteral("last_name")).toString()).simplified().left(160);
            if (!name.isEmpty()) authors.append(name);
        }
        const QString credit = authors.join(QStringLiteral(" & "));
        if (credit.isEmpty() || (!authorKey.isEmpty() && normalized(credit) != authorKey)) continue;

        const auto sections = book.value(QStringLiteral("sections")).toArray();
        if (sections.isEmpty() || sections.size() > LibriVoxMedia::maxChapters) continue;
        QMap<int, QVariantMap> ordered;
        QSet<QString> urls;
        QStringList narrators;
        QSet<QString> readerIds;
        for (const auto sectionValue : sections) {
            const auto section = sectionValue.toObject();
            const auto url = section.value(QStringLiteral("listen_url")).toString();
            bool ok = false;
            const int number = section.value(QStringLiteral("section_number")).toVariant().toInt(&ok);
            if (!ok || number < 1 || number > 10000 || ordered.contains(number) || urls.contains(url)
                || !LibriVoxMedia::validAudioUrl(QUrl(url, QUrl::StrictMode))) break;
            urls.insert(url);
            ordered.insert(number, {
                {QStringLiteral("number"), number},
                {QStringLiteral("title"), section.value(QStringLiteral("title")).toString().trimmed().left(180)},
                {QStringLiteral("url"), url}
            });
            for (const auto value : section.value(QStringLiteral("readers")).toArray()) {
                const auto reader = value.toObject();
                const QString name = reader.value(QStringLiteral("display_name")).toString().trimmed().left(160);
                const QString readerId = reader.value(QStringLiteral("reader_id")).toVariant().toString();
                const QString identity = readerId.isEmpty() ? name : readerId;
                if (name.isEmpty() || readerIds.contains(identity)) continue;
                readerIds.insert(identity);
                narrators.append(name);
            }
        }
        if (ordered.size() != sections.size()) continue;
        QVariantList chapters;
        chapters.reserve(ordered.size());
        for (const auto& chapter : ordered) chapters.append(chapter);
        projects.insert(id);
        rows.append(QVariantMap{
            {QStringLiteral("id"), id}, {QStringLiteral("title"), edition},
            {QStringLiteral("author"), credit}, {QStringLiteral("narrators"), narrators},
            {QStringLiteral("language"), book.value(QStringLiteral("language")).toString().left(64)},
            {QStringLiteral("chapters"), chapters}
        });
        if (rows.size() >= maxBooks) break;
    }
    return rows;
}
} // namespace

LibriVoxSearch::LibriVoxSearch(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), m_network(network)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LibriVoxSearch::request);
}

LibriVoxSearch::~LibriVoxSearch()
{
    cancel(m_token);
}

void LibriVoxSearch::search(const QString& token, const QString& title, const QString& author)
{
    if (token.isEmpty()) return;
    cancel(m_token);
    m_token = token;
    m_title = baseTitle(title.simplified());
    m_author = author;
    m_attempts = 0;
    if (m_title.isEmpty() || m_title.size() > 256 || m_title.startsWith(QChar('^'))) {
        complete({}, QStringLiteral("Invalid LibriVox title."));
        return;
    }
    schedule();
}

void LibriVoxSearch::cancel(const QString& token)
{
    if (m_token.isEmpty() || token != m_token) return;
    m_timer.stop();
    m_token.clear();
    m_body.clear();
    if (auto* reply = m_reply.data()) {
        m_reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void LibriVoxSearch::schedule()
{
    const auto delay = m_sinceRequest.isValid() ? std::max<qint64>(0, requestGapMs - m_sinceRequest.elapsed()) : 0;
    m_timer.start(static_cast<int>(delay));
}

void LibriVoxSearch::request()
{
    if (m_token.isEmpty()) return;
    if (!m_network || (m_enabledCheck && !m_enabledCheck())) {
        complete({}, QStringLiteral("LibriVox is unavailable."));
        return;
    }
    QUrl url(QStringLiteral("https://librivox.org/api/feed/audiobooks/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), QChar('^') + m_title);
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("extended"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(maxBooks));
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum audiobook search"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(requestTimeoutMs);
    m_body.clear();
    ++m_attempts;
    m_sinceRequest.start();
    auto* reply = m_network->get(req);
    m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_body += reply->readAll();
        if (m_body.size() > maxBodyBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, &LibriVoxSearch::finishRequest);
    QTimer::singleShot(requestTimeoutMs, reply, [reply] {
        if (!reply->isFinished()) reply->abort();
    });
}

void LibriVoxSearch::finishRequest()
{
    auto* reply = m_reply.data();
    if (!reply) return;
    m_reply.clear();
    const auto error = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error == QNetworkReply::NoError || status == 404) m_body += reply->readAll();
    reply->deleteLater();
    if (m_body.size() > maxBodyBytes) {
        complete({}, QStringLiteral("LibriVox search response was too large."));
        return;
    }
    if (m_enabledCheck && !m_enabledCheck()) {
        complete({}, QStringLiteral("LibriVox is disabled."));
        return;
    }
    if (m_attempts < maxAttempts && (status == 408 || status == 429 || status >= 500
        || (error != QNetworkReply::NoError && status < 400))) {
        schedule();
        return;
    }
    const auto response = QJsonDocument::fromJson(m_body).object();
    if (status == 404 && response.value(QStringLiteral("error")).toString()
        == QLatin1String("Audiobooks could not be found")) {
        complete({}, {});
        return;
    }
    if (error != QNetworkReply::NoError || status != 200) {
        complete({}, QStringLiteral("LibriVox search failed."));
        return;
    }
    const auto books = response.value(QStringLiteral("books"));
    if (!books.isArray()) {
        complete({}, QStringLiteral("LibriVox returned an invalid search response."));
        return;
    }
    complete(parseBooks(books.toArray(), m_title, m_author), {});
}

void LibriVoxSearch::complete(const QVariantList& rows, const QString& error)
{
    const QString token = m_token;
    cancel(token);
    emit searchFinished(token, rows, error);
}
