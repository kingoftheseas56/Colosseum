#include "AnnotationMeaningPlane.h"
#include "DeterministicRanker.h"
#include "DiscoveryEnrichment.h"
#include "FeedbackTastePersistence.h"
#include "QueueSessionCoordinator.h"
#include "RecommendationDerivation.h"
#include "ResolvedPlaybackSource.h"
#include "SourceResolutionSession.h"
#include "TimedTextCore.h"
#include "VinylCatalogueClient.h"
#include "VinylListenEventLedger.h"
#include "VinylMpvObservability.h"
#include "VinylPersonalStatePersistence.h"
#include "VinylSessionPersistence.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    static_assert(vinyl::VinylSessionPersistence::SchemaVersion == 2);
    static_assert(vinyl::FeedbackTastePersistence::SchemaVersion == 2);
    qInfo().noquote()
        << "VINYL_W4_SUPERBUILD_LINK_OK"
        << "ba3Schema=" << vinyl::VinylSessionPersistence::SchemaVersion
        << "be2Schema=" << vinyl::FeedbackTastePersistence::SchemaVersion;
    return 0;
}

