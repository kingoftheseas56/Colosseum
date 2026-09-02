#include "engine/TankoyomiProviderRegistry.h"
#include "engine/TankoyomiScriptProvider.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QVariantMap>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        qCritical().noquote() << "usage: tankoyomi_provider_live_harness <language> <provider> <query>";
        return 64;
    }
    const QString language = TankoyomiProviderRegistry::normalizeLanguage(args.at(1));
    const QString providerId = args.at(2);
    const QString query = args.at(3);

    const TankoyomiProviderRegistry registry = TankoyomiProviderRegistry::fromResource();
    if (!registry.isValid()) {
        qCritical().noquote() << "FAIL registry" << registry.error();
        return 65;
    }
    const auto descriptor = registry.provider(language, providerId);
    if (!descriptor) {
        qCritical().noquote() << "FAIL provider not configured" << language << providerId;
        return 66;
    }

    QObject providerOwner;
    auto *provider = new TankoyomiScriptProvider(
        descriptor->id, descriptor->language, descriptor->resourcePath,
        descriptor->allowedHosts, &providerOwner);
    if (!provider->isReady()) {
        qCritical().noquote() << "FAIL provider load" << provider->loadError();
        return 67;
    }

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(60000);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&]() {
        qCritical().noquote() << "FAIL timeout" << language << providerId << query;
        app.exit(70);
    });

    QVariantMap selectedSeries;
    QVariantMap selectedChapter;
    QObject::connect(provider, &TankoyomiScriptProvider::failed, &app,
                     [&](const QString &token, const QString &message) {
        qCritical().noquote() << "FAIL" << token << message;
        app.exit(71);
    });

    QObject::connect(provider, &TankoyomiScriptProvider::resolved, &app,
                     [&](const QString &token, const QVariant &value) {
        if (token == QLatin1String("search")) {
            const QVariantList rows = value.toList();
            if (rows.isEmpty()) {
                qCritical().noquote() << "FAIL empty search";
                app.exit(72);
                return;
            }
            selectedSeries = rows.first().toMap();
            const QString wanted = query.trimmed();
            for (const QVariant &candidateValue : rows) {
                const QVariantMap candidate = candidateValue.toMap();
                const QString candidateTitle = candidate.value(QStringLiteral("title")).toString().trimmed();
                if (candidateTitle.compare(wanted, Qt::CaseInsensitive) == 0) {
                    selectedSeries = candidate;
                    break;
                }
            }
            qInfo().noquote() << "SEARCH" << providerId << rows.size()
                              << selectedSeries.value(QStringLiteral("title")).toString();
            provider->getChapters(QStringLiteral("chapters"), selectedSeries);
            return;
        }

        if (token == QLatin1String("chapters")) {
            const QVariantList rows = value.toList();
            if (rows.isEmpty()) {
                qCritical().noquote() << "FAIL empty chapters";
                app.exit(73);
                return;
            }
            selectedChapter = rows.first().toMap();
            for (const QVariant &chapterValue : rows) {
                const QVariantMap chapter = chapterValue.toMap();
                if (chapter.value(QStringLiteral("number")).toDouble() >= 1.0) {
                    selectedChapter = chapter;
                    break;
                }
            }
            qInfo().noquote() << "CHAPTERS" << providerId << rows.size()
                              << selectedChapter.value(QStringLiteral("id")).toString();
            provider->getPages(QStringLiteral("pages"), selectedChapter);
            return;
        }

        if (token == QLatin1String("pages")) {
            const QVariantList rows = value.toList();
            if (rows.isEmpty()) {
                qCritical().noquote() << "FAIL empty pages";
                app.exit(74);
                return;
            }
            const QVariantMap first = rows.first().toMap();
            const QString url = first.value(QStringLiteral("url")).toString();
            if (!url.startsWith(QStringLiteral("https://"))) {
                qCritical().noquote() << "FAIL bad page url" << url;
                app.exit(75);
                return;
            }
            qInfo().noquote() << "PASS PROVIDER" << language << providerId
                              << "pages" << rows.size() << url.left(100);
            QTimer::singleShot(0, &app, [&app]() { app.exit(0); });
        }
    });

    timeout.start();
    provider->searchSeries(QStringLiteral("search"), query);
    return app.exec();
}
