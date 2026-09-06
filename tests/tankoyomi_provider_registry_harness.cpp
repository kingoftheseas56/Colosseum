#include "engine/TankoyomiProviderRegistry.h"

#include <QByteArray>
#include <QDebug>

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
          {"id":"second","name":"Second","entry":"languages/en/second.js","priority":2,"enabled":true,"allowedHosts":["second.example"]},
          {"id":"first","name":"First","entry":"languages/en/first.js","priority":1,"enabled":true,"allowedHosts":["first.example","api.first.example"]},
          {"id":"disabled","name":"Disabled by manifest","entry":"languages/en/disabled.js","priority":3,"enabled":false,"allowedHosts":["disabled.example"]}
        ]},
        {"code":"pt","label":"Português (Brasil)","countryCode":"BR","providers":[
          {"id":"pt-one","name":"PT One","entry":"languages/pt/one.js","priority":1,"enabled":true,"allowedHosts":["pt.example"]}
        ]}
      ]
    })JSON";
}

static QByteArray emptyDefaultLanguageManifest()
{
    return R"JSON({
      "defaultLanguage":"en",
      "fallbackPolicy":"same-language-only",
      "languages":[
        {"code":"en","label":"English","providers":[]},
        {"code":"pt","label":"Português (Brasil)","countryCode":"BR","providers":[
          {"id":"pt-one","name":"PT One","entry":"languages/pt/one.js","priority":1,"enabled":true,"allowedHosts":["pt.example"]}
        ]}
      ]
    })JSON";
}
int main()
{
    TankoyomiProviderRegistry registry(validManifest());
    check(registry.isValid(), "valid manifest is accepted");
    check(registry.defaultLanguage() == QStringLiteral("en"), "default language is retained");
    check(TankoyomiProviderRegistry::normalizeLanguage(QStringLiteral("pt-BR")) == QStringLiteral("pt"),
          "regional language normalizes to base language");

    const auto english = registry.providersForLanguage(QStringLiteral("en"));
    check(english.size() == 2, "legacy provider projection keeps manifest-enabled providers only");
    check(english.size() == 2 && english.at(0).id == QStringLiteral("first")
          && english.at(1).id == QStringLiteral("second"),
          "legacy providers are returned in manifest priority order");
    const auto inventory = registry.allProvidersForLanguage(QStringLiteral("en"));
    check(inventory.size() == 3 && inventory.at(0).id == QStringLiteral("first")
          && inventory.at(1).id == QStringLiteral("second")
          && inventory.at(2).id == QStringLiteral("disabled")
          && !inventory.at(2).manifestEnabled,
          "inventory projection includes default-disabled providers in manifest order");
    check(registry.providersForLanguage(QString()).size() == 2,
          "empty language uses the configured default");
    check(registry.providersForLanguage(QStringLiteral("fr")).isEmpty(),
          "explicit unsupported language has no cross-language fallback");
    check(registry.providersForLanguage(QStringLiteral("pt-BR")).size() == 1,
          "regional request resolves the matching base-language providers");

    const auto first = registry.provider(QStringLiteral("en"), QStringLiteral("first"));
    check(first.has_value() && first->resourcePath == QStringLiteral(":/tankoyomi/languages/en/first.js"),
          "provider entry becomes an embedded resource path");
    check(first.has_value() && first->allowedHosts.contains(QStringLiteral("api.first.example")),
          "provider host allowlist survives parsing");
    check(!registry.provider(QStringLiteral("en"), QStringLiteral("disabled")).has_value(),
          "legacy provider lookup excludes manifest-disabled providers");
    check(registry.allProvidersForLanguage(QStringLiteral("en")).size() == 3,
          "inventory lookup remains available for configuration ownership");
    QByteArray missingHosts = validManifest();
    missingHosts.replace("\"allowedHosts\":[\"pt.example\"]", "\"allowedHosts\":[]");
    TankoyomiProviderRegistry missingHostsRegistry(missingHosts);
    check(!missingHostsRegistry.isValid(), "empty host allowlist fails closed");

    QByteArray unsafeEntry = validManifest();
    unsafeEntry.replace("languages/pt/one.js", "../outside.js");
    TankoyomiProviderRegistry unsafeEntryRegistry(unsafeEntry);
    check(!unsafeEntryRegistry.isValid(), "unsafe provider entry path fails closed");

    QByteArray duplicate = validManifest();
    duplicate.replace("\"id\":\"second\"", "\"id\":\"first\"");
    TankoyomiProviderRegistry duplicateRegistry(duplicate);
    check(!duplicateRegistry.isValid(), "duplicate provider id inside a language fails closed");

    const QVariantList languages = registry.languages();
    check(languages.size() == 2
          && languages.at(0).toMap().value(QStringLiteral("code")).toString() == QStringLiteral("en")
          && languages.at(1).toMap().value(QStringLiteral("code")).toString() == QStringLiteral("pt"),
          "registry projects ordered language metadata for QML");
    check(languages.size() == 2
          && languages.at(0).toMap().value(QStringLiteral("countryCode")).toString() == QStringLiteral("GB")
          && languages.at(1).toMap().value(QStringLiteral("countryCode")).toString() == QStringLiteral("BR"),
          "registry projects manifest-backed country codes for QML");

    TankoyomiProviderRegistry emptyDefault(emptyDefaultLanguageManifest());
    check(!emptyDefault.isValid(), "manifest default language with no inventory providers fails closed");

    if (failures) return 1;
    qInfo() << "PASS — Tankoyomi provider registry contract";
    return 0;
}
