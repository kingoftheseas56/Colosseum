#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../CollectionStore.h"
#include "../../ProgressStore.h"

#include "../../engine/AudiobookDownloader.h"
#include "../../engine/BookDownloader.h"
#include "../../player/streamserver.h"
#include "../../reader2/Reader2Bridge.h"
#include "../../torrent/BookTorrents.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace {

const QString kFeed = QStringLiteral("detail.book");
const QByteArray kBrowserUa =
    QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                      "AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/120.0.0.0 Safari/537.36");

struct DetailState {
    QString id;
    QVariantMap book;
    QString pairKey;
    bool metadataStarted = false;
    bool metadataLoading = true;
    QString metadataError;
    bool editionsStarted = false;
    bool editionsLoading = true;
    QVariantList editions;
    bool torrentsStarted = false;
    bool torrentsLoading = true;
    QVariantList torrents;
    bool audiobooksStarted = false;
    bool audiobooksLoading = true;
    QVariantList audiobooks;
    QString audiobookActiveSlug;
    QString audiobookState;
    double audiobookReceived = 0;
    double audiobookTotal = 0;
    QString localPath;
    int readGeneration = 0;
    QString pendingReadTransport;
    QString pendingReadId;
    QString pendingReadBookIdentity;
    QString pendingReadState;
    double pendingReadReceived = 0;
    double pendingReadTotal = 0;
    QString readError;
    QVariantList readChoices;
    ActionRegistry::Completion pendingReadCompletion;
};

QHash<QString, DetailState> g_states;
QPointer<ColosseumWebBridge> g_bridge;
QPointer<BookDownloader> g_books;
QPointer<BookTorrents> g_torrents;
QPointer<AudiobookDownloader> g_audiobooks;
QString g_torrentOwner;

QVariantMap ok(const QVariant &result = {})
{
    QVariantMap answer{{QStringLiteral("ok"), true}};
    if (result.isValid())
        answer.insert(QStringLiteral("result"), result);
    return answer;
}

QVariantMap fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

void refresh(ColosseumWebBridge &bridge, const QString &id)
{
    if (bridge.detailActive(kFeed, id))
        bridge.updateDetail(kFeed, id, {});
}

