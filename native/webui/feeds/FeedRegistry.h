#pragma once

#include "WorldFeed.h"

#include <QString>
#include <QStringList>
#include <QHash>
#include <QMetaObject>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class ColosseumWebBridge;

struct FeedContext {
    QVariantMap params;
    QVariantList recent;
    QVariantList collection;
    QVariantList extensions;
    QVariantMap libraryFacts;
    QStringList downloadedIds;
    QStringList history;
    QVariantList baseSections;
    QVariantMap nativeSnapshot;
    WorldFeed::Paths paths;
    int subscriptionId = 0;
    int generation = 0;
    int visibleCount = 24;
    bool showExplicit = false;
};

// A feed's translation unit registers only when it is compiled into the app.
// World feeds select by world; Theatre's extension catalogue See All selects
// its own compiled handler. Other feeds use an empty selector.
class FeedRegistry final {
public:
    using Validator = bool (*)(const QVariantMap &);
    using Initial = QVariantList (*)(const QVariantMap &);
    using Builder = QVariantList (*)(const FeedContext &);
    using Capture = void (*)(ColosseumWebBridge &, FeedContext &);
    using BindOwnerSignal = QMetaObject::Connection (*)(QObject *, QObject *,
                                                        std::function<void()>);

    struct OwnerSignal {
        QString service;
        BindOwnerSignal bind = nullptr;
    };

    struct Entry {
        QString name;
        QString selector;
        Validator valid = nullptr;
        Initial initial = nullptr;
        Builder build = nullptr;
        bool needsProgress = false;
        bool needsCollection = false;
        bool needsExtensions = false;
        bool needsHistory = false;
        Builder enrich = nullptr; // optional slower second pass; result replaces sections
        Capture capture = nullptr; // GUI-thread snapshot before the worker builds sections
        Capture enrichCapture = nullptr; // GUI-thread snapshot after first worker result
        QVector<OwnerSignal> ownerSignals; // profile-bound invalidation, never sent to worker
        QHash<QString, int> pageableSections; // section id -> rows advanced by more()
    };

    static bool add(Entry entry);
    static const Entry *find(const QString &name, const QVariantMap &params);
};
