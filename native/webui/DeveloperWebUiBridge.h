#pragma once

#include <QObject>
#include <QMetaObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class BiblioCatalog;
class CollectionStore;
class ComicsCatalog;
class ExtensionsStore;
class ImdbCatalog;
class MalCatalog;
class ProgressStore;

class DeveloperWebUiBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString resourceUrl READ resourceUrl CONSTANT)
    Q_PROPERTY(QString wallpaper READ wallpaper WRITE setWallpaper NOTIFY wallpaperChanged)

public:
    explicit DeveloperWebUiBridge(
        MalCatalog *malCatalog,
        ComicsCatalog *comicsCatalog,
        BiblioCatalog *biblioCatalog,
        ImdbCatalog *imdbCatalog,
        ExtensionsStore *extensions,
        QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QString resourceUrl() const;
    QString wallpaper() const { return m_wallpaper; }
    void setWallpaper(const QString &wallpaper);

    void bindPersonalStores(ProgressStore *progress, CollectionStore *collection);

    Q_INVOKABLE QVariantMap snapshot(const QString &surface = QStringLiteral("Home"),
                                     const QString &tab = QString()) const;
    Q_INVOKABLE void requestSnapshot(const QString &surface = QStringLiteral("Home"),
                                     const QString &tab = QString());
    Q_INVOKABLE void clientReady();
    Q_INVOKABLE void postAction(const QVariantMap &action);

signals:
    void revisionChanged();
    void wallpaperChanged();
    void snapshotReady(const QVariantMap &snapshot);
    void patchReady(const QVariantMap &patch);
    void invalidAction(const QString &reason);
    void homeRequested();
    void openWorldRequested(const QString &world);
    void worldTabRequested(const QString &world, const QString &tab);
    void openItemRequested(const QString &world, const QVariantMap &item,
                           const QString &intent);
    void resumeRequested(const QString &world, const QVariantMap &item);
    void continueDetailsRequested(const QString &world, const QVariantMap &item);
    void nextUpRequested(const QString &world, const QVariantMap &item);
    void continueSeeAllRequested(const QString &world);
    void seeAllRequested(const QString &world, const QString &tab,
                         const QVariantMap &pin);
    void openUniverseRequested(const QString &extensionId, const QString &name,
                               const QVariantMap &item);
    void openUniverseHallRequested();
    void openVaultRequested();
    void openGenreRequested(const QString &world, const QString &genre);
    void searchOpened();
    void searchClosed();
    void searchRequested(const QString &surface, const QString &query);
    void trackersRequested();
    void wallpaperRequested();
    void accountRequested();
    void windowMinimizeRequested();
    void windowToggleFullscreenRequested();
    void windowCloseRequested();

private:
    QVariantMap homeSnapshot() const;
    QVariantMap tankobanSnapshot(const QString &tab) const;
    QVariantMap biblioSnapshot(const QString &tab) const;
    QVariantMap theatreSnapshot(const QString &tab) const;
    QVariantList universeRows() const;
    QVariantList recent(const QString &kind, int limit) const;
    QVariantList collection(const QString &world) const;

    QVariantMap page(const QString &world, const QString &catalog,
                     int offset, int limit) const;
    QVariantList theatreRows(const QString &tab, int limit) const;
    QVariantList theatreAnimeRows(int limit) const;
    QVariantList biblioExploreSections(int limit) const;

    static QVariantMap normalizeRow(const QVariantMap &row, const QString &world,
                                    const QString &kind = QString());
    static QVariantList normalizeRows(const QVariantList &rows, const QString &world,
                                      const QString &kind = QString());
    static QVariantMap section(const QString &title, const QVariantList &items,
                               const QString &layout = QStringLiteral("rail"),
                               bool seeAll = false, const QVariantMap &pin = {});
    static QString canonicalSurface(const QString &surface);
    static bool isKnownWorld(const QString &world);
    static bool isKnownTab(const QString &world, const QString &tab);
    static QVariantMap actionItem(const QVariantMap &action);
    void bump();
    void emitNativeSearch(const QString &surface, const QString &query);

    MalCatalog *m_malCatalog = nullptr;
    ComicsCatalog *m_comicsCatalog = nullptr;
    BiblioCatalog *m_biblioCatalog = nullptr;
    ImdbCatalog *m_imdbCatalog = nullptr;
    ExtensionsStore *m_extensions = nullptr;
    ProgressStore *m_progress = nullptr;
    CollectionStore *m_collection = nullptr;
    QMetaObject::Connection m_progressChanged;
    QMetaObject::Connection m_collectionChanged;
    QString m_activeSurface = QStringLiteral("Home");
    QString m_activeTab;
    QString m_wallpaper;
    bool m_clientReady = false;
    int m_revision = 0;
};
