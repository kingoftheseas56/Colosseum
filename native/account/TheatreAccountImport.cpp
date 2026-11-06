#include "TheatreAccountImport.h"

#include "CollectionStore.h"
#include "HistoryStore.h"
#include "ProgressStore.h"
#include "engine/ExtensionsStore.h"

#include <QTimer>
#include <QUrl>

#include <array>
#include <utility>

namespace {
using Category = TheatreAccountImport::Category;
using Disposition = TheatreAccountImport::Disposition;
using Item = TheatreAccountImport::Item;
const auto kTheatre = QStringLiteral("theatre");
const auto kVideo = QStringLiteral("video");

qint64 activity(const Item &item) {
    switch (item.category) {
    case Category::Library: return item.record.value("addedAt").toLongLong();
    case Category::Movie:
    case Category::Episode: return item.record.value("latestKnownAt").toLongLong();
    case Category::Progress: return item.record.value("updatedAt").toLongLong();
    case Category::Addon: return 0;
    }
    return 0;
}

QString identity(const Item &item) {
    return item.record.value(item.category == Category::Addon ? "url" : "id").toString();
}

bool newerThan(qint64 remote, qint64 local) {
    return local > 0 && remote > local;
}
}

struct TheatreAccountImport::Plan {
    QList<Item> applicable;
    QVariantList counts;
};

struct TheatreAccountImport::Applying {
    quint64 generation = 0;
    QList<Item> items;
    qsizetype next = 0;
    Result result;
    Completion completion;
};

TheatreAccountImport::TheatreAccountImport(QObject *parent) : QObject(parent) {}

void TheatreAccountImport::bind(CollectionStore *collection, ProgressStore *progress,
    HistoryStore *history, ExtensionsStore *extensions) {
    cancel();
    m_collection = collection;
    m_progress = progress;
    m_history = history;
    m_extensions = extensions;
}

void TheatreAccountImport::setExtensions(ExtensionsStore *extensions) {
    m_extensions = extensions;
}

void TheatreAccountImport::cancel() {
    ++m_generation;
    m_applying = false;
    disconnect(m_addonInstalled);
    disconnect(m_addonFailed);
}

QVariantList TheatreAccountImport::preview(const QList<Item> &items) const {
    return makePlan(items).counts;
}

TheatreAccountImport::Plan TheatreAccountImport::makePlan(const QList<Item> &items) const {
    constexpr std::array categoryNames{"library", "movies", "episodes", "addons", "progress"};
    constexpr std::array dispositionNames{"matched", "unresolved", "existing", "skipped"};
    std::array<std::array<int, dispositionNames.size()>, categoryNames.size()> counts{};
    QVariantMap localLibrary;
    if (m_collection) {
        for (const QVariant &value : m_collection->items(kTheatre)) {
            const QVariantMap record = value.toMap();
            localLibrary.insert(record.value("id").toString(), record);
        }
    }

    QList<Item> candidates;
    QHash<QString, qsizetype> byIdentity;
    QSet<QString> conflictingLibraryIds;
    for (Item item : items) {
        const auto category = static_cast<size_t>(item.category);
        if (item.disposition == Disposition::Matched && item.category == Category::Addon) {
            const QString rawUrl = identity(item);
            const QString normalized = m_extensions && rawUrl.size() <= 4096
                ? m_extensions->normalizeUrl(rawUrl) : QString();
            const QUrl url(normalized, QUrl::StrictMode);
            if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
                || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
                item.disposition = Disposition::Skipped;
            } else {
                item.record.insert("url", normalized);
            }
        }
        if (item.disposition != Disposition::Matched) {
            ++counts[category][static_cast<size_t>(item.disposition)];
            continue;
        }
        const QString key = QString::number(static_cast<int>(category)) + QLatin1Char('/') + identity(item);
        const auto found = byIdentity.constFind(key);
        if (found != byIdentity.constEnd()) {
            if (item.category == Category::Library
                && (conflictingLibraryIds.contains(key)
                    || item.record.value("type") != candidates.at(*found).record.value("type"))) {
                conflictingLibraryIds.insert(key);
                candidates[*found].disposition = Disposition::Unresolved;
                ++counts[category][static_cast<size_t>(Disposition::Unresolved)];
                continue;
            }
            ++counts[category][static_cast<size_t>(Disposition::Skipped)];
            if (activity(item) > activity(candidates.at(*found)))
                candidates[*found] = std::move(item);
            continue;
        }
        byIdentity.insert(key, candidates.size());
        candidates.append(std::move(item));
    }

    Plan plan;
    for (const Item &item : candidates) {
        const Disposition state = disposition(item, localLibrary);
        ++counts[static_cast<size_t>(item.category)][static_cast<size_t>(state)];
        if (state == Disposition::Matched)
            plan.applicable.append(item);
    }
    for (size_t category = 0; category < categoryNames.size(); ++category) {
        if (category == static_cast<size_t>(Category::Progress)
            && counts[category] == decltype(counts)::value_type{}) {
            continue;
        }
        QVariantMap summary{{"category", QString::fromLatin1(categoryNames[category])}};
        for (size_t state = 0; state < dispositionNames.size(); ++state)
            summary.insert(QString::fromLatin1(dispositionNames[state]), counts[category][state]);
        plan.counts.append(summary);
    }
    return plan;
}

