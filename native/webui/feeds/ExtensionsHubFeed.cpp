// page.extensionsHub — Chain and House (CONTRACT §16). The pages are Hemanth's own designs; this feed only
// tells them which of the add-ons they show are installed, and switches them on or off. Installed means the
// ExtensionsStore row exists and is enabled; install/uninstall flip `enabled`, so nothing is ever deleted.
#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "../ColosseumWebBridge.h"
#include "../../engine/ExtensionsStore.h"

#include <QVariantList>
#include <QVariantMap>

namespace {

// Page keys → ExtensionsStore ids (native/engine/ExtensionsStore.cpp seed roster).
struct Addon { const char *key; const char *id; };
constexpr Addon kAddons[] = {
    {"torrentio",    "com.stremio.torrentio.addon"},
    {"nyaa",         "colosseum.well.nyaa"},
    {"tankoyomi",    "colosseum.well.tankoyomi"},
    {"getcomics",    "colosseum.well.getcomics.issues"},
    {"tankorent",    "colosseum.well.indexers"},
    {"libgen",       "colosseum.well.libgen"},
    {"audiobookbay", "colosseum.well.audiobookbay"},
};

QString idFor(const QString &key)
{
    for (const Addon &a : kAddons)
        if (key == QLatin1String(a.key)) return QString::fromLatin1(a.id);
    return {};
}

ExtensionsStore *store(ColosseumWebBridge &bridge)
{
    return qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions")));
}

QVariantMap section(const QString &state, const QVariantMap &addons = {})
{
    QVariantMap out{{QStringLiteral("id"), QStringLiteral("extensionsHub.addons")},
                    {QStringLiteral("index"), 0},
                    {QStringLiteral("title"), QString()},
                    {QStringLiteral("layout"), QStringLiteral("custom")},
                    {QStringLiteral("state"), state},
                    {QStringLiteral("items"), QVariantList{}}};
    if (state == QLatin1String("ready"))
        out.insert(QStringLiteral("data"), QVariantMap{{QStringLiteral("schema"), QStringLiteral("extensions.addons")},
                                                       {QStringLiteral("addons"), addons}});
    if (state == QLatin1String("error"))
        out.insert(QStringLiteral("error"), QStringLiteral("Extensions are unavailable for this profile."));
    return out;
}

bool valid(const QVariantMap &params)
{
    // The only param is the page's own tab; the feed is the same for every tab.
    for (auto it = params.begin(); it != params.end(); ++it)
        if (it.key() != QLatin1String("tab")) return false;
    return true;
}

QVariantList initial(const QVariantMap &) { return {section(QStringLiteral("loading"))}; }

void capture(ColosseumWebBridge &bridge, FeedContext &context)
{
    // GUI thread: copy the store's rows; the worker only reads this snapshot.
    if (ExtensionsStore *s = store(bridge))
        context.nativeSnapshot.insert(QStringLiteral("installed"), s->installed());
    else
        context.nativeSnapshot.insert(QStringLiteral("missing"), true);
}

QVariantList build(const FeedContext &context)
{
    if (context.nativeSnapshot.value(QStringLiteral("missing")).toBool())
        return {section(QStringLiteral("error"))};
    QVariantMap byId;
    for (const QVariant &row : context.nativeSnapshot.value(QStringLiteral("installed")).toList())
        byId.insert(row.toMap().value(QStringLiteral("id")).toString(), row);
    QVariantMap addons;
    for (const Addon &a : kAddons) {
        const QVariantMap row = byId.value(QString::fromLatin1(a.id)).toMap();
        addons.insert(QString::fromLatin1(a.key), QVariantMap{
            {QStringLiteral("present"), !row.isEmpty()},
            {QStringLiteral("installed"), !row.isEmpty() && row.value(QStringLiteral("enabled")).toBool()}});
    }
    return {section(QStringLiteral("ready"), addons)};
}

QMetaObject::Connection watchStore(QObject *object, QObject *receiver, std::function<void()> changed)
{
    auto *s = qobject_cast<ExtensionsStore *>(object);
    if (!s) return {};
    return QObject::connect(s, &ExtensionsStore::changed, receiver, [changed = std::move(changed)] { changed(); });
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("page.extensionsHub");
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.capture = capture;
    entry.ownerSignals.append({QStringLiteral("Extensions"), watchStore});
    return FeedRegistry::add(std::move(entry));
}();

bool validKey(const QVariantMap &payload)
{
    return payload.size() == 1 && !idFor(payload.value(QStringLiteral("key")).toString()).isEmpty();
}

void setInstalled(ColosseumWebBridge &bridge, const QVariantMap &payload, ActionRegistry::Completion done, bool on)
{
    ExtensionsStore *s = store(bridge);
    if (!s) {
        done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Extensions are unavailable for this profile.")}});
        return;
    }
    const QString id = idFor(payload.value(QStringLiteral("key")).toString());
    QVariantMap row;
    for (const QVariant &v : s->installed())
        if (v.toMap().value(QStringLiteral("id")).toString() == id) { row = v.toMap(); break; }
    if (row.isEmpty()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("This add-on was removed. Add it again from the Store.")}});
        return;
    }
    s->setEnabled(id, on);
    bool nowOn = false;
    for (const QVariant &v : s->installed())
        if (v.toMap().value(QStringLiteral("id")).toString() == id) { nowOn = v.toMap().value(QStringLiteral("enabled")).toBool(); break; }
    if (nowOn == on) done({{QStringLiteral("ok"), true}});
    else done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Colosseum couldn't save that change. Try again.")}});
}

void install(ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion d) { setInstalled(b, p, std::move(d), true); }
void uninstall(ColosseumWebBridge &b, const QVariantMap &p, ActionRegistry::Completion d) { setInstalled(b, p, std::move(d), false); }

const bool installRegistered = ActionRegistry::add({QStringLiteral("page.extensionsHub.install"), validKey, install});
const bool uninstallRegistered = ActionRegistry::add({QStringLiteral("page.extensionsHub.uninstall"), validKey, uninstall});

} // namespace
