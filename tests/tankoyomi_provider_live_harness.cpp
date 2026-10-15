#include "engine/TankoyomiProviderRegistry.h"
#include "engine/TankoyomiScriptProvider.h"
#include "engine/TankoyomiSeriesMatcher.h"
#include "engine/TankoyomiNetworkPolicy.h"
#include "engine/TankoyomiIdentity.h"
#include "engine/MangaPageTransport.h"
#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiConfigurationStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>

namespace {
// Heap-owned image probe: the redirect continuation runs from the event loop
// after the resolved-handler's stack is gone, so all state lives here.
struct ImageProbe : public QObject {
    QNetworkAccessManager &nam;
    QCoreApplication &app;
    QNetworkRequest request;
    QString logHost;
    QString language;
    QString providerId;
    int redirects = 0;
    QTimer deadline{this};
    std::function<void()> step;
    ImageProbe(QNetworkAccessManager &n, QCoreApplication &a, QObject *parent)
        : QObject(parent), nam(n), app(a) {}
};
} // namespace

// Service-level live probe: the real TankoyomiChapterService path (registry
// resolution, same-language ladder, qualified identity, page transport) with an
// isolated temporary configuration store, so user settings are never touched.
// --disable-first removes the language's first manifest-enabled provider to
// force the same-language fallback ladder.
static int runServiceMode(QCoreApplication &app)
{
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        qCritical().noquote() << "usage: --service <language> <query> [--disable-first]";
        return 64;
    }
    const QString requestedLanguage = args.at(2);
    const QString query = args.at(3);
    const bool disableFirst = args.contains(QLatin1String("--disable-first"));

    const TankoyomiProviderRegistry registry = TankoyomiProviderRegistry::fromResource();
    if (!registry.isValid()) {
        qCritical().noquote() << "FAIL registry" << registry.error();
        return 65;
    }
    const std::optional<QString> resolved = registry.resolveLanguage(requestedLanguage);
    if (!resolved.has_value()) {
        qCritical().noquote() << "FAIL language not installed" << requestedLanguage;
        return 66;
    }
    const QString language = resolved.value();

    QNetworkAccessManager nam;
    QTemporaryDir configurationDir;
    if (!configurationDir.isValid()) return 2;
    TankoyomiConfigurationStore configuration(registry,
                                              configurationDir.filePath(QStringLiteral("service.ini")));
    QString disabledProvider;
    if (disableFirst) {
        const QList<TankoyomiProviderDescriptor> providers = registry.providersForLanguage(language);
        if (!providers.isEmpty()) {
            disabledProvider = providers.first().id;
            configuration.setProviderEnabled(language, disabledProvider, false);
            qInfo().noquote() << "DISABLED" << language << disabledProvider;
        }
    }
    TankoyomiChapterService service(&nam, &configuration);
    QString serviceQualifiedChapterId;

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(120000);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&]() {
        qCritical().noquote() << "FAIL service timeout" << language << query;
        app.exit(70);
    });

    QObject::connect(&service, &TankoyomiChapterService::catalogueFailed, &app,
                     [&](const QString &, const QString &message) {
        qCritical().noquote() << "FAIL catalogue" << message;
        app.exit(71);
    });
    QObject::connect(&service, &TankoyomiChapterService::pagesFailed, &app,
                     [&](const QString &, const QString &message) {
        qCritical().noquote() << "FAIL pages" << message;
        app.exit(72);
    });
    QObject::connect(&service, &TankoyomiChapterService::catalogueReady, &app,
                     [&](const QString &, const QString &sourceSeriesId, const QVariantList &chapters) {
        if (chapters.isEmpty() || !TankoyomiIdentity::isQualifiedChapter(
                                      chapters.first().toMap().value(QStringLiteral("id")).toString())) {
            qCritical().noquote() << "FAIL catalogue rows" << chapters.size();
            app.exit(73);
            return;
        }
        const QVariantMap chapter = chapters.first().toMap();
        serviceQualifiedChapterId = chapter.value(QStringLiteral("id")).toString();
        qInfo().noquote() << "CATALOGUE" << language << chapters.size()
                          << chapter.value(QStringLiteral("source")).toString()
                          << chapter.value(QStringLiteral("language")).toString();
        if (!disabledProvider.isEmpty()
            && chapter.value(QStringLiteral("source")).toString() == disabledProvider) {
            qCritical().noquote() << "FAIL disabled provider still served the catalogue";
            app.exit(74);
            return;
        }
        service.fetchPages(QStringLiteral("pages"), serviceQualifiedChapterId);
    });
    QObject::connect(&service, &TankoyomiChapterService::pagesReady, &app,
                     [&](const QString &, const QVariantList &rows) {
        if (rows.isEmpty()) {
            qCritical().noquote() << "FAIL empty pages";
            app.exit(74);
            return;
        }
        // Normalize the service rows through the page transport exactly like
        // the production bridge does for the qualified chapter it received.
        const QList<PageInfo> pages = MangaPageTransport::normalizeTankoyomiPages(
            rows, serviceQualifiedChapterId);
        if (pages.isEmpty()) {
            qCritical().noquote() << "FAIL page transport rejected every service row";
            app.exit(77);
            return;
        }
        const PageInfo page = pages.first();
        auto *probe = new ImageProbe(nam, app, &app);
        probe->request = MangaPageTransport::requestForPage(page, serviceQualifiedChapterId);
        probe->logHost = QUrl(page.imageUrl).host();
        probe->language = language;
        probe->providerId = QStringLiteral("service");
        probe->deadline.setSingleShot(true);
        probe->deadline.setInterval(30000);
        QObject::connect(&probe->deadline, &QTimer::timeout, probe, [probe]() {
            qCritical().noquote() << "FAIL image fetch timeout";
            probe->app.exit(78);
        });
        probe->step = [probe]() {
            QNetworkReply *reply = probe->nam.get(probe->request);
            QObject::connect(reply, &QNetworkReply::finished, probe, [probe, reply]() {
                reply->deleteLater();
                if (!probe->deadline.isActive()) return;
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status >= 300 && status < 400) {
                    const QUrl location = reply->attribute(
                        QNetworkRequest::RedirectionTargetAttribute).toUrl();
                    const QUrl next = probe->request.url().resolved(location);
                    if (++probe->redirects > 5 || location.isEmpty()
                        || !TankoyomiNetworkPolicy::pageUrlAllowedBeforeDns(
                               next, QStringLiteral("public-https"))) {
                        qCritical().noquote() << "FAIL image redirect rejected" << status;
                        probe->app.exit(79);
                        return;
                    }
                    probe->request.setUrl(next);
                    probe->step();
                    return;
                }
                probe->deadline.stop();
                const QByteArray body = reply->readAll();
                const QByteArray contentType = reply->header(
                    QNetworkRequest::ContentTypeHeader).toByteArray();
                bool magicOk = body.startsWith(QByteArrayLiteral("\xff\xd8\xff"))
                    || body.startsWith(QByteArrayLiteral("\x89PNG"))
                    || body.startsWith(QByteArrayLiteral("GIF8"))
                    || body.startsWith(QByteArrayLiteral("RIFF"))
                    || (body.size() >= 8 && body.mid(4, 4) == QByteArrayLiteral("ftyp"));
                if (status != 200 || body.size() <= 1024 || !magicOk
                    || (!contentType.startsWith("image/") && !body.startsWith(QByteArrayLiteral("RIFF")))) {
                    qCritical().noquote() << "FAIL real image bytes" << status << body.size()
                                          << contentType << probe->logHost;
                    probe->app.exit(79);
                    return;
                }
                qInfo().noquote() << "PASS IMAGE" << body.size() << contentType << probe->logHost;
                qInfo().noquote() << "PASS SERVICE" << probe->language;
                probe->app.exit(0);
            });
        };
        probe->deadline.start();
        probe->step();
    });

    timeout.start();
    service.fetchCatalogue(QStringLiteral("catalogue"), query, requestedLanguage);
    return app.exec();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 2 && args.at(1) == QLatin1String("--service"))
        return runServiceMode(app);
    if (args.size() < 4) {
        qCritical().noquote() << "usage: tankoyomi_provider_live_harness <language> <provider> <query>"
                                 " | --service <language> <query> [--disable-first]";
        return 64;
    }
    const QString providerId = args.at(2);
    const QString query = args.at(3);

    const TankoyomiProviderRegistry registry = TankoyomiProviderRegistry::fromResource();
    if (!registry.isValid()) {
        qCritical().noquote() << "FAIL registry" << registry.error();
        return 65;
    }
    // Regional requests like pt-BR resolve to the canonical installed code; an
    // unresolved tag fails the probe instead of silently crossing languages.
    const std::optional<QString> resolved = registry.resolveLanguage(args.at(1));
    if (!resolved.has_value()) {
        qCritical().noquote() << "FAIL language not installed" << args.at(1);
        return 66;
    }
    const QString language = resolved.value();
    // Resolve the explicitly named provider through the complete manifest
    // inventory so a manifest-default-disabled provider stays directly
    // probeable without touching any user configuration.
    std::optional<TankoyomiProviderDescriptor> descriptor;
    for (const TankoyomiProviderDescriptor &entry : registry.allProvidersForLanguage(language)) {
        if (entry.id == providerId) {
            descriptor = entry;
            break;
        }
    }
    if (!descriptor) {
        qCritical().noquote() << "FAIL provider not configured" << language << providerId;
        return 66;
    }

    QNetworkAccessManager nam;
    QObject providerOwner;
    auto *provider = new TankoyomiScriptProvider(
        descriptor->id, descriptor->language, descriptor->resourcePath,
        descriptor->allowedHosts, &nam, &providerOwner);
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
            TankoyomiSeriesQuery identityQuery;
            identityQuery.title = query;
            const auto matched = TankoyomiSeriesMatcher::match(identityQuery, rows, descriptor->titleDecorators);
            if (!matched.accepted) {
                qCritical().noquote() << "FAIL series identity" << matched.reason;
                app.exit(72);
                return;
            }
            selectedSeries = matched.row;
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
            // A URL-shape-only PASS is not provider qualification: qualify the
            // selected chapter through TankoyomiIdentity, normalize the provider
            // rows through MangaPageTransport, build the provider-aware page
            // request, and require real image bytes with a valid magic number.
            const QString qualifiedId = TankoyomiIdentity::qualifyChapter(
                language, descriptor->id, selectedChapter);
            if (qualifiedId.isEmpty()) {
                qCritical().noquote() << "FAIL qualified identity";
                app.exit(76);
                return;
            }
            const QList<PageInfo> pages = MangaPageTransport::normalizeTankoyomiPages(rows, qualifiedId);
            if (pages.isEmpty()) {
                qCritical().noquote() << "FAIL page transport rejected every page row";
                app.exit(77);
                return;
            }
            const PageInfo page = pages.first();
            // Follow image redirects manually exactly like the production
            // downloader: every hop must stay a public HTTPS URL, the chain is
            // bounded, and the referer survives the hop.
            auto *probe = new ImageProbe(nam, app, &app);
            probe->request = MangaPageTransport::requestForPage(page, qualifiedId);
            probe->logHost = QUrl(page.imageUrl).host();
            probe->language = language;
            probe->providerId = providerId;
            probe->deadline.setSingleShot(true);
            probe->deadline.setInterval(30000);
            QObject::connect(&probe->deadline, &QTimer::timeout, probe, [probe]() {
                qCritical().noquote() << "FAIL image fetch timeout";
                probe->app.exit(78);
            });
            probe->step = [probe]() {
                QNetworkReply *reply = probe->nam.get(probe->request);
                QObject::connect(reply, &QNetworkReply::finished, probe, [probe, reply]() {
                    reply->deleteLater();
                    if (!probe->deadline.isActive()) return;
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status >= 300 && status < 400) {
                        const QUrl location = reply->attribute(
                            QNetworkRequest::RedirectionTargetAttribute).toUrl();
                        const QUrl next = probe->request.url().resolved(location);
                        if (++probe->redirects > 5 || location.isEmpty()
                            || !TankoyomiNetworkPolicy::pageUrlAllowedBeforeDns(
                                   next, QStringLiteral("public-https"))) {
                            qCritical().noquote() << "FAIL image redirect rejected" << status
                                                  << next.toString();
                            probe->app.exit(79);
                            return;
                        }
                        probe->request.setUrl(next);
                        probe->step();
                        return;
                    }
                    probe->deadline.stop();
                    const QByteArray body = reply->readAll();
                    const QByteArray contentType = reply->header(
                        QNetworkRequest::ContentTypeHeader).toByteArray();
                    static const QList<QByteArray> magics{
                        QByteArrayLiteral("\xff\xd8\xff"), QByteArrayLiteral("\x89PNG"),
                        QByteArrayLiteral("GIF8"), QByteArrayLiteral("RIFF")};
                    bool magicOk = false;
                    for (const QByteArray &magic : magics)
                        magicOk = magicOk || body.startsWith(magic);
                    // AVIF/HEIF carry their brand in the ISO-BMFF ftyp box.
                    magicOk = magicOk || (body.size() >= 8 && body.mid(4, 4) == QByteArrayLiteral("ftyp"));
                    const bool typeOk = contentType.startsWith("image/")
                        || body.startsWith(QByteArrayLiteral("RIFF"));
                    if (status != 200 || body.size() <= 1024 || !magicOk || !typeOk) {
                        qCritical().noquote() << "FAIL real image bytes" << status << body.size()
                                              << contentType << probe->logHost;
                        probe->app.exit(79);
                        return;
                    }
                    qInfo().noquote() << "PASS IMAGE" << body.size() << contentType << probe->logHost;
                    qInfo().noquote() << "PASS PROVIDER" << probe->language << probe->providerId;
                    probe->app.exit(0);
                });
            };
            probe->deadline.start();
            probe->step();
        }
    });

    timeout.start();
    provider->searchSeries(QStringLiteral("search"), query);
    return app.exec();
}