TheatreAccountImport::Disposition TheatreAccountImport::disposition(
    const Item &item, const QVariantMap &localLibrary) const {
    if (item.disposition != Disposition::Matched)
        return item.disposition;
    const QString id = identity(item);
    if (id.isEmpty())
        return Disposition::Unresolved;
    if (item.category == Category::Addon)
        return m_extensions && !m_extensions->isInstalled(id)
            ? Disposition::Matched : Disposition::Existing;

    const qint64 remoteAt = activity(item);
    if (item.category == Category::Library) {
        const QVariantMap current = localLibrary.value(id).toMap();
        if (current.isEmpty())
            return Disposition::Matched;
        if (current.value("type") != item.record.value("type"))
            return Disposition::Unresolved;
        return newerThan(remoteAt, current.value("addedAt").toLongLong())
            ? Disposition::Matched : Disposition::Existing;
    }

    if (remoteAt <= 0)
        return Disposition::Skipped;
    const QVariantMap localProgress = m_progress ? m_progress->get(kVideo, id) : QVariantMap();
    const qint64 localProgressAt = localProgress.value("updatedAt").toLongLong();
    if (!localProgress.isEmpty() && !newerThan(remoteAt, localProgressAt))
        return Disposition::Existing;
    if (item.category == Category::Progress)
        return Disposition::Matched;

    const QVariantMap history = m_history
        ? m_history->get(item.record.value("kind").toString(), id) : QVariantMap();
    const qint64 lastActivityAt = history.value("lastActivityAt").toLongLong();
    const bool repairMovieMark = item.category == Category::Movie && m_progress
        && m_progress->watchedMark(id) == 0 && history.value("source") == item.record.value("source")
        && history.value("latestKnownAt").toLongLong() == remoteAt;
    if (!history.isEmpty() && !newerThan(remoteAt, lastActivityAt) && !repairMovieMark)
        return Disposition::Existing;
    if (item.category == Category::Movie && m_progress && m_progress->watchedMark(id) != 0
        && !newerThan(remoteAt, m_progress->watchedMarkActionAt(id))) {
        return Disposition::Existing;
    }
    return Disposition::Matched;
}

bool TheatreAccountImport::apply(const QList<Item> &items, Completion completion) {
    if (m_applying || !m_collection || !m_history || !m_progress)
        return false;
    const Plan plan = makePlan(items);
    auto batch = std::make_shared<Applying>();
    batch->generation = m_generation;
    batch->completion = std::move(completion);
    m_applying = true;

    QVariantMap localLibrary;
    for (const QVariant &value : m_collection->items(kTheatre)) {
        const QVariantMap entry = value.toMap();
        localLibrary.insert(entry.value("id").toString(), entry);
    }
    QVariantList library;
    for (const Item &item : plan.applicable) {
        if (item.category != Category::Library) {
            batch->items.append(item);
            continue;
        }
        // Keep local fields outside the imported projection, e.g. user's
        // existing source binding. Unknown membership dates stay unknown.
        QVariantMap entry = localLibrary.value(identity(item)).toMap();
        for (auto it = item.record.cbegin(); it != item.record.cend(); ++it)
            entry.insert(it.key(), it.value());
        if (activity(item) <= 0) {
            if (batch->generation != m_generation || !m_collection
                || !m_collection->applySyncedEntry(kTheatre, entry)) {
                finish(batch, false);
                return true;
            }
            ++batch->result.changed;
        } else {
            library.append(entry);
        }
    }
    for (qsizetype offset = 0; offset < library.size(); offset += 4096) {
        const QVariantList page = library.mid(offset, 4096);
        if (batch->generation != m_generation || !m_collection
            || !m_collection->replaceItems(kTheatre, {}, page)) {
            finish(batch, false);
            return true;
        }
        batch->result.changed += page.size();
    }
    applyNext(batch);
    return true;
}

