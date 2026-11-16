#include "VaultLibrary.h"

#include "VaultAndroidStorageBridge.h"
#include "VaultConfig.h"
#include "VaultDownloadsRoot.h"
#include "VaultIdentity.h"
#include "VaultIndex.h"
#include "VaultLocation.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QtConcurrentRun>

namespace {

QPointer<VaultLibrary> vaultOwner; // Qt thread only
constexpr auto treeClass = "org/colosseum/vault/VaultDocumentTree";

VaultAndroidStorageBridge::DecodeResult readDocuments(const QStringList& roots,
                                                      const QStringList& ignored)
{
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto rootJson = QJniObject::fromString(QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(roots)).toJson(QJsonDocument::Compact)));
    const auto ignoreJson = QJniObject::fromString(QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(ignored)).toJson(QJsonDocument::Compact)));
    const auto payload = QJniObject::callStaticObjectMethod(
        treeClass, "snapshot",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        context.object<jobject>(), rootJson.object<jstring>(), ignoreJson.object<jstring>());
    return VaultAndroidStorageBridge::decodeSnapshotJson(payload.toString().toUtf8());
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_ColosseumActivity_nativeVaultTreePicked(JNIEnv*, jclass, jstring uri)
{
    const QString path = QJniObject(uri).toString();
    QMetaObject::invokeMethod(QCoreApplication::instance(), [path]() {
        if (vaultOwner)
            vaultOwner->addFolder(path);
    }, Qt::QueuedConnection);
}

void VaultLibrary::installDocumentPicker()
{
    vaultOwner = this;
}

void VaultLibrary::pickDocumentFolder(const QString& previous)
{
    if (!previous.isEmpty() && (!m_config->isRootConfirmed(previous)
                                || !m_documentPermissionLost.contains(previous)))
        return;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([previous]() -> QVariant {
        const auto activity = QNativeInterface::QAndroidApplication::context();
        const auto uri = QJniObject::fromString(previous);
        activity.callMethod<void>("pickVaultTree", "(Ljava/lang/String;)V", uri.object<jstring>());
        return {};
    });
}

void VaultLibrary::addDocumentRoot(const QString& path)
{
    if (!VaultLocation::isContentUri(path))
        return;
    m_config->addRoot(path);
    m_config->confirmRoot(path);
    ++m_revision;
    emit changed();
    refreshDocuments();
}

void VaultLibrary::forgetDocumentRoot(const QString& path)
{
    if (!m_index->reconcileRoot(path, {}, {}))
        return;
    m_config->removeRootCompletely(path);
    m_documentRootAvailable.remove(path);
    m_documentPermissionLost.remove(path);
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto uri = QJniObject::fromString(path);
    QJniObject::callStaticMethod<void>(treeClass, "releaseGrant",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object<jobject>(), uri.object<jstring>());
    ++m_revision;
    emit changed();
}

void VaultLibrary::refreshDocuments()
{
    if (m_documentScanInFlight) {
        m_documentScanPending = true;
        return;
    }

    QStringList roots;
    for (const QVariant& value : m_config->roots()) {
        const QString path = value.toMap().value(QStringLiteral("path")).toString();
        if (VaultLocation::isContentUri(path) && m_config->isRootConfirmed(path))
            roots.append(path);
    }
    if (roots.isEmpty())
        return;

    m_documentScanInFlight = true;
    m_documentScanCancelled = false;
    m_scanningRoot.clear();
    m_scanDone = m_scanTotal = 0;
    emit scanProgressChanged();
    setScanning(true);

    using Result = VaultAndroidStorageBridge::DecodeResult;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        const auto decoded = watcher->result();
        watcher->deleteLater();
        const bool superseded = m_documentScanCancelled || m_documentScanPending;
        if (decoded.ok && !superseded) {
            VaultAndroidIndexAdapter adapter(m_index, m_identity);
            bool changed = false;
            const quint64 before = m_index->revision();
            for (const auto& source : decoded.sources) {
                if (!m_config->isRootConfirmed(source.rootUri))
                    continue; // forgotten while the provider query was running
                const bool lost = m_documentPermissionLost.contains(source.rootUri);
                if (source.permissionLost)
                    m_documentPermissionLost.insert(source.rootUri);
                else
                    m_documentPermissionLost.remove(source.rootUri);
                changed |= lost != source.permissionLost
                    || m_documentRootAvailable.value(source.rootUri, true) != source.available;
                m_documentRootAvailable.insert(source.rootUri, source.available);
                if (!source.displayName.isEmpty())
                    changed |= m_config->setRootDisplayName(source.rootUri, source.displayName);
                if (!adapter.applySnapshot(source).ok)
                    qWarning("Could not reconcile Android Vault folder");
            }
            if (changed) {
                ++m_revision;
                emit this->changed();
            }
            if (m_index->revision() != before)
                emit documentScanPublished();
        } else if (!decoded.ok && !superseded) {
            qWarning().noquote() << "Android Vault scan:" << decoded.error;
        }
        m_documentScanInFlight = false;
        setScanning(false);
        if (std::exchange(m_documentScanPending, false))
            refreshDocuments();
    });
    watcher->setFuture(QtConcurrent::run(readDocuments, roots, m_config->scanIgnore()));
}

void VaultLibrary::publishAndroidDownloads()
{
    if (!m_downloadsRoot || m_downloadsRootPath.isEmpty())
        return;

    QList<VaultIndex::FileRow> rows;
    if (m_config->isRootConfirmed(m_downloadsRootPath))
        rows = m_downloadsRoot->rowsForDownloads(m_downloadsRootPath);
    QSet<QString> ids;
    QList<VaultIdentity::FileFacts> facts;
    for (const auto& row : std::as_const(rows))
        facts.append({row.path, row.size, row.mtimeMs});
    const auto observed = m_identity->observeFiles(facts);
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows[i].id.isEmpty())
            rows[i].id = observed[i].value(QStringLiteral("id")).toString();
        if (!rows[i].id.isEmpty())
            ids.insert(rows[i].id);
    }
    rows.removeIf([](const auto& row) { return row.id.isEmpty(); });
    const quint64 before = m_index->revision();
    if (m_index->reconcileRoot(m_downloadsRootPath, ids, rows)
            && m_index->revision() != before) {
        emit documentScanPublished();
    }
}
