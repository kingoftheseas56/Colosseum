#include "HomeServerProgressOwner.h"
#include "ProgressStore.h"
#include <QPointer>

namespace {
class ProgressBridge final : public HomeServerProgressOwner {
public:
  explicit ProgressBridge(ProgressStore *store) : m_store(store) {}
  QVariantList entries() const override {
    return m_store ? m_store->deliveryEntries() : QVariantList{};
  }
  QVariantMap get(const QString &id) const override {
    return m_store ? m_store->get(QStringLiteral("video"), id) : QVariantMap{};
  }
  bool apply(const QVariantMap &entry) override {
    return m_store && m_store->applySyncedEntry(entry);
  }
  bool remove(const QString &id) override {
    return m_store && m_store->removeSyncedEntry(QStringLiteral("video"), id);
  }
  Watch watch(const QString &id) const override {
    if (!m_store)
      return {};
    return {m_store->watchedMark(id), m_store->watchedMarkActionAt(id),
            m_store->watchedMarkIsManual(id)};
  }
  bool applyWatched(const QString &id, bool played, qint64 at) override {
    return m_store && m_store->applySyncedWatchedMark(id, played ? 1 : -1, at);
  }

private:
  QPointer<ProgressStore> m_store;
};
} // namespace
std::shared_ptr<HomeServerProgressOwner>
homeServerProgressBridge(ProgressStore *store) {
  return std::make_shared<ProgressBridge>(store);
}