void TheatreAccountImport::applyNext(const std::shared_ptr<Applying> &batch) {
    if (batch->generation != m_generation)
        return;
    if (!m_history || !m_progress) {
        finish(batch, false);
        return;
    }
    // Commit watched facts before playback positions. A completed fact can
    // remain visible without inventing a resume position or playback event.
    for (int rows = 0; rows < 32 && batch->next < batch->items.size(); ++rows) {
        const Item item = batch->items.at(batch->next++);
        if (disposition(item, {}) != Disposition::Matched)
            continue;
        if (item.category == Category::Addon) {
            applyAddon(batch, item);
            return;
        }
        if (item.category == Category::Progress) {
            const bool started = m_progress->applySyncedEntryAsync(item.record,
                [self = QPointer<TheatreAccountImport>(this), batch](bool committed, const QString &) {
                    if (!self || batch->generation != self->m_generation)
                        return;
                    if (!committed) {
                        self->finish(batch, false);
                        return;
                    }
                    ++batch->result.changed;
                    self->applyNext(batch);
                });
            if (!started)
                finish(batch, false);
            return;
        }
        if (!m_history->applySyncedRecord(item.record)) {
            finish(batch, false);
            return;
        }
        if (batch->generation != m_generation)
            return;
        const QString id = identity(item);
        const qint64 at = activity(item);
        const QVariantMap committed = m_history->get(item.record.value("kind").toString(), id);
        if (committed.value("latestKnownAt").toLongLong() != at)
            continue; // The owner's durable deletion/reset barrier won.
        if (item.category == Category::Movie
            && !m_progress->applySyncedWatchedMark(id, 1, at)) {
            finish(batch, false);
            return;
        }
        ++batch->result.changed;
    }
    if (batch->next >= batch->items.size()) {
        finish(batch, true);
        return;
    }
    QTimer::singleShot(0, this, [this, batch] { applyNext(batch); });
}

void TheatreAccountImport::applyAddon(const std::shared_ptr<Applying> &batch, const Item &item) {
    const QString url = identity(item);
    if (!m_extensions) {
        ++batch->result.addonFailures;
        QTimer::singleShot(0, this, [this, batch] { applyNext(batch); });
        return;
    }
    const auto settle = [this, batch, url, enabled = item.record.value("enabled", true).toBool()]
        (bool installed) {
            if (batch->generation != m_generation)
                return;
            disconnect(m_addonInstalled);
            disconnect(m_addonFailed);
            if (installed) {
                ++batch->result.changed;
                if (!enabled && m_extensions)
                    m_extensions->setEnabledInstance(url, false);
            } else {
                ++batch->result.addonFailures;
            }
            QTimer::singleShot(0, this, [this, batch] { applyNext(batch); });
        };
    m_addonInstalled = connect(m_extensions, &ExtensionsStore::installFinished, this,
        [this, url, settle](const QString &, const QString &) {
            if (m_extensions && m_extensions->isInstalled(url))
                settle(true);
        });
    m_addonFailed = connect(m_extensions, &ExtensionsStore::installFailed, this,
        [url, settle](const QString &failedUrl, const QString &) {
            if (url == failedUrl)
                settle(false);
        });
    m_extensions->install(url);
}

void TheatreAccountImport::finish(const std::shared_ptr<Applying> &batch, bool committed) {
    if (batch->generation != m_generation)
        return;
    m_applying = false;
    batch->result.committed = committed;
    if (auto completion = std::exchange(batch->completion, {}))
        completion(batch->result);
}
