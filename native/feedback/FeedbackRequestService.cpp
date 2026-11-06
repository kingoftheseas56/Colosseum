#include "FeedbackRequestService.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {
QNetworkRequest jsonRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20000);
    return request;
}

QString platform()
{
#if defined(Q_OS_ANDROID)
    return u"Android"_s;
#elif defined(Q_OS_WIN)
    return u"Windows"_s;
#elif defined(Q_OS_MACOS)
    return u"macOS"_s;
#else
    return u"Linux"_s;
#endif
}

int statusCode(const QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}
}

FeedbackRequestService::FeedbackRequestService(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this))
{
}

QString FeedbackRequestService::newReference() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void FeedbackRequestService::finish(bool ok, const QString &message)
{
    m_sending = false;
    emit submissionFinished(ok, message);
}

void FeedbackRequestService::submit(const QString &kind, const QVariantMap &fields)
{
    if (m_sending)
        return;

    const bool universe = kind == u"universe";
    const bool feature = kind == u"feature";
    if (!universe && !feature && kind != u"bug") {
        finish(false, tr("This request type is unavailable."));
        return;
    }

    const QString title = fields.value(u"title"_s).toString().trimmed();
    const QString message = fields.value(u"message"_s).toString().trimmed();
    const QString email = fields.value(u"email"_s).toString().trimmed();
    static const QRegularExpression emailPattern(u"^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"_s);
    if (title.isEmpty() || title.size() > 120 || message.size() < (universe ? 1 : 20)
        || message.size() > (universe ? 120 : 4000)) {
        finish(false, universe ? tr("Enter a franchise name.") : tr("Enter a short title and at least 20 characters of detail."));
        return;
    }
    if (!universe && !email.isEmpty() && (email.size() > 254 || !emailPattern.match(email).hasMatch())) {
        finish(false, tr("Enter a valid email address, or leave it empty."));
        return;
    }

    QString reference = fields.value(u"reference"_s).toString();
    static const QRegularExpression referencePattern(u"^[A-Za-z0-9_-]{1,64}$"_s);
    if (!referencePattern.match(reference).hasMatch())
        reference = newReference();

    const QString type = universe ? u"Universe request"_s : feature ? u"Feature idea"_s : u"Bug report"_s;
    const QString area = universe ? u"Universes"_s : u"General"_s;
    const QString version = QCoreApplication::applicationVersion();
    QJsonObject payload{{u"type"_s, type}, {u"area"_s, area}, {u"title"_s, title},
                        {u"message"_s, message}, {u"reference"_s, reference},
                        {u"platform"_s, platform()},
                        {u"app_version"_s, version.isEmpty() ? u"Not supplied"_s : version}};
    if (universe) {
        const qint64 page = fields.value(u"wikipedia_page_id"_s).toLongLong();
        if (page > 0) {
            payload.insert(u"wikipedia_page_id"_s, page);
            const auto article = QUrl::toPercentEncoding(QString(title).replace(u' ', u'_'));
            payload.insert(u"wikipedia_url"_s, u"https://en.wikipedia.org/wiki/"_s + QString::fromLatin1(article));
        }
    } else {
        if (!email.isEmpty())
            payload.insert(u"email"_s, email);
        const auto addOptional = [&](const QString &key, qsizetype limit) {
            const QString value = fields.value(key).toString().trimmed().left(limit);
            if (!value.isEmpty())
                payload.insert(key, value);
        };
        if (feature) {
            addOptional(u"benefit"_s, 2500);
        } else {
            addOptional(u"steps_to_reproduce"_s, 2500);
            addOptional(u"expected_behavior"_s, 2000);
        }
        payload.insert(u"_gotcha"_s, QString());
    }

    QString subject = u"[Colosseum] [%1] [%2] %3"_s.arg(type, area, title);
    subject.replace(u'\r', u' ');
    subject.replace(u'\n', u' ');
    m_sending = true;
    auto *configReply = m_network->get(jsonRequest(QUrl(u"https://getcolosseum.world/feedback/config.json"_s)));
    connect(configReply, &QNetworkReply::finished, this, [this, configReply, payload, subject]() mutable {
        const auto config = QJsonDocument::fromJson(configReply->readAll()).object();
        const bool loaded = configReply->error() == QNetworkReply::NoError && statusCode(configReply) == 200;
        configReply->deleteLater();
        if (!loaded) {
            finish(false, tr("Could not connect to feedback. Keep your draft and try again."));
            return;
        }
        if (!config.value(u"turnstileSiteKey"_s).toString().isEmpty()) {
            finish(false, tr("The feedback inbox requires browser verification. Your draft is still here."));
            return;
        }

        const QString provider = config.value(u"provider"_s).toString();
        const bool web3 = provider == u"web3forms";
        QUrl endpoint;
        if (web3) {
            const QString key = config.value(u"accessKey"_s).toString();
            static const QRegularExpression keyPattern(
                u"^[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"_s);
            if (keyPattern.match(key).hasMatch()) {
                endpoint = QUrl(u"https://api.web3forms.com/submit"_s);
                payload.insert(u"access_key"_s, key);
                payload.insert(u"subject"_s, subject);
                payload.insert(u"from_name"_s, u"Colosseum feedback"_s);
                payload.insert(u"botcheck"_s, false);
            }
        } else if (provider == u"formspark") {
            const QString address = config.value(u"formEndpoint"_s).toString();
            static const QRegularExpression endpointPattern(u"^https://submit-form\\.com/[A-Za-z0-9_-]+$"_s);
            if (endpointPattern.match(address).hasMatch()) {
                endpoint = QUrl(address);
                payload.insert(u"_email"_s, QJsonObject{{u"subject"_s, subject}});
            }
        }
        if (endpoint.isEmpty()) {
            finish(false, tr("The feedback inbox is unavailable. Keep your draft and try again later."));
            return;
        }

        auto request = jsonRequest(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        auto *reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, web3] {
            const int status = statusCode(reply);
            const auto response = QJsonDocument::fromJson(reply->readAll());
            const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && response.isObject() && (!web3 || response.object().value(u"success"_s).toBool());
            reply->deleteLater();
            if (ok)
                finish(true, tr("Your request was sent."));
            else if (status == 402)
                finish(false, tr("The feedback inbox has reached capacity. Please try again later."));
            else if (status == 403)
                finish(false, tr("The feedback inbox's spam check rejected this request."));
            else if (status == 429)
                finish(false, tr("Too many attempts. Wait a moment, then try again."));
            else
                finish(false, tr("Your request could not be sent. Keep your draft and try again."));
        });
    });
}

