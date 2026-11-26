#pragma once
#include <QVariantMap>
#include <QVariantList>
#include "RecommendationDerivation.h"
class ActivityStore;
class HistoryStore;

// Rebuildable read projection. Activity owns qualified time and intent/outcome
// facts; History owns chronology. Neither generic progress nor names prove plays.
class VinylRecommendationHistory final {
public:
    struct Inputs {
        bool enabled=false,healthy=true,hasChronology=false;
        QString activityDatabase;
        QList<QVariantMap> inMemoryFacts;
        QVariantList chronology;
        quint64 resetGeneration=0;
        qint64 resetAt=0;
    };
    static Inputs capture(ActivityStore *,HistoryStore *);
    static QVariantMap project(const Inputs &,const QVariantList &catalogueRows,qint64 nowMs);
    static QVariantMap project(ActivityStore *,HistoryStore *,const QVariantList &catalogueRows,qint64 nowMs);
    static vinyl::recommendation::DerivationResult<vinyl::recommendation::RecommendationDerivationOutput>
        derive(const QString &profile,const QVariantMap &snapshot,qint64 nowMs);
};
