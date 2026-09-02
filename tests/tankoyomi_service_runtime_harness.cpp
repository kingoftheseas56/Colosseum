#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiIdentity.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QVariantMap>

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
        const QVariantMap first = chapters.first().toMap();
        const QString chapterId = first.value(QStringLiteral("id")).toString();
        const QString provider = first.value(QStringLiteral("source")).toString();
        const QString rowLanguage = first.value(QStringLiteral("language")).toString();
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
        qInfo().noquote() << "CATALOGUE" << rowLanguage << provider
                          << "chapters" << chapters.size() << sourceSeriesId;
        service.fetchPages(QStringLiteral("pages"), chapterId);
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
        if (pages.isEmpty()) {
            qCritical().noquote() << "FAIL empty pages";
            app.exit(76);
            return;
        }
        const QString url = pages.first().toMap().value(QStringLiteral("url")).toString();
        if (!url.startsWith(QStringLiteral("https://"))) {
            qCritical().noquote() << "FAIL bad page url" << url;
            app.exit(77);
            return;
        }
        qInfo().noquote() << "PASS service runtime pages" << pages.size() << url.left(120);
        app.exit(0);
    });

    timeout.start();
    service.fetchCatalogue(QStringLiteral("catalogue"), query, language);
    return app.exec();
}
