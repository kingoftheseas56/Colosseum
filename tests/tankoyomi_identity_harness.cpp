#include "engine/TankoyomiIdentity.h"

#include <QDebug>
#include <QUrl>

static int failures = 0;
static void check(bool ok, const char *message)
{
    qInfo().noquote() << (ok ? "  ok  " : "  FAIL") << message;
    if (!ok) ++failures;
}

int main()
{
    const QVariantMap chapter{
        {QStringLiteral("id"), QStringLiteral("abc:123")},
        {QStringLiteral("url"), QStringLiteral("https://reader.example/chapter/abc?x=1")},
        {QStringLiteral("seriesId"), QStringLiteral("series-9")},
        {QStringLiteral("providerOpaque"), QStringLiteral("needed-later")}
    };
    const QString qualified = TankoyomiIdentity::qualifyChapter(
        QStringLiteral("pt-BR"), QStringLiteral("provider-one"), chapter);
    check(qualified.startsWith(QStringLiteral("tankoyomi:pt:provider-one:chapter:")),
          "qualified id carries canonical language, provider, and chapter kind");

    const auto parsed = TankoyomiIdentity::parseChapter(qualified);
    check(parsed.has_value(), "qualified chapter parses");    if (parsed) {
        check(parsed->language == QStringLiteral("pt"), "identity stores canonical base language");
        check(parsed->providerId == QStringLiteral("provider-one"), "identity stores provider id");
        check(parsed->chapter == chapter, "opaque provider chapter object round-trips exactly");
    }

    check(!TankoyomiIdentity::parseChapter(QStringLiteral("weebcentral-raw-id")).has_value(),
          "legacy raw ids are not mistaken for Tankoyomi identities");
    check(!TankoyomiIdentity::parseChapter(QStringLiteral("tankoyomi:pt:provider-one:chapter:")).has_value(),
          "empty payload is rejected");
    check(!TankoyomiIdentity::parseChapter(QStringLiteral("tankoyomi:pt::chapter:abcd")).has_value(),
          "empty provider id is rejected");
    check(TankoyomiIdentity::qualifyChapter(QString(), QStringLiteral("provider-one"), chapter).isEmpty(),
          "missing language cannot create a qualified identity");
    check(TankoyomiIdentity::qualifyChapter(QStringLiteral("pt"), QString(), chapter).isEmpty(),
          "missing provider cannot create a qualified identity");
    check(TankoyomiIdentity::qualifyChapter(QStringLiteral("pt"), QStringLiteral("provider-one"), {}).isEmpty(),
          "empty chapter payload cannot create a qualified identity");

    if (failures) return 1;
    qInfo().noquote() << "PASS - Tankoyomi qualified identity contract";
    return 0;
}