// qml/BiblioApi.js:137-147.
QString stripHtml(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    text.replace(QRegularExpression(QStringLiteral("&#xa0;|&nbsp;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("&amp;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("&"));
    text.replace(QRegularExpression(QStringLiteral("&quot;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\""));
    text.replace(QRegularExpression(QStringLiteral("&#x27;|&#39;|&apos;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("&#x2014;|&mdash;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QString::fromUtf8("—"));
    text.replace(QRegularExpression(QStringLiteral("&#x2013;|&ndash;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QString::fromUtf8("–"));
    text.replace(QRegularExpression(QStringLiteral("&[#a-z0-9]+;"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

// qml/BiblioApi.js:151-179.
QString cleanBlurb(QString text)
{
    text = stripHtml(text);
    text.replace(QRegularExpression(QStringLiteral("\\*+")), QStringLiteral(" "));
    const QStringList noise = {
        QStringLiteral("(the\\s+)?(instant\\s+)?#?\\s*1?\\s*(international|new york times|usa today|sunday times|national|wall street journal)\\s+bestsell\\w*"),
        QStringLiteral("(instant\\s+)?#?\\s*1\\s+bestsell\\w*"),
        QStringLiteral("now a (major )?(motion picture|netflix|hbo|apple tv\\+?|prime video|major series|tv series|film)[^.!?]*"),
        QStringLiteral("soon to be[^.!?]*"),
        QStringLiteral("over [\\d,.]+\\s*(million|thousand)?\\s*copies sold[^.!?]*"),
        QStringLiteral("translated into[^.!?]*"),
        QStringLiteral("(a|an)?\\s*(reese'?s|oprah'?s|today show|good morning america|gma|jenna'?s)\\s*book club( pick)?"),
        QStringLiteral("(winner|finalist)\\s+(of|for)\\b[^.!?]*"),
        QStringLiteral("\\b\\d+\\s+best books?\\b[^.!?]*"),
        QStringLiteral("\\bhugo award[^.!?]*"),
        QStringLiteral("\\b(reader'?s?|readers?')\\s+(pick|favorite)\\b[^.!?]*"),
        QStringLiteral("\\bworldwide phenomenon\\b[^.!?]*"),
        QStringLiteral("\\bnational book award[^.!?]*"),
        QStringLiteral("named (a|one of)[^.!?]*best book[^.!?]*")
    };
    for (const QString &pattern : noise)
        text.replace(QRegularExpression(pattern,
                                        QRegularExpression::CaseInsensitiveOption),
                     QStringLiteral(" "));
    text.remove(QRegularExpression(QString::fromUtf8("^[\\s•\\-–—:#\"'.,!]+")));
    text.replace(QRegularExpression(QStringLiteral("\\s{2,}")), QStringLiteral(" "));
    return text.trimmed();
}

bool looksMarketing(const QString &text)
{
    if (QRegularExpression(
            QStringLiteral("bestsell|motion picture|book club|copies sold|award|netflix|oprah|reese|#\\s*1\\b"),
            QRegularExpression::CaseInsensitiveOption).match(text).hasMatch())
        return true;
    int upper = 0;
    int letters = 0;
    for (const QChar ch : text) {
        if (!ch.isLetter())
            continue;
        ++letters;
        if (ch.isUpper())
            ++upper;
    }
    return letters > 0 && static_cast<double>(upper) / letters > 0.5;
}

// qml/BiblioApi.js:186-208.
QString clampSynopsis(const QString &raw, int maximum)
{
    const QString text = raw.trimmed();
    if (text.size() <= maximum)
        return text;
    QString cut = text.left(maximum);
    const int end = std::max({cut.lastIndexOf(QStringLiteral(". ")),
                              cut.lastIndexOf(QStringLiteral("! ")),
                              cut.lastIndexOf(QStringLiteral("? "))});
    if (end > static_cast<int>(maximum * 0.55))
        return cut.left(end + 1).trimmed();
    const int space = cut.lastIndexOf(QLatin1Char(' '));
    return cut.left(space > 0 ? space : maximum).trimmed() + QString::fromUtf8("…");
}

QPair<QString, QString> splitTagline(const QString &description)
{
    const QString cleaned = cleanBlurb(description);
    if (cleaned.isEmpty())
        return {};
    const QRegularExpression re(
        QString::fromUtf8("^[^.!?:]{8,170}:\\s+([A-Za-z“\"'][^:;]{14,130}?[.!?])\\s+(.+)$"));
    const auto match = re.match(cleaned);
    if (match.hasMatch()) {
        QString hook = match.captured(1).trimmed();
        if (!looksMarketing(hook) && !hook.endsWith(QStringLiteral("..."))) {
            if (!hook.isEmpty())
                hook[0] = hook[0].toUpper();
            return {hook, match.captured(2).trimmed()};
        }
    }
    return {QString(), cleaned};
}

// qml/BiblioApi.js:210-223.
QString bigCover(QString url)
{
    if (url.isEmpty())
        return {};
    url.replace(QRegularExpression(QStringLiteral("/[0-9]+x[0-9]+bb\\.(png|jpg|jpeg)$"),
                                   QRegularExpression::CaseInsensitiveOption),
                QStringLiteral("/600x900bb.jpg"));
    return url;
}

QString genreLine(const QJsonArray &genres, const QString &author, const QString &year)
{
    QStringList parts;
    for (const QJsonValue &value : genres) {
        const QString genre = value.toString();
        if (genre.isEmpty() || genre == QLatin1String("Books"))
            continue;
        parts.append(genre);
        break;
    }
    if (!author.isEmpty())
        parts.append(author);
    if (!year.isEmpty())
        parts.append(year);
    return parts.join(QString::fromUtf8("  ·  "));
}

// qml/BiblioApi.js:269-285.
QString normalizedPairPart(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression(QStringLiteral("\\([^)]*\\)")), QStringLiteral(" "));
    text.replace(QRegularExpression(
                     QStringLiteral(":\\s*(a|an|the)\\s+(novel|memoir|story|thriller|mystery|romance|tale)\\b[^:]*$"),
                     QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\bunabridged\\b|\\babridged\\b"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.replace(QLatin1Char('&'), QStringLiteral(" and "));
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return text.simplified();
}

QString pairKey(const QVariantMap &book)
{
    return normalizedPairPart(book.value(QStringLiteral("title")).toString())
        + QLatin1Char('|')
        + normalizedPairPart(book.value(QStringLiteral("author")).toString());
}

// qml/BiblioApi.js:225-241.
QVariantMap fullBook(const QJsonObject &row, const QVariantMap &fallback)
{
    const QString description = row.value(QStringLiteral("description")).toString();
    const auto split = splitTagline(description);
    const QString year = row.value(QStringLiteral("releaseDate")).toString().left(4);
    QString title = row.value(QStringLiteral("trackName")).toString();
    if (title.isEmpty())
        title = row.value(QStringLiteral("trackCensoredName")).toString();
    if (title.isEmpty())
        title = fallback.value(QStringLiteral("title")).toString();
    const QString author = row.value(QStringLiteral("artistName")).toString();
    const QJsonArray rawGenres = row.value(QStringLiteral("genres")).toArray();
    QVariantList genres;
    for (const QJsonValue &value : rawGenres)
        if (value.toString() != QLatin1String("Books"))
            genres.append(value.toString());
    qlonglong numericId = row.value(QStringLiteral("trackId")).toVariant().toLongLong();
    if (numericId <= 0)
        numericId = row.value(QStringLiteral("collectionId")).toVariant().toLongLong();
    const QString appleId = numericId > 0
        ? QString::number(numericId) : fallback.value(QStringLiteral("id")).toString();
    QString cover = row.value(QStringLiteral("artworkUrl100")).toString();
    if (cover.isEmpty())
        cover = fallback.value(QStringLiteral("cover")).toString();
    return {
        {QStringLiteral("id"), appleId},
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("year"), year},
        {QStringLiteral("genres"), genres},
        {QStringLiteral("genreLine"), genreLine(rawGenres, author, year)},
        {QStringLiteral("tagline"), split.first},
        {QStringLiteral("synopsis"), clampSynopsis(
             split.second.isEmpty() ? stripHtml(description) : split.second, 400)},
        {QStringLiteral("cover"), bigCover(cover)},
        {QStringLiteral("rating"), row.value(QStringLiteral("averageUserRating")).toDouble()},
        {QStringLiteral("ratingCount"), row.value(QStringLiteral("userRatingCount")).toInt()}
    };
}

// qml/BiblioBook.qml:163-179.
QString fmtLabel(const QVariantMap &edition)
{
    const QString format = edition.value(QStringLiteral("format")).toString().toLower().trimmed();
    const QRegularExpression re(
        QStringLiteral("\\b(epub|pdf|mobi|azw3|azw|cbz|cbr|djvu|fb2|txt)\\b"));
    const auto match = re.match(format);
    if (match.hasMatch())
        return match.captured(1).toUpper();
    if (format.contains(QLatin1Char('/')) && format.size() <= 12)
        return format.toUpper();
    return edition.value(QStringLiteral("md5")).toString().isEmpty()
        ? QStringLiteral("WEB") : QStringLiteral("FILE");
}

QString editionMeta(const QVariantMap &edition)
{
    QStringList parts;
    for (const QString &field : {QStringLiteral("size"), QStringLiteral("source"),
                                 QStringLiteral("year"), QStringLiteral("language")}) {
        QString value = edition.value(field).toString();
        if (field == QLatin1String("source"))
            value = value.toUpper();
        if (!value.isEmpty())
            parts.append(value);
    }
    return parts.join(QString::fromUtf8("   ·   "));
}

using RawDone = std::function<void(bool, QByteArray)>;

void requestRaw(ColosseumWebBridge &bridge, const QUrl &url, int timeoutMs,
                const QByteArray &userAgent, RawDone done)
{
    auto *manager = new QNetworkAccessManager(&bridge);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = manager->get(request);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    auto settled = std::make_shared<bool>(false);
    QObject::connect(timer, &QTimer::timeout, reply,
        [reply, done, settled] {
            if (*settled)
                return;
            *settled = true;
            reply->abort();
            done(false, {});
        });
    QObject::connect(reply, &QNetworkReply::finished, &bridge,
        [reply, manager, timer, done, settled] {
            if (*settled) {
                reply->deleteLater();
                manager->deleteLater();
                return;
            }
            *settled = true;
            timer->stop();
            const bool success = reply->error() == QNetworkReply::NoError;
            const QByteArray body = success ? reply->readAll() : QByteArray{};
            reply->deleteLater();
            manager->deleteLater();
            done(success, body);
        });
    timer->start(timeoutMs);
}

void requestLibGen(ColosseumWebBridge &bridge, const QUrl &url,
                   int attempt, RawDone done)
{
    // qml/BiblioApi.js:333-358: 25 second timeout and one fresh retry.
    requestRaw(bridge, url, 25000, kBrowserUa,
        [&bridge, url, attempt, done](bool success, QByteArray body) {
            if (!success && attempt < 1) {
                requestLibGen(bridge, url, attempt + 1, done);
                return;
            }
            done(success, std::move(body));
        });
}

QString htmlDecode(QString text)
{
    // qml/AbbApi.js:23-29.
    const QList<QPair<QString, QString>> replacements = {
        {QStringLiteral("&amp;"), QStringLiteral("&")},
        {QStringLiteral("&quot;"), QStringLiteral("\"")},
        {QStringLiteral("&apos;"), QStringLiteral("'")},
        {QStringLiteral("&#039;"), QStringLiteral("'")},
        {QStringLiteral("&#8211;"), QStringLiteral("-")},
        {QStringLiteral("&#8212;"), QStringLiteral("-")},
        {QStringLiteral("&#8230;"), QStringLiteral("...")},
        {QStringLiteral("&nbsp;"), QStringLiteral(" ")},
        {QStringLiteral("&lt;"), QStringLiteral("<")},
        {QStringLiteral("&gt;"), QStringLiteral(">")}
    };
    for (const auto &replacement : replacements)
        text.replace(replacement.first, replacement.second);
    return text.trimmed();
}

// qml/AbbApi.js:31-76. Exact post class excludes ABB honeypot blocks.
QVariantList parseAbbSearch(const QString &html)
{
    QList<int> starts;
    auto posts = QRegularExpression(QStringLiteral("<div class=\"post\">")).globalMatch(html);
    while (posts.hasNext())
        starts.append(posts.next().capturedStart());
    QVariantList out;
    if (starts.isEmpty())
        return out;

    const QRegularExpression titleRe(
        QStringLiteral("<h2><a href=\"([^\"]+)\"[^>]*>([^<]+)</a></h2>"));
    const QRegularExpression coverRe(
        QStringLiteral("<img src=\"([^\"]+)\"[^>]*width=\"250\""));
    const QRegularExpression langRe(
        QStringLiteral("Language:\\s*([^<]+?)(?:<span|<br)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression formatRe(
        QStringLiteral("Format:\\s*<span[^>]*>([^<]+)</span>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression sizeRe(
        QStringLiteral("File Size:\\s*<span[^>]*>([^<]+)</span>\\s*([GMK]?Bs?)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression postedRe(
        QStringLiteral("Posted:\\s*([^<]+?)<br"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression slugRe(QStringLiteral("/abss/([^/]+)/?"));

    for (int i = 0; i < starts.size(); ++i) {
        const int end = i + 1 < starts.size() ? starts.at(i + 1) : html.size();
        const QString block = html.mid(starts.at(i), end - starts.at(i));
        const auto titleMatch = titleRe.match(block);
        if (!titleMatch.hasMatch())
            continue;
        const QString href = titleMatch.captured(1).trimmed();
        const QString detailUrl = href.startsWith(QLatin1String("http"))
            ? href : QStringLiteral("https://audiobookbay.lu") + href;
        const auto slugMatch = slugRe.match(detailUrl);
        if (!slugMatch.hasMatch())
            continue;

        QString wholeTitle = htmlDecode(titleMatch.captured(2));
        QString title = wholeTitle;
        QString author;
        const int dash = wholeTitle.lastIndexOf(QStringLiteral(" - "));
        if (dash > 0 && dash < wholeTitle.size() - 3) {
            const QString after = wholeTitle.mid(dash + 3).trimmed();
            if (!after.isEmpty() && after.size() <= 60
                && !after.contains(QLatin1Char('/'))
                && !after.contains(QLatin1Char('['))
                && !after.front().isDigit()) {
                author = after;
                title = wholeTitle.left(dash).trimmed();
            }
        }

        const auto coverMatch = coverRe.match(block);
        const auto languageMatch = langRe.match(block);
        const auto formatMatch = formatRe.match(block);
        const auto sizeMatch = sizeRe.match(block);
        const auto postedMatch = postedRe.match(block);
        out.append(QVariantMap{
            {QStringLiteral("slug"), slugMatch.captured(1)},
            {QStringLiteral("detailUrl"), detailUrl},
            {QStringLiteral("title"), title},
            {QStringLiteral("author"), author},
            {QStringLiteral("cover"),
             coverMatch.hasMatch() ? coverMatch.captured(1).trimmed() : QString()},
            {QStringLiteral("language"),
             languageMatch.hasMatch() ? htmlDecode(languageMatch.captured(1)) : QString()},
            {QStringLiteral("format"),
             formatMatch.hasMatch() ? formatMatch.captured(1).trimmed() : QString()},
            {QStringLiteral("size"),
             sizeMatch.hasMatch()
                 ? sizeMatch.captured(1).trimmed() + QLatin1Char(' ')
                     + sizeMatch.captured(2).trimmed()
                 : QString()},
            {QStringLiteral("posted"),
             postedMatch.hasMatch() ? postedMatch.captured(1).trimmed() : QString()}
        });
    }
    return out;
}

// qml/AbbApi.js:117-148.
QVariantList relevantAbbRows(const QVariantList &rows,
                             const QString &title, const QString &author)
{
    static const QSet<QString> stop{
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"),
        QStringLiteral("with"), QStringLiteral("book"), QStringLiteral("series"),
        QStringLiteral("audiobook"), QStringLiteral("unabridged"),
        QStringLiteral("novel"), QStringLiteral("volume")
    };
    QStringList words;
    QStringList raw;
    auto tokens = QRegularExpression(QStringLiteral("[a-z0-9]{3,}"))
        .globalMatch((title + QLatin1Char(' ') + author).toLower());
    while (tokens.hasNext())
        raw.append(tokens.next().captured());
    for (const QString &word : raw)
        if (!stop.contains(word))
            words.append(word);
    if (words.isEmpty())
        words = raw;
    if (words.isEmpty())
        return rows;

    QVariantList out;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString hay = (row.value(QStringLiteral("title")).toString()
                             + QLatin1Char(' ')
                             + row.value(QStringLiteral("author")).toString()).toLower();
        for (const QString &word : words) {
            if (!hay.contains(word))
                continue;
            out.append(row);
            break;
        }
    }
    return out;
}

// qml/AbbApi.js:78-84.
QString parseAbbInfoHash(const QString &html)
{
    const QRegularExpression re(
        QStringLiteral("<td>Info Hash:</td>\\s*<td[^>]*>\\s*([0-9a-fA-F]{40})\\s*</td>"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(html);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

// qml/BiblioApi.js:360-429.
QVariantList parseLibGen(const QString &html)
{
    const QRegularExpression tableRe(
        QStringLiteral("<table[^>]*id=\"tablelibgen\"[^>]*>([\\s\\S]*?)</table>"),
        QRegularExpression::CaseInsensitiveOption);
    const auto tableMatch = tableRe.match(html);
    if (!tableMatch.hasMatch())
        return {};

    QString body = tableMatch.captured(1);
    body.replace(QRegularExpression(QStringLiteral("<(br|wbr|hr)\\s*/?>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    const QRegularExpression rowRe(
        QStringLiteral("<tr[^>]*>[\\s\\S]*?</tr>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression md5Re(
        QStringLiteral("(?:ads|get)\\.php\\?[^\"']*md5=([a-fA-F0-9]{32})"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression cellRe(
        QStringLiteral("<td[^>]*>([\\s\\S]*?)</td>"),
        QRegularExpression::CaseInsensitiveOption);

    QVariantList raw;
    auto rowIt = rowRe.globalMatch(body);
    while (rowIt.hasNext() && raw.size() < 40) {
        const QString rowHtml = rowIt.next().captured();
        const auto md5Match = md5Re.match(rowHtml);
        if (!md5Match.hasMatch())
            continue;
        QStringList cells;
        auto cellIt = cellRe.globalMatch(rowHtml);
        while (cellIt.hasNext())
            cells.append(cellIt.next().captured(1));
        if (cells.size() < 9)
            continue;
        raw.append(QVariantMap{
            {QStringLiteral("md5"), md5Match.captured(1)},
            {QStringLiteral("format"), stripHtml(cells.at(7)).toLower()},
            {QStringLiteral("size"), stripHtml(cells.at(6))},
            {QStringLiteral("year"), stripHtml(cells.at(3))},
            {QStringLiteral("language"), stripHtml(cells.at(4))},
            {QStringLiteral("source"), QStringLiteral("libgen")},
            {QStringLiteral("detailUrl"),
             QStringLiteral("https://libgen.li/ads.php?md5=") + md5Match.captured(1)},
            {QStringLiteral("best"), false}
        });
    }

    auto tier = [](const QVariantMap &edition) {
        const QString format =
            edition.value(QStringLiteral("format")).toString().toLower().trimmed();
        if (format == QLatin1String("epub"))
            return 3;
        if (format == QLatin1String("mobi") || format == QLatin1String("fb2")
            || format == QLatin1String("azw3"))
            return 2;
        if (format == QLatin1String("pdf"))
            return 1;
        return 0;
    };

    int top = 0;
    for (const QVariant &value : raw)
        top = std::max(top, tier(value.toMap()));
    QVariantList shown;
    if (top <= 0)
        return shown;
    for (const QVariant &value : raw) {
        if (tier(value.toMap()) != top)
            continue;
        QVariantMap edition = value.toMap();
        edition.insert(QStringLiteral("best"), shown.isEmpty());
        shown.append(edition);
        if (shown.size() >= 12)
            break;
    }
    return shown;
}


QVariantMap bookStatus(const QString &id)
{
    return g_books && !id.isEmpty()
        ? g_books->statusOf(id)
        : QVariantMap{{QStringLiteral("state"), QStringLiteral("none")}};
}

QVariantMap torrentStatus(const QString &id)
{
    return g_torrents && !id.isEmpty()
        ? g_torrents->statusOf(id)
        : QVariantMap{{QStringLiteral("state"), QStringLiteral("none")}};
}

bool inFlight(const QString &state)
{
    return state == QLatin1String("resolving")
        || state == QLatin1String("queued")
        || state == QLatin1String("downloading");
}

// qml/BiblioBook.qml:300-302.
QVariantMap candidate(const QString &transport, const QString &id,
                      const QString &label, const QString &meta,
                      const QVariantMap &payload)
{
    return {
        {QStringLiteral("transport"), transport},
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("meta"), meta},
        {QStringLiteral("payload"), payload}
    };
}

// qml/BiblioBook.qml:263-286.
void clearReadIntent(DetailState &state)
{
    state.pendingReadTransport.clear();
    state.pendingReadId.clear();
    state.pendingReadBookIdentity.clear();
    state.pendingReadState.clear();
    state.pendingReadReceived = 0;
    state.pendingReadTotal = 0;
    state.readChoices.clear();
}

void invalidateReadIntent(DetailState &state, const QString &reason = QString())
{
    ++state.readGeneration;
    auto completion = std::move(state.pendingReadCompletion);
    clearReadIntent(state);
    if (completion) {
        completion(ok(QVariantMap{{QStringLiteral("cancelled"), true},
                                      {QStringLiteral("reason"), reason}}));
    }
}

// qml/BiblioBook.qml:564-601.
QString localBookByIdentity(BookDownloader *books, const DetailState &state)
{
    if (!books)
        return {};
    for (const QVariant &value : state.editions) {
        const QString md5 = value.toMap().value(QStringLiteral("md5")).toString();
        if (md5.isEmpty())
            continue;
        const QString path = books->localBook(md5);
        if (!path.isEmpty())
            return path;
    }

    if (state.pairKey.isEmpty() || state.pairKey == QLatin1String("|"))
        return {};
    for (const QVariant &value : books->downloadedBooks()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("missing")).toBool())
            continue;
        const QString path = row.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        const QVariantMap landed{
            {QStringLiteral("title"), row.value(QStringLiteral("title"))},
            {QStringLiteral("author"), row.value(QStringLiteral("author"))}
        };
        if (pairKey(landed) == state.pairKey)
            return path;
    }
    return {};
}

void refreshLocal(DetailState &state)
{
    QString path = localBookByIdentity(g_books, state);
    if (path.isEmpty() && g_torrents) {
        for (const QVariant &value : state.torrents) {
            const QString hash =
                value.toMap().value(QStringLiteral("infoHash")).toString().toLower();
            if (hash.isEmpty())
                continue;
            path = g_torrents->localFile(hash);
            if (!path.isEmpty())
                break;
        }
    }
    state.localPath = path;
}

// qml/BiblioBook.qml:304-349.
QVariantList activeReadCandidates(const DetailState &state)
{
    QVariantList out;
    if (g_books) {
        for (const QVariant &value : state.editions) {
            const QVariantMap edition = value.toMap();
            const QString md5 = edition.value(QStringLiteral("md5")).toString();
            if (inFlight(bookStatus(md5).value(QStringLiteral("state")).toString()))
                out.append(candidate(QStringLiteral("books"), md5,
                                     fmtLabel(edition), editionMeta(edition), edition));
        }
    }
    if (g_torrents) {
        for (const QVariant &value : state.torrents) {
            const QVariantMap row = value.toMap();
            const QString hash =
                row.value(QStringLiteral("infoHash")).toString().toLower();
            if (inFlight(torrentStatus(hash).value(QStringLiteral("state")).toString()))
                out.append(candidate(QStringLiteral("torrent"), hash,
                                     row.value(QStringLiteral("format"),
                                               QStringLiteral("EBOOK")).toString(),
                                     row.value(QStringLiteral("title"),
                                               QStringLiteral("Torrent")).toString(),
                                     row));
        }
    }
    return out;
}

QVariantList trackedReadChoices(const DetailState &state)
{
    QVariantList out;
    if (g_books) {
        for (const QVariant &value : state.editions) {
            const QVariantMap edition = value.toMap();
            const QString md5 = edition.value(QStringLiteral("md5")).toString();
            if (!md5.isEmpty())
                out.append(candidate(QStringLiteral("books"), md5,
                                     fmtLabel(edition), editionMeta(edition), edition));
        }
    }
    if (g_torrents) {
        for (int i = 0; i < state.torrents.size() && i < 5; ++i) {
            const QVariantMap row = state.torrents.at(i).toMap();
            const QString hash =
                row.value(QStringLiteral("infoHash")).toString().toLower();
            if (!hash.isEmpty())
                out.append(candidate(QStringLiteral("torrent"), hash,
                                     row.value(QStringLiteral("format"),
                                               QStringLiteral("EBOOK")).toString(),
                                     row.value(QStringLiteral("title"),
                                               QStringLiteral("Torrent")).toString(),
                                     row));
        }
    }
    return out;
}

// qml/BiblioBook.qml:192-216.
QVariantMap collectionItem(const DetailState &state)
{
    const QVariantMap ref{
        {QStringLiteral("id"), state.pairKey},
        {QStringLiteral("author"), state.book.value(QStringLiteral("author"))},
        {QStringLiteral("payload"),
         QVariantMap{{QStringLiteral("book"), state.book}}}
    };
    return {
        {QStringLiteral("world"), QStringLiteral("Biblio")},
        {QStringLiteral("kind"), QStringLiteral("book")},
        {QStringLiteral("title"), state.book.value(QStringLiteral("title"))},
        {QStringLiteral("cover"), state.book.value(QStringLiteral("cover"))},
        {QStringLiteral("ref"), ref}
    };
}

void collectBook(ColosseumWebBridge &bridge, const DetailState &state)
{
    bridge.act(QStringLiteral("collection.add"),
               {{QStringLiteral("item"), collectionItem(state)}});
}

void clearExistingCopies(DetailState &state)
{
    if (g_books) {
        for (const QVariant &value : state.editions) {
            const QString md5 =
                value.toMap().value(QStringLiteral("md5")).toString();
            if (!md5.isEmpty() && g_books->isDownloaded(md5))
                g_books->deleteBook(md5);
        }
    }
    if (g_torrents) {
        for (const QVariant &value : state.torrents) {
            const QString hash =
                value.toMap().value(QStringLiteral("infoHash")).toString().toLower();
            if (!hash.isEmpty() && g_torrents->isDownloaded(hash))
                g_torrents->deleteDownload(hash);
        }
    }
}

// qml/BiblioBook.qml:415-428 and qml/Main.qml:2646-2654.
void delegateReader(ColosseumWebBridge &bridge, DetailState &state,
                    const QString &path, ActionRegistry::Completion completion)
{
    const QVariantMap ref{
        {QStringLiteral("kind"), QStringLiteral("book")},
        {QStringLiteral("id"), state.book.value(QStringLiteral("id"))},
        {QStringLiteral("continueGroupKey"),
         QStringLiteral("book:") + state.pairKey},
        {QStringLiteral("resume"),
         QVariantMap{{QStringLiteral("path"), path},
                     {QStringLiteral("book"), state.book}}}
    };
    const QVariantMap item{
        {QStringLiteral("world"), QStringLiteral("Biblio")},
        {QStringLiteral("kind"), QStringLiteral("book")},
        {QStringLiteral("title"), state.book.value(QStringLiteral("title"))},
        {QStringLiteral("cover"), state.book.value(QStringLiteral("cover"))},
        {QStringLiteral("ref"), ref}
    };
    bridge.delegateAction(
        QStringLiteral("open"),
        {{QStringLiteral("intent"), QStringLiteral("resume")},
         {QStringLiteral("item"), item}},
        [completion = std::move(completion)](const QVariantMap &answer) mutable {
            if (completion)
                completion(answer);
        });
}

// qml/BiblioBook.qml:430-436.
void failPendingRead(ColosseumWebBridge &bridge, DetailState &state,
                     const QString &transport, const QString &id,
                     const QString &reason)
{
    if (state.pendingReadTransport != transport
        || state.pendingReadId != id
        || state.pendingReadBookIdentity != state.pairKey)
        return;
    const QString message = reason.isEmpty()
        ? QStringLiteral("Read failed · try again")
        : QStringLiteral("Read failed · ") + reason;
    state.readError = message;
    auto completion = std::move(state.pendingReadCompletion);
    ++state.readGeneration;
    clearReadIntent(state);
    refresh(bridge, state.id);
    if (completion)
        completion(fail(message));
}

// qml/BiblioBook.qml:415-428.
void finishPendingRead(ColosseumWebBridge &bridge, DetailState &state,
                       const QString &transport, const QString &id)
{
    if (state.pendingReadTransport != transport
        || state.pendingReadId != id
        || state.pendingReadBookIdentity != state.pairKey)
        return;
    if (!bridge.detailActive(kFeed, state.id)) {
        auto completion = std::move(state.pendingReadCompletion);
        ++state.readGeneration;
        clearReadIntent(state);
        if (completion)
            completion(fail(QStringLiteral("This book page is no longer open.")));
        return;
    }

    const QVariantMap status = transport == QLatin1String("books")
        ? bookStatus(id) : torrentStatus(id);
    if (status.value(QStringLiteral("state")).toString()
        != QLatin1String("done"))
        return;

    const QString path = transport == QLatin1String("books")
        ? (g_books ? g_books->localBook(id) : QString())
        : (g_torrents ? g_torrents->localFile(id) : QString());
    if (path.isEmpty())
        return;

    state.localPath = path;
    state.readError.clear();
    auto completion = std::move(state.pendingReadCompletion);
    clearReadIntent(state);
    refresh(bridge, state.id);
    delegateReader(bridge, state, path, std::move(completion));
}

// qml/BiblioBook.qml:369-403.
bool targetReadCandidate(ColosseumWebBridge &bridge, DetailState &state,
                         const QVariantMap &pick)
{
    QString transport = pick.value(QStringLiteral("transport")).toString();
    QString id = pick.value(QStringLiteral("id")).toString();
    if (transport == QLatin1String("torrent"))
        id = id.toLower();
    if (id.isEmpty())
        return false;

    const bool booksTransport = transport == QLatin1String("books");
    if ((booksTransport && !g_books)
        || (!booksTransport && transport == QLatin1String("torrent") && !g_torrents))
        return false;

    const QVariantMap status = booksTransport ? bookStatus(id) : torrentStatus(id);
    const QString stateName =
        status.value(QStringLiteral("state"), QStringLiteral("none")).toString();
    state.pendingReadTransport = transport;
    state.pendingReadId = id;
    state.pendingReadState = stateName;
    state.pendingReadReceived = status.value(QStringLiteral("received")).toDouble();
    state.pendingReadTotal = status.value(QStringLiteral("total")).toDouble();
    state.readChoices.clear();

    if (stateName == QLatin1String("done")) {
        finishPendingRead(bridge, state, transport, id);
        return true;
    }
    if (inFlight(stateName)) {
        refresh(bridge, state.id);
        return true;
    }

    clearExistingCopies(state);
    collectBook(bridge, state);
    if (booksTransport) {
        const QVariantMap edition =
            pick.value(QStringLiteral("payload")).toMap();
        const QString format =
            edition.value(QStringLiteral("format"),
                          QStringLiteral("epub")).toString();
        const QString fileName =
            state.book.value(QStringLiteral("title"),
                             QStringLiteral("book")).toString()
            + QLatin1Char('.') + format;
        g_books->downloadBook(
            id, fileName,
            state.book.value(QStringLiteral("title")).toString(),
            0,
            state.book.value(QStringLiteral("author")).toString());
    } else if (transport == QLatin1String("torrent")) {
        g_torrents->download(
            id,
            state.book.value(QStringLiteral("title")).toString(),
            state.book.value(QStringLiteral("author")).toString());
    } else {
        return false;
    }

    const QVariantMap fresh =
        booksTransport ? bookStatus(id) : torrentStatus(id);
    state.pendingReadState =
        fresh.value(QStringLiteral("state"),
                    QStringLiteral("resolving")).toString();
    state.pendingReadReceived =
        fresh.value(QStringLiteral("received")).toDouble();
    state.pendingReadTotal =
        fresh.value(QStringLiteral("total")).toDouble();
    refresh(bridge, state.id);
    return true;
}

// qml/BiblioBook.qml:437-498.
void resolvePendingLookup(ColosseumWebBridge &bridge, DetailState &state)
{
    if (state.pendingReadTransport != QLatin1String("lookup")
        || state.pendingReadBookIdentity != state.pairKey)
        return;

    refreshLocal(state);
    if (!state.localPath.isEmpty()) {
        auto completion = std::move(state.pendingReadCompletion);
        clearReadIntent(state);
        state.readError.clear();
        refresh(bridge, state.id);
        delegateReader(bridge, state, state.localPath, std::move(completion));
        return;
    }

    const QVariantList active = activeReadCandidates(state);
    if (active.size() == 1) {
        targetReadCandidate(bridge, state, active.first().toMap());
        return;
    }
    if (active.size() > 1) {
        state.pendingReadTransport = QStringLiteral("choice");
        state.pendingReadState = QStringLiteral("choice");
        state.readChoices = active;
        refresh(bridge, state.id);
        return;
    }

    if (state.editionsLoading || state.torrentsLoading) {
        state.pendingReadState = QStringLiteral("finding");
        refresh(bridge, state.id);
        return;
    }

    QVariantMap best;
    for (const QVariant &value : state.editions) {
        if (value.toMap().value(QStringLiteral("best")).toBool()) {
            best = value.toMap();
            break;
        }
    }
    if (best.isEmpty() && !state.editions.isEmpty())
        best = state.editions.first().toMap();

    if (!best.isEmpty()
        && !best.value(QStringLiteral("md5")).toString().isEmpty()
        && g_books) {
        targetReadCandidate(
            bridge, state,
            candidate(QStringLiteral("books"),
                      best.value(QStringLiteral("md5")).toString(),
                      fmtLabel(best), editionMeta(best), best));
        return;
    }

    if (!best.isEmpty()) {
        const QVariantList choices = trackedReadChoices(state);
        if (!choices.isEmpty()) {
            state.pendingReadTransport = QStringLiteral("choice");
            state.pendingReadState = QStringLiteral("choice");
            state.readChoices = choices;
            refresh(bridge, state.id);
            return;
        }
        state.readError =
            QStringLiteral("This book only has external editions right now · use Editions");
        auto completion = std::move(state.pendingReadCompletion);
        ++state.readGeneration;
        clearReadIntent(state);
        refresh(bridge, state.id);
        if (completion)
            completion(fail(state.readError));
        return;
    }

    if (g_torrents && !state.torrents.isEmpty()) {
        const QVariantMap row = state.torrents.first().toMap();
        targetReadCandidate(
            bridge, state,
            candidate(QStringLiteral("torrent"),
                      row.value(QStringLiteral("infoHash")).toString().toLower(),
                      row.value(QStringLiteral("format"),
                                QStringLiteral("EBOOK")).toString(),
                      row.value(QStringLiteral("title"),
                                QStringLiteral("Torrent")).toString(),
                      row));
        return;
    }

    const QVariantList fallback = trackedReadChoices(state);
    if (!fallback.isEmpty()) {
        state.pendingReadTransport = QStringLiteral("choice");
        state.pendingReadState = QStringLiteral("choice");
        state.readChoices = fallback;
        refresh(bridge, state.id);
        return;
    }

    state.readError = QStringLiteral("No tracked readable source yet");
    auto completion = std::move(state.pendingReadCompletion);
    ++state.readGeneration;
    clearReadIntent(state);
    refresh(bridge, state.id);
    if (completion)
        completion(fail(state.readError));
}

void bindServices(ColosseumWebBridge &bridge)
{
    g_bridge = &bridge;

    auto *books =
        qobject_cast<BookDownloader *>(bridge.service(QStringLiteral("Books")));
    if (books && books != g_books) {
        g_books = books;
        QObject::connect(
            books, &BookDownloader::resolving, &bridge,
            [&bridge](const QString &md5) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pendingReadTransport != QLatin1String("books")
                        || it->pendingReadId != md5)
                        continue;
                    it->pendingReadState = QStringLiteral("resolving");
                    it->pendingReadReceived = 0;
                    it->pendingReadTotal = 0;
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            books, &BookDownloader::progress, &bridge,
            [&bridge](const QString &md5, double received, double total) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pendingReadTransport != QLatin1String("books")
                        || it->pendingReadId != md5)
                        continue;
                    it->pendingReadState = QStringLiteral("downloading");
                    it->pendingReadReceived = received;
                    it->pendingReadTotal = total;
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            books, &BookDownloader::finished, &bridge,
            [&bridge](const QString &md5, const QString &) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    refreshLocal(*it);
                    if (it->pendingReadTransport == QLatin1String("books")
                        && it->pendingReadId == md5)
                        finishPendingRead(
                            bridge, *it, QStringLiteral("books"), md5);
                    else
                        refresh(bridge, it.key());
                }
            });
        QObject::connect(
            books, &BookDownloader::failed, &bridge,
            [&bridge](const QString &md5, const QString &reason) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it)
                    failPendingRead(
                        bridge, *it, QStringLiteral("books"), md5, reason);
            });
        QObject::connect(
            books, &BookDownloader::removed, &bridge,
            [&bridge](const QString &md5) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    refreshLocal(*it);
                    if (it->pendingReadTransport == QLatin1String("books")
                        && it->pendingReadId == md5)
                        failPendingRead(
                            bridge, *it, QStringLiteral("books"), md5,
                            QStringLiteral("Edition removed."));
                    else
                        refresh(bridge, it.key());
                }
            });
    }

    auto *torrents =
        qobject_cast<BookTorrents *>(bridge.service(QStringLiteral("BookTorrents")));
    if (torrents && torrents != g_torrents) {
        g_torrents = torrents;
        QObject::connect(
            torrents, &BookTorrents::resultsReady, &bridge,
            [&bridge](const QVariantList &rows) {
                auto it = g_states.find(g_torrentOwner);
                if (it == g_states.end())
                    return;
                it->torrents = rows;
                it->torrentsLoading = false;
                refreshLocal(*it);
                resolvePendingLookup(bridge, *it);
                refresh(bridge, it.key());
            });
        QObject::connect(
            torrents, &BookTorrents::searchFinished, &bridge,
            [&bridge] {
                auto it = g_states.find(g_torrentOwner);
                if (it == g_states.end())
                    return;
                it->torrentsLoading = false;
                resolvePendingLookup(bridge, *it);
                refresh(bridge, it.key());
            });
        QObject::connect(
            torrents, &BookTorrents::resolving, &bridge,
            [&bridge](const QString &hash) {
                const QString id = hash.toLower();
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pendingReadTransport != QLatin1String("torrent")
                        || it->pendingReadId != id)
                        continue;
                    it->pendingReadState = QStringLiteral("resolving");
                    it->pendingReadReceived = 0;
                    it->pendingReadTotal = 0;
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            torrents, &BookTorrents::progress, &bridge,
            [&bridge](const QString &hash, double received, double total) {
                const QString id = hash.toLower();
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pendingReadTransport != QLatin1String("torrent")
                        || it->pendingReadId != id)
                        continue;
                    it->pendingReadState = QStringLiteral("downloading");
                    it->pendingReadReceived = received;
                    it->pendingReadTotal = total;
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            torrents, &BookTorrents::finished, &bridge,
            [&bridge](const QString &hash, const QString &) {
                const QString id = hash.toLower();
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    refreshLocal(*it);
                    if (it->pendingReadTransport == QLatin1String("torrent")
                        && it->pendingReadId == id)
                        finishPendingRead(
                            bridge, *it, QStringLiteral("torrent"), id);
                    else
                        refresh(bridge, it.key());
                }
            });
        QObject::connect(
            torrents, &BookTorrents::failed, &bridge,
            [&bridge](const QString &hash, const QString &reason) {
                const QString id = hash.toLower();
                for (auto it = g_states.begin(); it != g_states.end(); ++it)
                    failPendingRead(
                        bridge, *it, QStringLiteral("torrent"), id, reason);
            });
        QObject::connect(
            torrents, &BookTorrents::removed, &bridge,
            [&bridge](const QString &hash) {
                const QString id = hash.toLower();
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    refreshLocal(*it);
                    if (it->pendingReadTransport == QLatin1String("torrent")
                        && it->pendingReadId == id)
                        failPendingRead(
                            bridge, *it, QStringLiteral("torrent"), id,
                            QStringLiteral("Edition removed."));
                    else
                        refresh(bridge, it.key());
                }
            });
    }

    auto *audiobooks =
        qobject_cast<AudiobookDownloader *>(
            bridge.service(QStringLiteral("Audiobooks")));
    if (audiobooks && audiobooks != g_audiobooks) {
        g_audiobooks = audiobooks;
        QObject::connect(
            audiobooks, &AudiobookDownloader::resolving, &bridge,
            [&bridge](const QString &key) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pairKey != key)
                        continue;
                    it->audiobookState = QStringLiteral("resolving");
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            audiobooks, &AudiobookDownloader::progress, &bridge,
            [&bridge](const QString &key, double received, double total) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pairKey != key)
                        continue;
                    it->audiobookState = QStringLiteral("downloading");
                    it->audiobookReceived = received;
                    it->audiobookTotal = total;
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            audiobooks, &AudiobookDownloader::finished, &bridge,
            [&bridge](const QString &key, const QString &) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pairKey != key)
                        continue;
                    it->audiobookState = QStringLiteral("done");
                    refresh(bridge, it.key());
                }
            });
        QObject::connect(
            audiobooks, &AudiobookDownloader::failed, &bridge,
            [&bridge](const QString &key, const QString &) {
                for (auto it = g_states.begin(); it != g_states.end(); ++it) {
                    if (it->pairKey != key)
                        continue;
                    it->audiobookState = QStringLiteral("failed");
                    refresh(bridge, it.key());
                }
            });
    }
}

// qml/BiblioBook.qml:104-134.
void startLibGen(ColosseumWebBridge &bridge, DetailState &state)
{
    if (state.editionsStarted)
        return;
    state.editionsStarted = true;
    state.editionsLoading = true;
    const QString id = state.id;
    const QString term =
        (state.book.value(QStringLiteral("title")).toString()
         + QLatin1Char(' ')
         + state.book.value(QStringLiteral("author")).toString()).trimmed();
    if (term.isEmpty()) {
        state.editionsLoading = false;
        return;
    }

    QUrl url(QStringLiteral("https://libgen.li/index.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("req"), term);
    query.addQueryItem(QStringLiteral("topics[]"), QStringLiteral("l"));
    query.addQueryItem(QStringLiteral("topics[]"), QStringLiteral("f"));
    query.addQueryItem(
        QStringLiteral("nc"),
        QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    requestLibGen(
        bridge, url, 0,
        [&bridge, id](bool success, QByteArray body) {
            auto it = g_states.find(id);
            if (it == g_states.end())
                return;
            it->editions = success
                ? parseLibGen(QString::fromUtf8(body))
                : QVariantList{};
            it->editionsLoading = false;
            refreshLocal(*it);
            resolvePendingLookup(bridge, *it);
            refresh(bridge, id);
        });
}

// qml/BiblioBook.qml:116-132.
void prewarmTopAudiobook(ColosseumWebBridge &bridge, const QString &id)
{
    auto it = g_states.find(id);
    if (it == g_states.end()
        || it->audiobooks.isEmpty()
        || !g_audiobooks
        || g_audiobooks->isDownloaded(it->pairKey))
        return;

    const QString slug =
        it->audiobooks.first().toMap()
            .value(QStringLiteral("slug")).toString();
    if (slug.isEmpty())
        return;

    const QUrl url(
        QStringLiteral("https://audiobookbay.lu/abss/")
        + slug + QLatin1Char('/'));
    requestRaw(
        bridge, url, 15000, kBrowserUa,
        [&bridge](bool success, QByteArray body) {
            if (!success)
                return;
            const QString hash =
                parseAbbInfoHash(QString::fromUtf8(body));
            if (hash.isEmpty())
                return;
            if (auto *stream =
                    qobject_cast<StreamServer *>(
                        bridge.service(QStringLiteral("Stream"))))
                stream->prefetch(hash, 0);
        });
}

void startAudiobooks(ColosseumWebBridge &bridge, DetailState &state)
{
    if (state.audiobooksStarted)
        return;
    state.audiobooksStarted = true;
    state.audiobooksLoading = true;
    const QString id = state.id;
    const QString title =
        state.book.value(QStringLiteral("title")).toString();
    const QString author =
        state.book.value(QStringLiteral("author")).toString();
    const QString term =
        (title + QLatin1Char(' ') + author).trimmed().toLower();
    if (term.isEmpty()) {
        state.audiobooksLoading = false;
        return;
    }

    QUrl url(QStringLiteral("https://audiobookbay.lu/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), term);
    url.setQuery(query);
    requestRaw(
        bridge, url, 15000, kBrowserUa,
        [&bridge, id, title, author](bool success, QByteArray body) {
            auto it = g_states.find(id);
            if (it == g_states.end())
                return;
            it->audiobooks = success
                ? relevantAbbRows(
                      parseAbbSearch(QString::fromUtf8(body)),
                      title, author)
                : QVariantList{};
            it->audiobooksLoading = false;
            refresh(bridge, id);
            prewarmTopAudiobook(bridge, id);
        });
}

// qml/BiblioBook.qml:136-160.
void startTorrents(ColosseumWebBridge &bridge, DetailState &state)
{
    if (state.torrentsStarted)
        return;
    state.torrentsStarted = true;
    if (!g_torrents
        || state.book.value(QStringLiteral("title")).toString().isEmpty()) {
        state.torrentsLoading = false;
        resolvePendingLookup(bridge, state);
        return;
    }
    state.torrentsLoading = true;
    g_torrentOwner = state.id;
    g_torrents->search(
        state.book.value(QStringLiteral("title")).toString(),
        state.book.value(QStringLiteral("author")).toString());
}

void startSources(ColosseumWebBridge &bridge, DetailState &state)
{
    state.pairKey = pairKey(state.book);
    startLibGen(bridge, state);
    startAudiobooks(bridge, state);
    startTorrents(bridge, state);
}

void startMetadata(ColosseumWebBridge &bridge, DetailState &state)
{
    if (state.metadataStarted)
        return;
    state.metadataStarted = true;
    state.metadataLoading = true;
    const QString id = state.id;
    const QString title =
        state.book.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) {
        state.metadataLoading = false;
        state.metadataError =
            QStringLiteral("Book details are unavailable.");
        startSources(bridge, state);
        return;
    }

    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("media"), QStringLiteral("ebook"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("24"));
    query.addQueryItem(QStringLiteral("term"), title);
    url.setQuery(query);

    requestRaw(
        bridge, url, 15000,
        QByteArrayLiteral("Colosseum/1.0 (book detail)"),
        [&bridge, id, title](bool success, QByteArray body) {
            auto it = g_states.find(id);
            if (it == g_states.end())
                return;

            if (success) {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(body);
                const QJsonArray rows =
                    doc.object().value(QStringLiteral("results")).toArray();
                QJsonObject chosen;
                for (const QJsonValue &value : rows) {
                    const QJsonObject row = value.toObject();
                    qlonglong numericId =
                        row.value(QStringLiteral("trackId"))
                            .toVariant().toLongLong();
                    if (numericId <= 0)
                        numericId =
                            row.value(QStringLiteral("collectionId"))
                                .toVariant().toLongLong();
                    if (QString::number(numericId) == id) {
                        chosen = row;
                        break;
                    }
                }

                if (chosen.isEmpty()) {
                    const QString folded =
                        normalizedPairPart(title);
                    for (const QJsonValue &value : rows) {
                        const QJsonObject row = value.toObject();
                        QString rowTitle =
                            row.value(QStringLiteral("trackName"))
                                .toString();
                        if (rowTitle.isEmpty())
                            rowTitle =
                                row.value(
                                    QStringLiteral("trackCensoredName"))
                                    .toString();
                        if (normalizedPairPart(rowTitle) == folded) {
                            chosen = row;
                            break;
                        }
                    }
                }

                if (chosen.isEmpty() && !rows.isEmpty())
                    chosen = rows.first().toObject();
                if (!chosen.isEmpty())
                    it->book = fullBook(chosen, it->book);
            } else {
                it->metadataError =
                    QStringLiteral("Rich book details could not be loaded.");
            }

            it->metadataLoading = false;
            it->pairKey = pairKey(it->book);
            startSources(bridge, *it);
            refreshLocal(*it);
            refresh(bridge, id);
        });
}


QVariantMap customSection(const QString &id, int index, const QString &title,
                          const QString &state, const QString &schema,
                          const QVariantMap &data = {}, const QString &error = {})
{
    QVariantMap section = WebFeedValue::section(
        id, index, title, QStringLiteral("custom"), {}, state);
    QVariantMap payload = data;
    payload.insert(QStringLiteral("schema"), schema);
    section.insert(QStringLiteral("data"), WebFeedValue::jsonMap(payload));
    if (!error.isEmpty())
        section.insert(QStringLiteral("error"), error);
    return section;
}

double readingProgress(ColosseumWebBridge &bridge, const DetailState &state)
{
    // qml/BiblioBook.qml:217-229.
    auto *progress =
        qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));
    if (!progress)
        return -1;
    QString id = state.book.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = state.localPath;
    if (id.isEmpty())
        return -1;
    const QVariantMap record = progress->get(QStringLiteral("book"), id);
    if (!record.contains(QStringLiteral("progress")))
        return -1;
    const double value = record.value(QStringLiteral("progress")).toDouble();
    if (!std::isfinite(value))
        return -1;
    return std::clamp(value, 0.0, 1.0);
}

bool savedInCollection(ColosseumWebBridge &bridge, const DetailState &state)
{
    auto *collection =
        qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
    return collection
        && collection->has(QStringLiteral("biblio"), state.pairKey);
}

QString primaryLabel(const QVariantMap &snapshot)
{
    // qml/BiblioBook.qml:231-239.
    const QString transport =
        snapshot.value(QStringLiteral("pendingTransport")).toString();
    if (transport == QLatin1String("books")
        || transport == QLatin1String("torrent"))
        return QStringLiteral("Read when ready");

    const QString local =
        snapshot.value(QStringLiteral("localPath")).toString();
    const double progress =
        snapshot.value(QStringLiteral("progress"), -1).toDouble();
    if (!local.isEmpty() && progress > 0 && progress < 1)
        return QStringLiteral("Continue");
    return QStringLiteral("Read");
}

QString primaryStatus(const QVariantMap &snapshot)
{
    // qml/BiblioBook.qml:240-261.
    const QString error =
        snapshot.value(QStringLiteral("readError")).toString();
    if (!error.isEmpty())
        return error;

    const QString local =
        snapshot.value(QStringLiteral("localPath")).toString();
    const double progress =
        snapshot.value(QStringLiteral("progress"), -1).toDouble();
    if (!local.isEmpty()) {
        if (progress > 0 && progress < 1)
            return QString::number(qRound(progress * 100))
                + QStringLiteral("% read · Ready on this device");
        return QStringLiteral("Ready on this device");
    }

    const QString transport =
        snapshot.value(QStringLiteral("pendingTransport")).toString();
    const QString state =
        snapshot.value(QStringLiteral("pendingState")).toString();
    if (transport == QLatin1String("lookup"))
        return QStringLiteral("Finding a readable edition…");
    if (transport == QLatin1String("choice"))
        return QStringLiteral("Choose an edition to continue");
    if (transport == QLatin1String("books")
        || transport == QLatin1String("torrent")) {
        if (state == QLatin1String("resolving"))
            return QStringLiteral("Preparing your edition…");
        if (state == QLatin1String("queued"))
            return QStringLiteral("Queued · your Read will open when ready");
        const double total =
            snapshot.value(QStringLiteral("pendingTotal")).toDouble();
        if (total > 0) {
            const double received =
                snapshot.value(QStringLiteral("pendingReceived")).toDouble();
            return QStringLiteral("Your Read is attached · ")
                + QString::number(qRound(received / total * 100))
                + QLatin1Char('%');
        }
        return QStringLiteral("Your Read is attached · downloading");
    }

    const QVariantList editions =
        snapshot.value(QStringLiteral("editions")).toList();
    QVariantMap best;
    for (const QVariant &value : editions) {
        if (value.toMap().value(QStringLiteral("best")).toBool()) {
            best = value.toMap();
            break;
        }
    }
    if (best.isEmpty() && !editions.isEmpty())
        best = editions.first().toMap();
    if (!best.isEmpty()
        && !best.value(QStringLiteral("md5")).toString().isEmpty()
        && g_books)
        return QStringLiteral("Preferred ")
            + fmtLabel(best)
            + QStringLiteral(" will be prepared if needed");

    if (snapshot.value(QStringLiteral("editionsLoading")).toBool()
        || snapshot.value(QStringLiteral("torrentsLoading")).toBool())
        return QStringLiteral("Sources are loading");
    if (!editions.isEmpty()
        || !snapshot.value(QStringLiteral("torrents")).toList().isEmpty())
        return QStringLiteral("Read will choose a tracked edition");
    return QStringLiteral("No tracked readable source yet");
}

bool valid(const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    const QString world = params.value(QStringLiteral("world")).toString();
    return !id.isEmpty() && id.size() <= 512
        && params.value(QStringLiteral("title")).toString().size() <= 500
        && params.value(QStringLiteral("cover")).toString().size() <= 4096
        && (world.isEmpty() || world == QLatin1String("Biblio"));
}

QVariantList initial(const QVariantMap &params)
{
    const QVariantMap hero{
        {QStringLiteral("title"), params.value(QStringLiteral("title"))},
        {QStringLiteral("cover"), params.value(QStringLiteral("cover"))},
        {QStringLiteral("saved"), false},
        {QStringLiteral("local"), false},
        {QStringLiteral("progress"), -1.0},
        {QStringLiteral("primaryLabel"), QStringLiteral("Read")},
        {QStringLiteral("primaryStatus"), QStringLiteral("Sources are loading")},
        {QStringLiteral("primaryEnabled"), true},
        {QStringLiteral("readError"), QString()}
    };
    return {
        customSection(QStringLiteral("hero"), 0, QString(),
                      QStringLiteral("ready"),
                      QStringLiteral("book.hero"), hero),
        customSection(QStringLiteral("torrents"), 1,
                      QStringLiteral("Torrents"),
                      QStringLiteral("loading"),
                      QStringLiteral("book.torrents"),
                      {{QStringLiteral("loading"), true},
                       {QStringLiteral("rows"), QVariantList{}},
                       {QStringLiteral("count"), 0},
                       {QStringLiteral("collapsedCount"), 5}}),
        customSection(QStringLiteral("editions"), 2,
                      QStringLiteral("Editions"),
                      QStringLiteral("loading"),
                      QStringLiteral("book.editions"),
                      {{QStringLiteral("loading"), true},
                       {QStringLiteral("rows"), QVariantList{}},
                       {QStringLiteral("count"), 0}}),
        customSection(QStringLiteral("audiobooks"), 3,
                      QStringLiteral("Audiobook"),
                      QStringLiteral("loading"),
                      QStringLiteral("book.audiobooks"),
                      {{QStringLiteral("loading"), true},
                       {QStringLiteral("rows"), QVariantList{}},
                       {QStringLiteral("activeSlug"), QString()},
                       {QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("received"), 0.0},
                       {QStringLiteral("total"), 0.0},
                       {QStringLiteral("local"), false}})
    };
}

QVariantMap publicEdition(const QVariantMap &row)
{
    const QString md5 = row.value(QStringLiteral("md5")).toString();
    const QVariantMap status = bookStatus(md5);
    return {
        {QStringLiteral("key"), md5},
        {QStringLiteral("sourceKey"), md5},
        {QStringLiteral("md5"), md5},
        {QStringLiteral("format"), row.value(QStringLiteral("format"))},
        {QStringLiteral("formatLabel"), fmtLabel(row)},
        {QStringLiteral("meta"), editionMeta(row)},
        {QStringLiteral("size"), row.value(QStringLiteral("size"))},
        {QStringLiteral("source"), row.value(QStringLiteral("source"))},
        {QStringLiteral("year"), row.value(QStringLiteral("year"))},
        {QStringLiteral("language"), row.value(QStringLiteral("language"))},
        {QStringLiteral("best"), row.value(QStringLiteral("best"))},
        {QStringLiteral("state"),
         status.value(QStringLiteral("state"), QStringLiteral("none"))},
        {QStringLiteral("received"), status.value(QStringLiteral("received"))},
        {QStringLiteral("total"), status.value(QStringLiteral("total"))}
    };
}

QVariantMap publicTorrent(const QVariantMap &row)
{
    const QString hash =
        row.value(QStringLiteral("infoHash")).toString().toLower();
    const QVariantMap status = torrentStatus(hash);
    return {
        {QStringLiteral("key"), hash},
        {QStringLiteral("sourceKey"), hash},
        {QStringLiteral("title"), row.value(QStringLiteral("title"))},
        {QStringLiteral("seeders"), row.value(QStringLiteral("seeders"))},
        {QStringLiteral("size"), row.value(QStringLiteral("size"))},
        {QStringLiteral("pack"), row.value(QStringLiteral("pack"))},
        {QStringLiteral("state"),
         status.value(QStringLiteral("state"), QStringLiteral("none"))},
        {QStringLiteral("received"), status.value(QStringLiteral("received"))},
        {QStringLiteral("total"), status.value(QStringLiteral("total"))}
    };
}

QVariantMap publicAudiobook(const QVariantMap &row)
{
    const QString slug = row.value(QStringLiteral("slug")).toString();
    return {
        {QStringLiteral("key"), slug},
        {QStringLiteral("sourceKey"), slug},
        {QStringLiteral("slug"), slug},
        {QStringLiteral("format"), row.value(QStringLiteral("format"))},
        {QStringLiteral("size"), row.value(QStringLiteral("size"))},
        {QStringLiteral("language"), row.value(QStringLiteral("language"))},
        {QStringLiteral("posted"), row.value(QStringLiteral("posted"))}
    };
}

void capture(ColosseumWebBridge &bridge, FeedContext &ctx)
{
    // GUI-thread snapshot. BiblioBook.qml owns QObject services; the worker
    // receives only copied values through nativeSnapshot.
    bindServices(bridge);
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    auto it = g_states.find(id);
    if (it == g_states.end()) {
        DetailState state;
        state.id = id;
        state.book = {
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), ctx.params.value(QStringLiteral("title"))},
            {QStringLiteral("cover"), ctx.params.value(QStringLiteral("cover"))},
            {QStringLiteral("author"), ctx.params.value(QStringLiteral("author"))}
        };
        state.pairKey = pairKey(state.book);
        it = g_states.insert(id, std::move(state));
    } else {
        if (it->book.value(QStringLiteral("title")).toString().isEmpty())
            it->book.insert(QStringLiteral("title"),
                            ctx.params.value(QStringLiteral("title")));
        if (it->book.value(QStringLiteral("cover")).toString().isEmpty())
            it->book.insert(QStringLiteral("cover"),
                            ctx.params.value(QStringLiteral("cover")));
    }

    startMetadata(bridge, *it);
    refreshLocal(*it);

    QVariantList editions;
    for (const QVariant &value : it->editions)
        editions.append(publicEdition(value.toMap()));

    QVariantList torrents;
    for (const QVariant &value : it->torrents)
        torrents.append(publicTorrent(value.toMap()));

    QVariantList audiobooks;
    for (const QVariant &value : it->audiobooks)
        audiobooks.append(publicAudiobook(value.toMap()));

    QVariantMap audioStatus;
    bool audioLocal = false;
    if (g_audiobooks) {
        audioStatus = g_audiobooks->statusOf(it->pairKey);
        audioLocal = g_audiobooks->isDownloaded(it->pairKey);
    }

    QVariantMap snapshot{
        {QStringLiteral("book"), it->book},
        {QStringLiteral("pairKey"), it->pairKey},
        {QStringLiteral("metadataLoading"), it->metadataLoading},
        {QStringLiteral("metadataError"), it->metadataError},
        {QStringLiteral("editionsLoading"), it->editionsLoading},
        {QStringLiteral("editions"), editions},
        {QStringLiteral("torrentsLoading"), it->torrentsLoading},
        {QStringLiteral("torrents"), torrents},
        {QStringLiteral("audiobooksLoading"), it->audiobooksLoading},
        {QStringLiteral("audiobooks"), audiobooks},
        {QStringLiteral("audiobookActiveSlug"), it->audiobookActiveSlug},
        {QStringLiteral("audiobookState"),
         audioStatus.value(QStringLiteral("state"), it->audiobookState)},
        {QStringLiteral("audiobookReceived"),
         audioStatus.value(QStringLiteral("received"), it->audiobookReceived)},
        {QStringLiteral("audiobookTotal"),
         audioStatus.value(QStringLiteral("total"), it->audiobookTotal)},
        {QStringLiteral("audioLocal"), audioLocal},
        {QStringLiteral("localPath"), it->localPath},
        {QStringLiteral("progress"), readingProgress(bridge, *it)},
        {QStringLiteral("saved"), savedInCollection(bridge, *it)},
        {QStringLiteral("pendingTransport"), it->pendingReadTransport},
        {QStringLiteral("pendingState"), it->pendingReadState},
        {QStringLiteral("pendingReceived"), it->pendingReadReceived},
        {QStringLiteral("pendingTotal"), it->pendingReadTotal},
        {QStringLiteral("readError"), it->readError},
        {QStringLiteral("readChoices"), it->readChoices}
    };
    ctx.nativeSnapshot = WebFeedValue::jsonMap(snapshot);
}

QVariantList build(const FeedContext &ctx)
{
    const QVariantMap snapshot = ctx.nativeSnapshot;
    QVariantMap hero = snapshot.value(QStringLiteral("book")).toMap();
    hero.insert(QStringLiteral("saved"),
                snapshot.value(QStringLiteral("saved")));
    hero.insert(QStringLiteral("local"),
                !snapshot.value(QStringLiteral("localPath")).toString().isEmpty());
    hero.insert(QStringLiteral("progress"),
                snapshot.value(QStringLiteral("progress"), -1.0));
    hero.insert(QStringLiteral("primaryLabel"), primaryLabel(snapshot));
    hero.insert(QStringLiteral("primaryStatus"), primaryStatus(snapshot));
    hero.insert(QStringLiteral("readError"),
                snapshot.value(QStringLiteral("readError")));

    const bool readEnabled =
        !snapshot.value(QStringLiteral("localPath")).toString().isEmpty()
        || !snapshot.value(QStringLiteral("pendingTransport")).toString().isEmpty()
        || snapshot.value(QStringLiteral("editionsLoading")).toBool()
        || snapshot.value(QStringLiteral("torrentsLoading")).toBool()
        || !snapshot.value(QStringLiteral("editions")).toList().isEmpty()
        || !snapshot.value(QStringLiteral("torrents")).toList().isEmpty();
    hero.insert(QStringLiteral("primaryEnabled"), readEnabled);

    QVariantList sections;
    sections.append(customSection(
        QStringLiteral("hero"), 0, QString(), QStringLiteral("ready"),
        QStringLiteral("book.hero"), hero,
        snapshot.value(QStringLiteral("metadataError")).toString()));

    const QVariantList torrents =
        snapshot.value(QStringLiteral("torrents")).toList();
    const bool torrentsLoading =
        snapshot.value(QStringLiteral("torrentsLoading")).toBool();
    sections.append(customSection(
        QStringLiteral("torrents"), 1, QStringLiteral("Torrents"),
        torrentsLoading ? QStringLiteral("loading")
                        : torrents.isEmpty()
                            ? QStringLiteral("empty")
                            : QStringLiteral("ready"),
        QStringLiteral("book.torrents"),
        {{QStringLiteral("loading"), torrentsLoading},
         {QStringLiteral("rows"), torrents},
         {QStringLiteral("count"), torrents.size()},
         {QStringLiteral("collapsedCount"), 5}}));

    const QVariantList editions =
        snapshot.value(QStringLiteral("editions")).toList();
    const bool editionsLoading =
        snapshot.value(QStringLiteral("editionsLoading")).toBool();
    sections.append(customSection(
        QStringLiteral("editions"), 2, QStringLiteral("Editions"),
        editionsLoading ? QStringLiteral("loading")
                        : editions.isEmpty()
                            ? QStringLiteral("empty")
                            : QStringLiteral("ready"),
        QStringLiteral("book.editions"),
        {{QStringLiteral("loading"), editionsLoading},
         {QStringLiteral("rows"), editions},
         {QStringLiteral("count"), editions.size()}}));

    const QVariantList audiobooks =
        snapshot.value(QStringLiteral("audiobooks")).toList();
    const bool audiobooksLoading =
        snapshot.value(QStringLiteral("audiobooksLoading")).toBool();
    sections.append(customSection(
        QStringLiteral("audiobooks"), 3, QStringLiteral("Audiobook"),
        audiobooksLoading ? QStringLiteral("loading")
                          : audiobooks.isEmpty()
                              ? QStringLiteral("empty")
                              : QStringLiteral("ready"),
        QStringLiteral("book.audiobooks"),
        {{QStringLiteral("loading"), audiobooksLoading},
         {QStringLiteral("rows"), audiobooks},
         {QStringLiteral("activeSlug"),
          snapshot.value(QStringLiteral("audiobookActiveSlug"))},
         {QStringLiteral("state"),
          snapshot.value(QStringLiteral("audiobookState"),
                         QStringLiteral("none"))},
         {QStringLiteral("received"),
          snapshot.value(QStringLiteral("audiobookReceived"))},
         {QStringLiteral("total"),
          snapshot.value(QStringLiteral("audiobookTotal"))},
         {QStringLiteral("local"),
          snapshot.value(QStringLiteral("audioLocal"))}}));

    const QVariantList choices =
        snapshot.value(QStringLiteral("readChoices")).toList();
    if (!choices.isEmpty()) {
        sections.append(customSection(
            QStringLiteral("read-choice"), 4, QString(),
            QStringLiteral("ready"),
            QStringLiteral("book.readChoices"),
            {{QStringLiteral("rows"), choices}}));
    }
    return sections;
}

bool validIdAction(const QVariantMap &payload)
{
    const QString id = payload.value(QStringLiteral("id")).toString();
    return !id.isEmpty() && id.size() <= 512;
}

bool validSourceAction(const QVariantMap &payload)
{
    return validIdAction(payload)
        && !payload.value(QStringLiteral("sourceKey")).toString().isEmpty()
        && payload.value(QStringLiteral("sourceKey")).toString().size() <= 512;
}

DetailState *activeState(ColosseumWebBridge &bridge,
                         const QVariantMap &payload,
                         const ActionRegistry::Completion &complete)
{
    const QString id = payload.value(QStringLiteral("id")).toString();
    if (!bridge.detailActive(kFeed, id)) {
        complete(fail(QStringLiteral("This book page is no longer open.")));
        return nullptr;
    }
    auto it = g_states.find(id);
    if (it == g_states.end()) {
        complete(fail(QStringLiteral("This book is still loading.")));
        return nullptr;
    }
    return &it.value();
}

QVariantMap findEdition(const DetailState &state, const QString &md5)
{
    for (const QVariant &value : state.editions) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("md5")).toString() == md5)
            return row;
    }
    return {};
}

QVariantMap findTorrent(const DetailState &state, const QString &hash)
{
    for (const QVariant &value : state.torrents) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("infoHash")).toString().toLower()
            == hash.toLower())
            return row;
    }
    return {};
}

QVariantMap findAudiobook(const DetailState &state, const QString &slug)
{
    for (const QVariant &value : state.audiobooks) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("slug")).toString() == slug)
            return row;
    }
    return {};
}

