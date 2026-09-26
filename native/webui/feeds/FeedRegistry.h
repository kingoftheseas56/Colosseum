#pragma once

#include "WorldFeed.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct FeedContext {
    QVariantMap params;
    QVariantList recent;
    QVariantList collection;
    WorldFeed::Paths paths;
    int visibleCount = 24;
    bool showExplicit = false;
};

// A feed's translation unit registers only when it is compiled into the app.
// For world feeds, selector is the world name; other feeds use an empty selector.
class FeedRegistry final {
public:
    using Validator = bool (*)(const QVariantMap &);
    using Initial = QVariantList (*)(const QVariantMap &);
    using Builder = QVariantList (*)(const FeedContext &);

    struct Entry {
        QString name;
        QString selector;
        Validator valid = nullptr;
        Initial initial = nullptr;
        Builder build = nullptr;
        bool needsProgress = false;
        bool needsCollection = false;
    };

    static bool add(Entry entry);
    static const Entry *find(const QString &name, const QVariantMap &params);
};
