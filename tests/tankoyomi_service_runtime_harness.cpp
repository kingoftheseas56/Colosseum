#include "engine/MangaPageTransport.h"
#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiIdentity.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariantMap>

namespace {

bool looksLikeImage(const QByteArray &data, const QString &contentType)
{
    if (data.size() < 12) return false;
    const unsigned char *bytes =
        reinterpret_cast<const unsigned char *>(data.constData());
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return true;
    if (bytes[0] == 0x89 && bytes[1] == 0x50
        && bytes[2] == 0x4E && bytes[3] == 0x47) return true;
    if (bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46) return true;
    if (data.startsWith("RIFF") && data.mid(8, 4) == QByteArrayLiteral("WEBP")) return true;
    return contentType.trimmed().toLower().startsWith(QLatin1String("image/"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        qCritical().noquote() << "usage: tankoyomi_service_runtime_harness <language> <query> [expected-provider]";
        return 64;
    }
    const QString language = args.at(1);
    const QString query = args.at(2);
    const QString expectedProvider = args.size() > 3 ? args.at(3) : QString();

    QNetworkAccessManager nam;
    TankoyomiChapterService service(&nam);
    QString selectedChapterId;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(90000);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&]() {
        qCritical().noquote() << "FAIL timeout" << language << query;
        app.exit(70);
    });
    QObject::connect(&service, &TankoyomiChapterService::catalogueFailed, &app,
                     [&](const QString &requestId, const QString &message) {
        if (requestId != QLatin1String("catalogue")) return;
        qCritical().noquote() << "FAIL catalogue" << message;
        app.exit(71);
    });
    QObject::connect(&service, &TankoyomiChapterService::catalogueReady, &app,
                     [&](const QString &requestId, const QString &sourceSeriesId,
                         const QVariantList &chapters) {
        if (requestId != QLatin1String("catalogue")) return;
        if (chapters.isEmpty()) {
            qCritical().noquote() << "FAIL empty catalogue";
            app.exit(72);
            return;
        }
        QVariantMap sample = chapters.first().toMap();
        double bestNumber = sample.value(QStringLiteral("number")).toDouble();
        for (const QVariant &chapterValue : chapters) {
            const QVariantMap candidate = chapterValue.toMap();
            bool ok = false;
            const double number = candidate.value(QStringLiteral("number")).toDouble(&ok);
            if (ok && number > bestNumber) {
                bestNumber = number;
                sample = candidate;
            }
        }
        const QString chapterId = sample.value(QStringLiteral("id")).toString();
        const QString provider = sample.value(QStringLiteral("source")).toString();
        const QString rowLanguage = sample.value(QStringLiteral("language")).toString();
        const auto parsed = TankoyomiIdentity::parseChapter(chapterId);
        if (!parsed || parsed->providerId != provider || parsed->language != rowLanguage) {
            qCritical().noquote() << "FAIL qualified identity mismatch" << chapterId;
            app.exit(73);
            return;
        }
        if (!expectedProvider.isEmpty() && provider != expectedProvider) {
            qCritical().noquote() << "FAIL provider" << provider << "expected" << expectedProvider;
            app.exit(74);
            return;
        }
        selectedChapterId = chapterId;
        qInfo().noquote() << "CATALOGUE" << rowLanguage << provider
                          << "chapters" << chapters.size() << sourceSeriesId;
        service.fetchPages(QStringLiteral("pages"), selectedChapterId);
    });

    QObject::connect(&service, &TankoyomiChapterService::pagesFailed, &app,
                     [&](const QString &requestId, const QString &message) {
        if (requestId != QLatin1String("pages")) return;
        qCritical().noquote() << "FAIL pages" << message;
        app.exit(75);
    });
    QObject::connect(&service, &TankoyomiChapterService::pagesReady, &app,
                     [&](const QString &requestId, const QVariantList &pages) {
        if (requestId != QLatin1String("pages")) return;
        const QList<PageInfo> normalized =
            MangaPageTransport::normalizeTankoyomiPages(pages, selectedChapterId);
        if (normalized.isEmpty()) {
            qCritical().noquote() << "FAIL empty pages";
            app.exit(76);
            return;
        }

        const PageInfo firstPage = normalized.first();
        QNetworkRequest request =
            MangaPageTransport::requestForPage(firstPage, selectedChapterId);
        if (!request.url().isValid()
            || request.url().scheme() != QLatin1String("https")) {
            qCritical().noquote() << "FAIL bad page url" << request.url();
            app.exit(77);
            return;
        }
        qInfo().noquote() << "IMAGE REQUEST" << request.url().toString().left(120)
                          << "Referer" << request.rawHeader("Referer");

        QNetworkReply *reply = nam.get(request);
        QObject::connect(reply, &QNetworkReply::finished, &app,
                         [&, reply, pageCount = normalized.size()]() {
            const QNetworkReply::NetworkError error = reply->error();
            const QString errorText = reply->errorString();
            const QByteArray body = reply->readAll();
            const QString contentType =
                reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();

            if (body.size() <= 1024 || !looksLikeImage(body, contentType)) {
                qCritical().noquote() << "FAIL image bytes"
                                      << "status" << status
                                      << "error" << int(error) << errorText
                                      << "contentType" << contentType
                                      << "bytes" << body.size();
                app.exit(78);
                return;
            }
            qInfo().noquote() << "PASS service runtime image"
                              << "pages" << pageCount
                              << "status" << status
                              << "contentType" << contentType
                              << "bytes" << body.size();
            app.exit(0);
        });
    });

    timeout.start();
    service.fetchCatalogue(QStringLiteral("catalogue"), query, language);
    return app.exec();
}