void handleRead(ColosseumWebBridge &bridge, const QVariantMap &payload,
                ActionRegistry::Completion complete)
{
    // qml/BiblioBook.qml:488-498.
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    refreshLocal(*state);
    if (!state->localPath.isEmpty()) {
        invalidateReadIntent(*state);
        state->readError.clear();
        delegateReader(bridge, *state, state->localPath, std::move(complete));
        return;
    }

    invalidateReadIntent(*state);
    ++state->readGeneration;
    state->pendingReadBookIdentity = state->pairKey;
    state->pendingReadTransport = QStringLiteral("lookup");
    state->pendingReadState = QStringLiteral("finding");
    state->readError.clear();
    state->pendingReadCompletion = std::move(complete);
    resolvePendingLookup(bridge, *state);
}

void handleChooseRead(ColosseumWebBridge &bridge,
                      const QVariantMap &payload,
                      ActionRegistry::Completion complete)
{
    // qml/BiblioBook.qml:404-414.
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    if (state->pendingReadTransport != QLatin1String("choice")) {
        complete(fail(QStringLiteral("There is no edition choice waiting.")));
        return;
    }

    const QString transport =
        payload.value(QStringLiteral("transport")).toString();
    const QString sourceKey =
        payload.value(QStringLiteral("sourceKey")).toString();
    QVariantMap chosen;
    for (const QVariant &value : state->readChoices) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("transport")).toString() == transport
            && row.value(QStringLiteral("id")).toString() == sourceKey) {
            chosen = row;
            break;
        }
    }
    if (chosen.isEmpty()) {
        complete(fail(QStringLiteral("That edition is no longer available.")));
        return;
    }

    if (!targetReadCandidate(bridge, *state, chosen)) {
        complete(fail(QStringLiteral("That edition cannot be prepared.")));
        return;
    }
    // The original detail.book.read promise remains the foreground intent.
    complete(ok());
}

