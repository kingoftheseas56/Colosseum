#include "VaultConfig.h"

#include "VaultStoreIo.h"

#include <QDir>
#include <QJsonArray>
#include <QVariantMap>

VaultConfig::VaultConfig(QString vaultDir, QObject* parent)
    : QObject(parent), m_dir(std::move(vaultDir))
{
    load();
}

QString VaultConfig::norm(const QString& path)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

void VaultConfig::load()
{
    m_doc = VaultStoreIo::load(m_dir, QStringLiteral("config.json"), &m_recovered);
    ensureShape();
}

void VaultConfig::ensureShape()
{
    if (!m_doc.contains(QStringLiteral("version")))
        m_doc.insert(QStringLiteral("version"), 1);
    if (!m_doc.value(QStringLiteral("roots")).isArray())
        m_doc.insert(QStringLiteral("roots"), QJsonArray());
    if (!m_doc.value(QStringLiteral("scanIgnore")).isArray())
        m_doc.insert(QStringLiteral("scanIgnore"), QJsonArray());
    if (!m_doc.value(QStringLiteral("hidden")).isArray())
        m_doc.insert(QStringLiteral("hidden"), QJsonArray());
    if (!m_doc.value(QStringLiteral("kinds")).isObject())
        m_doc.insert(QStringLiteral("kinds"), QJsonObject());
}

void VaultConfig::persist()
{
    VaultStoreIo::save(m_dir, QStringLiteral("config.json"), m_doc);
    ++m_revision;
    emit changed();
}

int VaultConfig::rootIndex(const QString& normPath) const
{
    const QJsonArray roots = m_doc.value(QStringLiteral("roots")).toArray();
    for (int i = 0; i < roots.size(); ++i) {
        if (roots.at(i).toObject().value(QStringLiteral("path")).toString() == normPath)
            return i;
    }
    return -1;
}

QVariantList VaultConfig::roots() const
{
    QVariantList out;
    const QJsonArray roots = m_doc.value(QStringLiteral("roots")).toArray();
    for (const auto& v : roots) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m[QStringLiteral("path")] = o.value(QStringLiteral("path")).toString();
        m[QStringLiteral("confirmed")] = o.value(QStringLiteral("confirmed")).toBool();
        m[QStringLiteral("addedAtMs")] =
            o.value(QStringLiteral("addedAtMs")).toVariant().toLongLong();
        out.append(m);
    }
    return out;
}

bool VaultConfig::hasRoot(const QString& path) const
{
    return rootIndex(norm(path)) >= 0;
}

bool VaultConfig::isRootConfirmed(const QString& path) const
{
    const int i = rootIndex(norm(path));
    if (i < 0)
        return false;
    return m_doc.value(QStringLiteral("roots")).toArray().at(i).toObject()
        .value(QStringLiteral("confirmed")).toBool();
}

void VaultConfig::addRoot(const QString& path, qint64 addedAtMs)
{
    const QString n = norm(path);
    if (rootIndex(n) >= 0)
        return;
    QJsonArray roots = m_doc.value(QStringLiteral("roots")).toArray();
    QJsonObject o;
    o.insert(QStringLiteral("path"), n);
    o.insert(QStringLiteral("confirmed"), false);
    o.insert(QStringLiteral("addedAtMs"), addedAtMs);
    roots.append(o);
    m_doc.insert(QStringLiteral("roots"), roots);
    persist();
}

void VaultConfig::confirmRoot(const QString& path)
{
    const int i = rootIndex(norm(path));
    if (i < 0)
        return;
    QJsonArray roots = m_doc.value(QStringLiteral("roots")).toArray();
    QJsonObject o = roots.at(i).toObject();
    o.insert(QStringLiteral("confirmed"), true);
    roots.replace(i, o);
    m_doc.insert(QStringLiteral("roots"), roots);
    persist();
}

void VaultConfig::removeRoot(const QString& path)
{
    const int i = rootIndex(norm(path));
    if (i < 0)
        return;
    QJsonArray roots = m_doc.value(QStringLiteral("roots")).toArray();
    roots.removeAt(i);
    m_doc.insert(QStringLiteral("roots"), roots);
    persist();
}

void VaultConfig::setKind(const QString& subtreePath, const QString& kind)
{
    QJsonObject kinds = m_doc.value(QStringLiteral("kinds")).toObject();
    kinds.insert(norm(subtreePath), kind);
    m_doc.insert(QStringLiteral("kinds"), kinds);
    persist();
}

QString VaultConfig::kindFor(const QString& subtreePath) const
{
    return m_doc.value(QStringLiteral("kinds")).toObject()
        .value(norm(subtreePath)).toString();
}

QVariantMap VaultConfig::kindOverrides() const
{
    // Keys are already normalized (setKind stores norm(subtreePath)); the census
    // normalizes its subtree the same way before lookup.
    return m_doc.value(QStringLiteral("kinds")).toObject().toVariantMap();
}

QStringList VaultConfig::scanIgnore() const
{
    QStringList out;
    const QJsonArray a = m_doc.value(QStringLiteral("scanIgnore")).toArray();
    for (const auto& v : a)
        out.append(v.toString());
    return out;
}

void VaultConfig::setScanIgnore(const QStringList& needles)
{
    QJsonArray a;
    for (const QString& n : needles)
        a.append(n);
    m_doc.insert(QStringLiteral("scanIgnore"), a);
    persist();
}

bool VaultConfig::isHidden(const QString& fileId) const
{
    const QJsonArray a = m_doc.value(QStringLiteral("hidden")).toArray();
    for (const auto& v : a)
        if (v.toString() == fileId)
            return true;
    return false;
}

void VaultConfig::setHidden(const QString& fileId, bool hidden)
{
    QJsonArray a = m_doc.value(QStringLiteral("hidden")).toArray();
    bool present = false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).toString() == fileId) {
            present = true;
            if (!hidden) {
                a.removeAt(i);
                m_doc.insert(QStringLiteral("hidden"), a);
                persist();
            }
            return;
        }
    }
    if (hidden && !present) {
        a.append(fileId);
        m_doc.insert(QStringLiteral("hidden"), a);
        persist();
    }
}
