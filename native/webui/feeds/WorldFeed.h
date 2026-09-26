#pragma once

#include <QString>
#include <QVariantList>

class WorldFeed final {
public:
    struct Paths {
        QString imdb;
        QString mal;
        QString comics;
        QString biblio;
    };

    static bool validTab(const QString &world, const QString &tab);
    static QVariantList build(const QString &world, const QString &tab,
                              const Paths &paths, const QVariantList &collection,
                              bool showExplicit);
};