void handleCancelRead(ColosseumWebBridge &bridge,
                      const QVariantMap &payload,
                      ActionRegistry::Completion complete)
{
    const QString id = payload.value(QStringLiteral("id")).toString();
    auto it = g_states.find(id);
    if (it == g_states.end()) {
        complete(ok());
        return;
    }
    DetailState *state = &it.value();

    auto readCompletion = std::move(state->pendingReadCompletion);
    ++state->readGeneration;
    clearReadIntent(*state);
    state->readError.clear();
    refresh(bridge, state->id);

    // Leaving the page retires only the foreground auto-open. The download
    // continues exactly like BiblioBook.qml::_invalidateReadIntent().
    if (readCompletion)
        readCompletion(ok(QVariantMap{
            {QStringLiteral("cancelled"), true}}));
    complete(ok());
}

void handleEdition(ColosseumWebBridge &bridge,
                   const QVariantMap &payload,
                   ActionRegistry::Completion complete)
{
    // qml/BiblioBook.qml:501-517.
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    const QString md5 =
        payload.value(QStringLiteral("sourceKey")).toString();
    const QVariantMap edition = findEdition(*state, md5);
    if (edition.isEmpty() || !g_books) {
        complete(fail(QStringLiteral("This edition is unavailable.")));
        return;
    }

    invalidateReadIntent(*state);
    state->readError.clear();
    const QString current =
        g_books->statusOf(md5).value(QStringLiteral("state")).toString();
    if (current == QLatin1String("done") || inFlight(current)) {
        refresh(bridge, state->id);
        complete(ok());
        return;
    }

    clearExistingCopies(*state);
    collectBook(bridge, *state);
    g_books->downloadBook(
        md5,
        state->book.value(QStringLiteral("title"),
                          QStringLiteral("book")).toString()
            + QLatin1Char('.')
            + edition.value(QStringLiteral("format"),
                            QStringLiteral("epub")).toString(),
        state->book.value(QStringLiteral("title")).toString(),
        0,
        state->book.value(QStringLiteral("author")).toString());
    refresh(bridge, state->id);
    complete(ok());
}

