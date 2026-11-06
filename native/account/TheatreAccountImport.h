#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class CollectionStore;
class ProgressStore;
class HistoryStore;
class ExtensionsStore;

// A preview contains owner-shaped facts, never credentials or raw provider
// envelopes. Unresolved and skipped rows are counted but cannot be applied.
class TheatreAccountImport final : public QObject {
public:
    enum class Category { Library, Movie, Episode, Addon, Progress };
    enum class Disposition { Matched, Unresolved, Existing, Skipped };

    struct Item {
        Category category;
        QVariantMap record;
        Disposition disposition = Disposition::Matched;
    };

    struct Result {
        bool committed = false;
        int changed = 0;
        int addonFailures = 0;
    };

    using Completion = std::function<void(Result)>;

    explicit TheatreAccountImport(QObject *parent = nullptr);
    void bind(CollectionStore *collection, ProgressStore *progress,
              HistoryStore *history, ExtensionsStore *extensions);
    void setExtensions(ExtensionsStore *extensions);
    void cancel();
    QVariantList preview(const QList<Item> &items) const;
    bool apply(const QList<Item> &items, Completion completion);

private:
    struct Plan;
    struct Applying;

    Plan makePlan(const QList<Item> &items) const;
    Disposition disposition(const Item &item, const QVariantMap &localLibrary) const;
    void applyNext(const std::shared_ptr<Applying> &batch);
    void applyAddon(const std::shared_ptr<Applying> &batch, const Item &item);
    void finish(const std::shared_ptr<Applying> &batch, bool committed);

    QPointer<CollectionStore> m_collection;
    QPointer<ProgressStore> m_progress;
    QPointer<HistoryStore> m_history;
    QPointer<ExtensionsStore> m_extensions;
    QMetaObject::Connection m_addonInstalled;
    QMetaObject::Connection m_addonFailed;
    quint64 m_generation = 0;
    bool m_applying = false;
};
