#include "engine/TankoyomiConfigurationStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

static int failures = 0;
static void check(bool ok, const char *message)
{
    qInfo().noquote() << (ok ? "  ok  " : "  FAIL") << message;
    if (!ok) ++failures;
}

static QByteArray validManifest()
{
    return R"JSON({
      "defaultLanguage":"en",
      "fallbackPolicy":"same-language-only",
      "languages":[
        {"code":"en","label":"English","countryCode":"GB","providers":[
          {"id":"first","name":"First","entry":"languages/en/first.js","priority":1,"enabled":true,"allowedHosts":["first.example"]},
          {"id":"second","name":"Second","entry":"languages/en/second.js","priority":2,"enabled":true,"allowedHosts":["second.example"]},
          {"id":"manifest-off","name":"Manifest Off","entry":"languages/en/off.js","priority":3,"enabled":false,"allowedHosts":["off.example"]}
        ]},
        {"code":"pt","label":"Português","countryCode":"BR","providers":[
          {"id":"pt-one","name":"PT One","entry":"languages/pt/one.js","priority":1,"enabled":true,"allowedHosts":["pt.example"]}
        ]}
      ]
    })JSON";
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
    check(temp.isValid(), "temporary configuration directory exists");
    const QString settingsPath = temp.filePath(QStringLiteral("tankoyomi.ini"));
    const TankoyomiProviderRegistry registry(validManifest());
    check(registry.isValid(), "configuration fixture registry is valid");

    TankoyomiConfigurationStore store(registry, settingsPath);
    check(store.defaultLanguage() == QStringLiteral("en"), "manifest default language is the initial default");
    check(ids(store.providersForLanguage(QStringLiteral("en")))
              == QStringList{QStringLiteral("first"), QStringLiteral("second")},
          "runtime starts with manifest-enabled providers in manifest order");

    check(store.setDefaultLanguage(QStringLiteral("pt")), "valid default language change is accepted");
    check(store.setProviderEnabled(QStringLiteral("en"), QStringLiteral("manifest-off"), true),
          "manifest-disabled provider can be enabled by configuration");
    check(store.setProviderEnabled(QStringLiteral("en"), QStringLiteral("second"), false),
          "configured provider can be disabled");
    check(store.moveProvider(QStringLiteral("en"), QStringLiteral("manifest-off"), 0),
          "provider can be moved to an absolute rank");
    check(ids(store.providersForLanguage(QStringLiteral("en")))
              == QStringList{QStringLiteral("manifest-off"), QStringLiteral("first")},
          "runtime ladder applies enabled state and configured order");
    check(store.providersForLanguage(QStringLiteral("pt")).size() == 1
              && store.providersForLanguage(QStringLiteral("pt")).first().id == QStringLiteral("pt-one"),
          "English changes do not affect the Portuguese ladder");

    const QVariantList summaries = store.providers(QStringLiteral("en"));
    check(summaries.size() == 3, "all manifest providers remain visible in configuration summaries");
    check(summaries.size() == 3 && summaries.at(0).toMap().value(QStringLiteral("id")).toString()
              == QStringLiteral("manifest-off")
              && !summaries.at(2).toMap().value(QStringLiteral("enabled")).toBool(),
          "summaries expose configured rank and enabled state");
    check(store.languages().size() == 2
              && store.languages().at(0).toMap().value(QStringLiteral("countryCode")).toString()
                     == QStringLiteral("GB"),
          "language summaries preserve manifest-backed country codes");
    check(summaries.size() == 3
              && summaries.at(0).toMap().value(QStringLiteral("allowedHosts")).toStringList()
                     == QStringList{QStringLiteral("off.example")},
          "provider summaries preserve manifest host identity");

    check(store.resetProviderOrder(QStringLiteral("en")), "provider order can be reset");
    check(ids(store.providersForLanguage(QStringLiteral("en")))
              == QStringList{QStringLiteral("first"), QStringLiteral("manifest-off")},
          "reset restores manifest order without re-enabling disabled providers");

    TankoyomiConfigurationStore reopened(registry, settingsPath);
    check(reopened.defaultLanguage() == QStringLiteral("pt"), "default language survives reconstruction");
    check(ids(reopened.providersForLanguage(QStringLiteral("en")))
              == QStringList{QStringLiteral("first"), QStringLiteral("manifest-off")},
          "provider enabled state and reset order survive reconstruction");
    check(!reopened.setDefaultLanguage(QStringLiteral("fr")), "unsupported default language is rejected");
    check(!reopened.setProviderEnabled(QStringLiteral("en"), QStringLiteral("stale"), true),
          "stale provider ids are rejected");
    check(!reopened.moveProvider(QStringLiteral("en"), QStringLiteral("stale"), 0),
          "stale provider ids cannot mutate order");

    check(reopened.setProviderEnabled(QStringLiteral("pt"), QStringLiteral("pt-one"), false),
          "the sole provider can be disabled");
    check(reopened.providersForLanguage(QStringLiteral("pt")).isEmpty(),
          "fully-disabled language yields an empty runtime ladder");
    check(reopened.providersForLanguage(QStringLiteral("fr")).isEmpty(),
          "unsupported language does not fall back to another language");

    const QString corruptPath = temp.filePath(QStringLiteral("corrupt.ini"));
    QSettings corruptSettings(corruptPath, QSettings::IniFormat);
    corruptSettings.setValue(
        QStringLiteral("tankoyomi/configuration"),
        QByteArray(R"JSON({"defaultLanguage":"en","languages":{
          "":{"enabled":{"first":false}},
          "   ":{"enabled":{"second":false}},
          "zz":{"enabled":{"first":false}},
          "en":{"order":["stale","first","second"]}
        }})JSON"));
    corruptSettings.sync();
    TankoyomiConfigurationStore corrupt(registry, corruptPath);
    check(ids(corrupt.providersForLanguage(QStringLiteral("en")))
              == QStringList{QStringLiteral("first"), QStringLiteral("second")},
          "empty, whitespace, and unsupported persisted language keys cannot mutate the default");
    check(corrupt.providers(QStringLiteral("en")).size() == 3,
          "valid persisted language objects still retain all inventory rows");

    const QString malformedPath = temp.filePath(QStringLiteral("malformed.ini"));
    QSettings malformedSettings(malformedPath, QSettings::IniFormat);
    malformedSettings.setValue(QStringLiteral("tankoyomi/configuration"), QByteArray("not-json"));
    malformedSettings.sync();
    TankoyomiConfigurationStore malformed(registry, malformedPath);
    check(malformed.defaultLanguage() == QStringLiteral("en")
              && ids(malformed.providersForLanguage(QStringLiteral("en")))
                  == QStringList{QStringLiteral("first"), QStringLiteral("second")},
          "malformed persisted configuration falls back to manifest defaults");

    if (failures) return 1;
    qInfo() << "PASS — Tankoyomi configuration store contract";
    return 0;
}