void handleTorrent(ColosseumWebBridge &bridge,
                   const QVariantMap &payload,
                   ActionRegistry::Completion complete)
{
    // qml/BiblioBook.qml:518-527.
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    const QString hash =
        payload.value(QStringLiteral("sourceKey")).toString().toLower();
    if (findTorrent(*state, hash).isEmpty() || !g_torrents) {
        complete(fail(QStringLiteral("This torrent edition is unavailable.")));
        return;
    }

    invalidateReadIntent(*state);
    state->readError.clear();
    const QString current =
        g_torrents->statusOf(hash).value(QStringLiteral("state")).toString();
    if (current == QLatin1String("done") || inFlight(current)) {
        refresh(bridge, state->id);
        complete(ok());
        return;
    }

    clearExistingCopies(*state);
    collectBook(bridge, *state);
    g_torrents->download(
        hash,
        state->book.value(QStringLiteral("title")).toString(),
        state->book.value(QStringLiteral("author")).toString());
    refresh(bridge, state->id);
    complete(ok());
}

void handleAudiobook(ColosseumWebBridge &bridge,
                     const QVariantMap &payload,
                     ActionRegistry::Completion complete)
{
    // qml/BiblioBook.qml:548-561.
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    if (!g_audiobooks) {
        complete(fail(QStringLiteral("Audiobook downloads are unavailable.")));
        return;
    }

    const QString slug =
        payload.value(QStringLiteral("sourceKey")).toString();
    const QVariantMap row = findAudiobook(*state, slug);
    if (row.isEmpty()) {
        complete(fail(QStringLiteral("That audiobook is no longer available.")));
        return;
    }

    // Active completed row opens the ebook Reader2 surface, matching
    // activateAudiobookRow().
    if (state->audiobookActiveSlug == slug
        && g_audiobooks->isDownloaded(state->pairKey)
        && !state->localPath.isEmpty()) {
        delegateReader(bridge, *state, state->localPath, std::move(complete));
        return;
    }

    const QVariantMap live = g_audiobooks->statusOf(state->pairKey);
    const QString liveState =
        live.value(QStringLiteral("state")).toString();
    if (liveState == QLatin1String("resolving")
        || liveState == QLatin1String("downloading")
        || liveState == QLatin1String("queued")) {
        complete(ok());
        return;
    }

    state->audiobookActiveSlug = slug;
    state->audiobookState = QStringLiteral("resolving");
    refresh(bridge, state->id);

    const QString id = state->id;
    const QUrl url(
        QStringLiteral("https://audiobookbay.lu/abss/")
        + slug + QLatin1Char('/'));
    requestRaw(
        bridge, url, 15000, kBrowserUa,
        [&bridge, id, slug,
         complete = std::move(complete)](bool success, QByteArray body) mutable {
            auto it = g_states.find(id);
            if (it == g_states.end()
                || !bridge.detailActive(kFeed, id)
                || it->audiobookActiveSlug != slug) {
                complete(fail(QStringLiteral("This book page is no longer open.")));
                return;
            }
            const QString hash = success
                ? parseAbbInfoHash(QString::fromUtf8(body))
                : QString();
            if (hash.isEmpty()) {
                it->audiobookState = QStringLiteral("failed");
                refresh(bridge, id);
                complete(fail(QStringLiteral(
                    "The audiobook source could not be resolved.")));
                return;
            }

            QString bookId;
            if (!it->localPath.isEmpty()) {
                if (auto *reader =
                        qobject_cast<Reader2Bridge *>(
                            bridge.service(QStringLiteral("Reader2Bridge"))))
                    bookId = reader->bookKey(it->localPath);
            }

            g_audiobooks->downloadAudiobook(
                it->pairKey, hash,
                it->book.value(QStringLiteral("title")).toString(),
                it->book.value(QStringLiteral("author")).toString(),
                bookId, it->localPath);
            collectBook(bridge, *it);
            const QVariantMap status =
                g_audiobooks->statusOf(it->pairKey);
            it->audiobookState =
                status.value(QStringLiteral("state"),
                             QStringLiteral("resolving")).toString();
            refresh(bridge, id);
            complete(ok());
        });
}

