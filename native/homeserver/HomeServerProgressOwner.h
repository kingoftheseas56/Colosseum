#pragma once
#include "HomeServerProgressClient.h"
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class ProgressStore;

// Adapter to Colosseum's existing canonical progress owner. No additional store.
class HomeServerProgressOwner {
public:
    struct Watch { int mark = 0; qint64 updatedAt = 0; bool manual = false; };
    virtual ~HomeServerProgressOwner() = default;
    virtual QVariantList entries() const = 0;
    virtual QVariantMap get(const QString &id) const = 0;
    virtual bool apply(const QVariantMap &entry) = 0;
    virtual bool remove(const QString &id) = 0;
    virtual Watch watch(const QString &id) const = 0;
    virtual bool applyWatched(const QString &id, bool played, qint64 at) = 0;
};
std::shared_ptr<HomeServerProgressOwner> homeServerProgressBridge(ProgressStore *store);

namespace HomeServerProgress {
using IdentityAliases = QHash<QString, QString>;
IdentityAliases identityAliases(const QVariantList &entries);
QJsonObject withAliases(QJsonObject item, const IdentityAliases &aliases);
struct Decision {
    QString canonicalId;
    HomeServerProgressState state;
    bool localWins = false;
};
// Imports preserve the provider's actual activity time; missing time stays zero.
Decision reconcile(HomeServerProgressOwner &owner, const QJsonObject &item,
                   const QString &sourceKey);
void hideConnection(HomeServerProgressOwner &owner, const QString &connectionId);
void completed(HomeServerProgressOwner &owner, const QJsonObject &item,
               const QString &sourceKey, qint64 at);
}
