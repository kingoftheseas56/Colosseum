#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiConfigurationStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

static int failures = 0;
static void check(bool ok, const char *message)
{
    qInfo().noquote() << (ok ? "  ok  " : "  FAIL") << message;
    if (!ok) ++failures;
}

static QStringList ids(const QList<TankoyomiProviderDescriptor> &providers)
{
    QStringList out;
    for (const auto &provider : providers) out.append(provider.id);
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary service configuration directory exists");

    const TankoyomiProviderRegistry registry = TankoyomiProviderRegistry::fromResource();
    check(registry.isValid(), "embedded Tankoyomi manifest is valid");
    TankoyomiConfigurationStore store(registry, temp.filePath(QStringLiteral("service.ini")));
    QNetworkAccessManager nam;
    TankoyomiChapterService service(&nam, &store);

    QString language;
    QList<TankoyomiProviderDescriptor> manifest;
    for (const QVariant &rowValue : registry.languages()) {
        const QString candidate = rowValue.toMap().value(QStringLiteral("code")).toString();
        const auto providers = registry.providersForLanguage(candidate);
        if (providers.size() > 1) {
            language = candidate;
            manifest = providers;
            break;
        }
    }
    check(!manifest.isEmpty(), "embedded multi-provider language is available");
    if (!manifest.isEmpty()) {
        const QString first = manifest.first().id;
        check(!service.candidateProviders(QString()).isEmpty(),
              "empty service language resolves the persisted default language");
        check(store.setDefaultLanguage(language),
              "service policy accepts a configured default language");
        check(ids(service.candidateProviders(QString())) == ids(service.candidateProviders(language)),
              "empty service language follows the current configured default");
        check(service.candidateProviders(language).size() == manifest.size(),
              "service initially resolves every enabled manifest provider");
        check(store.setProviderEnabled(language, first, false),
              "service policy can disable the manifest-first provider");
        const auto afterDisable = service.candidateProviders(language);
        check(!afterDisable.isEmpty() && !ids(afterDisable).contains(first),
              "service resolves the current enabled overlay without restart");
        check(store.setProviderEnabled(language, first, true),
              "service policy can re-enable a provider");
        check(store.moveProvider(language, first, manifest.size() - 1),
              "service policy can move a provider to the end");
        const auto reordered = service.candidateProviders(language);
        check(!reordered.isEmpty() && reordered.last().id == first,
              "service resolves the current configured provider order");
        check(service.candidateProviders(QStringLiteral("zz")).isEmpty(),
              "service never crosses language boundaries for unsupported requests");
    }

    if (failures) return 1;
    qInfo() << "PASS — Tankoyomi service configuration contract";
    return 0;
}