void handleCollection(ColosseumWebBridge &bridge,
                      const QVariantMap &payload,
                      ActionRegistry::Completion complete)
{
    DetailState *state = activeState(bridge, payload, complete);
    if (!state)
        return;
    auto *collection =
        qobject_cast<CollectionStore *>(
            bridge.service(QStringLiteral("Collection")));
    if (!collection || !collection->healthy()) {
        complete(fail(QStringLiteral("Collection is unavailable.")));
        return;
    }

    const bool saved =
        payload.value(QStringLiteral("saved")).toBool();
    const bool changed = saved
        ? collection->add(
            QStringLiteral("biblio"),
            {{QStringLiteral("id"), state->pairKey},
             {QStringLiteral("type"), QStringLiteral("book")},
             {QStringLiteral("title"),
              state->book.value(QStringLiteral("title"))},
             {QStringLiteral("cover"),
              state->book.value(QStringLiteral("cover"))},
             {QStringLiteral("payload"),
              QVariantMap{{QStringLiteral("book"), state->book}}}})
        : collection->remove(
            QStringLiteral("biblio"), state->pairKey);
    if (!changed) {
        complete(fail(QStringLiteral("Collection could not be saved.")));
        return;
    }
    refresh(bridge, state->id);
    complete(ok());
}