void FeedbackRequestService::searchUniverses(const QString &query)
{
    if (m_search) {
        auto *previous = m_search.data();
        m_search.clear();
        previous->abort();
    }
    const QString term = query.trimmed();
    if (term.size() < 2 || term.size() > 120) {
        emit universeResults(query, {});
        return;
    }

    QUrl url(u"https://en.wikipedia.org/w/api.php"_s);
    QUrlQuery parameters;
    parameters.addQueryItem(u"action"_s, u"query"_s);
    parameters.addQueryItem(u"generator"_s, u"search"_s);
    parameters.addQueryItem(u"gsrsearch"_s, term);
    parameters.addQueryItem(u"gsrnamespace"_s, u"0"_s);
    parameters.addQueryItem(u"gsrlimit"_s, u"4"_s);
    parameters.addQueryItem(u"prop"_s, u"extracts|pageprops"_s);
    parameters.addQueryItem(u"ppprop"_s, u"disambiguation"_s);
    parameters.addQueryItem(u"explaintext"_s, u"1"_s);
    parameters.addQueryItem(u"exintro"_s, u"1"_s);
    parameters.addQueryItem(u"exchars"_s, u"160"_s);
    parameters.addQueryItem(u"exlimit"_s, u"4"_s);
    parameters.addQueryItem(u"format"_s, u"json"_s);
    parameters.addQueryItem(u"formatversion"_s, u"2"_s);
    url.setQuery(parameters);

    auto request = jsonRequest(url);
    request.setRawHeader("Api-User-Agent", "ColosseumUniverseRequest/1.0 (https://getcolosseum.world/)");
    auto *reply = m_network->get(request);
    m_search = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, query] {
        reply->deleteLater();
        if (m_search != reply)
            return;
        m_search.clear();
        QVariantList rows;
        if (reply->error() == QNetworkReply::NoError && statusCode(reply) == 200) {
            const auto document = QJsonDocument::fromJson(reply->readAll()).object();
            const auto pages = document.value(u"query"_s).toObject().value(u"pages"_s).toArray();
            for (const auto &value : pages) {
                const auto page = value.toObject();
                const QString title = page.value(u"title"_s).toString().trimmed();
                const qint64 id = page.value(u"pageid"_s).toInteger();
                if (id <= 0 || title.isEmpty() || title.size() > 120 || page.contains(u"missing"_s)
                    || page.value(u"pageprops"_s).toObject().contains(u"disambiguation"_s))
                    continue;
                rows.append(QVariantMap{{u"pageid"_s, id}, {u"title"_s, title},
                    {u"extract"_s, page.value(u"extract"_s).toString().simplified().left(160)},
                    {u"index"_s, page.value(u"index"_s).toInt()}});
            }
            std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value(u"index"_s).toInt() < b.toMap().value(u"index"_s).toInt();
            });
        }
        emit universeResults(query, rows);
    });
}
