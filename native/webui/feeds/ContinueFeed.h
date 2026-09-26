#pragma once

#include <QString>
#include <QVariantList>

class ContinueFeed final {
public:
    // ProgressStore::recent() is copied on the GUI thread; all catalogue work
    // happens in the worker which calls this function.
    static QVariantList build(const QVariantList &recent, const QString &scope,
                              const QString &imdbPath, int visibleCount);
};