QMetaObject::Connection watchProgress(QObject *object, QObject *receiver,
                                      std::function<void()> changed)
{
    auto *progress = qobject_cast<ProgressStore *>(object);
    if (!progress)
        return {};
    return QObject::connect(
        progress, &ProgressStore::changed, receiver,
        [changed = std::move(changed)] { changed(); });
}

QMetaObject::Connection watchCollection(QObject *object, QObject *receiver,
                                        std::function<void()> changed)
{
    auto *collection = qobject_cast<CollectionStore *>(object);
    if (!collection)
        return {};
    return QObject::connect(
        collection, &CollectionStore::changed, receiver,
        [changed = std::move(changed)] { changed(); });
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = kFeed;
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.capture = capture;
    entry.ownerSignals.append(
        {QStringLiteral("Progress"), watchProgress});
    entry.ownerSignals.append(
        {QStringLiteral("Collection"), watchCollection});
    return FeedRegistry::add(std::move(entry));
}();

const bool readRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.read"),
    validIdAction,
    handleRead
});

const bool chooseReadRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.chooseRead"),
    [](const QVariantMap &payload) {
        return validSourceAction(payload)
            && (payload.value(QStringLiteral("transport"))
                    == QLatin1String("books")
                || payload.value(QStringLiteral("transport"))
                    == QLatin1String("torrent"));
    },
    handleChooseRead
});

const bool cancelReadRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.cancelRead"),
    validIdAction,
    handleCancelRead
});

const bool editionRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.downloadEdition"),
    validSourceAction,
    handleEdition
});

const bool torrentRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.downloadTorrent"),
    validSourceAction,
    handleTorrent
});

const bool audiobookRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.downloadAudiobook"),
    validSourceAction,
    handleAudiobook
});

const bool collectionRegistered = ActionRegistry::add({
    QStringLiteral("detail.book.collection"),
    [](const QVariantMap &payload) {
        return validIdAction(payload)
            && payload.value(QStringLiteral("saved"))
                   .metaType().id() == QMetaType::Bool;
    },
    handleCollection
});

} // namespace
