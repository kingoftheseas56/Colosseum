#include "NuvioTmdbCredentials.h"

#include "account/credentials/AccountSecretBackend.h"

#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace NuvioTmdb {
namespace {
constexpr auto kTarget = "Brotherhood.Colosseum.Nuvio.Tmdb.v1";

Credential parse(QString value)
{
    if (value.endsWith(QChar::Null))
        value.chop(1);
    static const QRegularExpression v3(QStringLiteral(R"(\A[0-9a-fA-F]{32}\z)"));
    static const QRegularExpression v4(QStringLiteral(R"(\A[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\z)"));
    if (v3.match(value).hasMatch())
        return {value.toLatin1(), false};
    if (value.size() >= 64 && v4.match(value).hasMatch())
        return {value.toLatin1(), true};
    return {};
}
}

Credential loadCredential(const char *buildKey)
{
    const auto backend = createAccountSecretBackend();
    auto blob = backend ? backend->read(QString::fromLatin1(kTarget)) : std::nullopt;
#ifdef Q_OS_WIN
    // Opt-in development launches can use the credential provisioned for the host proof.
    if (!blob && !*buildKey && backend && qEnvironmentVariableIntValue("COLOSSEUM_DEV") == 1
        && qEnvironmentVariableIntValue("COLOSSEUM_NUVIO_P1_CREDENTIAL") == 1) {
        blob = backend->read(QStringLiteral("Colosseum-P1-TMDB"));
    }
#endif
    if (!blob || blob->isEmpty() || blob->size() > 4096)
        return parse(QString::fromLatin1(buildKey));

    Credential credential = parse(QString::fromUtf8(*blob));
#ifdef Q_OS_WIN
    // Credentials entered through the Windows UI may be stored as UTF-16.
    if (credential.secret.isEmpty() && blob->size() % 2 == 0) {
        QString utf16(blob->size() / 2, Qt::Uninitialized);
        for (qsizetype i = 0; i < utf16.size(); ++i) {
            const auto lo = static_cast<unsigned char>(blob->at(2 * i));
            const auto hi = static_cast<unsigned char>(blob->at(2 * i + 1));
            utf16[i] = QChar(ushort(lo | (hi << 8)));
        }
        credential = parse(utf16);
        utf16.fill(QChar::Null);
    }
#endif
    blob->fill('\0');
    return credential.secret.isEmpty() ? parse(QString::fromLatin1(buildKey)) : credential;
}

Authorization authorize(QNetworkRequest &request, const Credential &credential)
{
    const auto original = request.url();
    if (original.scheme() != QLatin1String("https")
        || original.host() != QLatin1String("api.themoviedb.org") || original.port(443) != 443
        || original.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        || (!original.path().startsWith(QLatin1String("/3/"))
            && !original.path().startsWith(QLatin1String("/4/")))) {
        return Authorization::Unchanged;
    }

    QUrlQuery query(original);
    const auto keys = query.allQueryItemValues(QStringLiteral("api_key"), QUrl::FullyDecoded);
    const bool queryToken = keys.size() == 1 && keys.first() == QLatin1String(placeholder);
    const bool headerToken = request.rawHeader("Authorization") == QByteArray("Bearer ") + placeholder;
    if ((!queryToken && !headerToken) || (!keys.isEmpty() && !queryToken))
        return Authorization::Unchanged;
    if (credential.secret.isEmpty())
        return Authorization::Missing;

    for (const auto &header : request.rawHeaderList())
        request.setRawHeader(header, {});
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Colosseum/Nuvio");
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);

    auto authorized = original;
    if (queryToken)
        query.removeAllQueryItems(QStringLiteral("api_key"));
    if (credential.bearer)
        request.setRawHeader("Authorization", QByteArray("Bearer ") + credential.secret);
    else
        query.addQueryItem(QStringLiteral("api_key"), QString::fromLatin1(credential.secret));
    authorized.setQuery(query);
    request.setUrl(authorized);
    return Authorization::Attached;
}
}
