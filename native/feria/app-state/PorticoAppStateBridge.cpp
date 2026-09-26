#include "PorticoAppStateBridge.h"

#include <QHash>
#include <QMetaProperty>
#include <QSet>
#include <QVariant>

namespace {

const QStringList &porticoProviderIds()
{
    static const QStringList ids = {
        QStringLiteral("netflix"), QStringLiteral("prime"),
        QStringLiteral("hbomax"), QStringLiteral("disney"),
        QStringLiteral("appletv"), QStringLiteral("crunchyroll"),
        QStringLiteral("youtube"), QStringLiteral("hulu"),
        QStringLiteral("mubi"), QStringLiteral("spotify"),
        QStringLiteral("ytmusic"), QStringLiteral("applemusic"),
        QStringLiteral("kindle"), QStringLiteral("playbooks"),
        QStringLiteral("mangaplus"), QStringLiteral("viz"),
        QStringLiteral("webtoon"), QStringLiteral("dcui"),
        QStringLiteral("marvel"), QStringLiteral("kobo"),
        QStringLiteral("applebooks")
    };
    return ids;
}

const QHash<QString, QString> &providerMap()
{
    static const QHash<QString, QString> mapping = [] {
        QHash<QString, QString> value;
        for (const QString &id : porticoProviderIds())
            value.insert(id, id);
        return value;
    }();
    return mapping;
}

QString normalizedKey(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

PorticoAppStateBridge::PorticoAppStateBridge(QObject *parent)
    : QObject(parent)
{
}

QStringList PorticoAppStateBridge::currentPorticoProviderIds()
{
    return porticoProviderIds();
}

QString PorticoAppStateBridge::backendProviderId(const QString &porticoProviderId) const
{
    return providerMap().value(normalizedKey(porticoProviderId));
}

bool PorticoAppStateBridge::isKnownProvider(const QString &porticoProviderId) const
{
    return !backendProviderId(porticoProviderId).isEmpty();
}

void PorticoAppStateBridge::setSettingsApps(const QStringList &apps)
{
    if (apps == m_settingsApps)
        return;

    m_settingsApps = apps;
    emit settingsAppsChanged();
    recompute();
}

void PorticoAppStateBridge::setRuntime(QObject *runtime)
{
    if (m_runtime == runtime)
        return;

    if (m_runtimeDestroyed)
        disconnect(m_runtimeDestroyed);

    m_runtime = runtime;
    if (m_runtime) {
        m_runtimeDestroyed = connect(m_runtime, &QObject::destroyed, this, [this] {
            m_runtime = nullptr;
            emit runtimeChanged();
        });
    }

    emit runtimeChanged();
    syncRuntime();
}

void PorticoAppStateBridge::recompute()
{
    QStringList nextActive;
    QStringList nextUnknown;
    QSet<QString> seenActive;

    for (const QString &entry : m_settingsApps) {
        const QString backendId = backendProviderId(entry);
        if (backendId.isEmpty()) {
            if (!entry.trimmed().isEmpty())
                nextUnknown.push_back(entry);
            continue;
        }
        if (!seenActive.contains(backendId)) {
            seenActive.insert(backendId);
            nextActive.push_back(backendId);
        }
    }

    if (nextUnknown != m_unknownApps) {
        m_unknownApps = nextUnknown;
        emit unknownAppsChanged();
    }

    if (nextActive != m_activeApps) {
        m_activeApps = nextActive;
        emit activeAppsChanged();
        syncRuntime();
    }
}

bool PorticoAppStateBridge::syncRuntime()
{
    if (!m_runtime)
        return false;

    const QMetaObject *meta = m_runtime->metaObject();
    const int index = meta->indexOfProperty("activeApps");
    if (index < 0)
        return false;

    const QMetaProperty property = meta->property(index);
    if (!property.isWritable())
        return false;

    if (property.read(m_runtime).toStringList() == m_activeApps)
        return true;

    return property.write(m_runtime, QVariant::fromValue(m_activeApps));
}
