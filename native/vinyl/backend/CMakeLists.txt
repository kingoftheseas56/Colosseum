# Adopted Arc 62 domain implementations. This target is deliberately not linked
# into the live app until M0F establishes host ownership and adapters.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
find_path(VINYL_MPV_INCLUDE_DIR mpv/client.h HINTS "${LIBMPV_PREFIX}/include" REQUIRED)
find_library(VINYL_MPV_LIBRARY NAMES mpv libmpv HINTS "${LIBMPV_PREFIX}/lib" REQUIRED)
set(BACKEND_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(BA0_DIR "${BACKEND_DIR}/ba0-domain-prototype")
set(BA1_DIR "${BACKEND_DIR}/ba1-queue-core-prototype")
set(BA2A_DIR "${BACKEND_DIR}/ba2-playback-session-prototype")
set(BA2B_DIR "${BACKEND_DIR}/ba2-engine-adapter-prototype")
set(BA2C_DIR "${BACKEND_DIR}/ba2c-queue-session-coordination-prototype")
set(BA2D_DIR "${BACKEND_DIR}/ba2d-mpv-adoption-prototype")
set(BA3_DIR "${BACKEND_DIR}/ba3-session-persistence-prototype")
set(BA4_DIR "${BACKEND_DIR}/ba4-listen-event-ledger-prototype")
set(BA5_DIR "${BACKEND_DIR}/ba5-library-playlists-prototype")
set(BB1_DIR "${BACKEND_DIR}/bb1-source-candidate-prototype")
set(BB2_DIR "${BACKEND_DIR}/bb2-exact-track-resolver-prototype")
set(BB3_DIR "${BACKEND_DIR}/bb3-cue-whole-album-prototype")
set(BB4_DIR "${BACKEND_DIR}/bb4-mpv-playback-adapter-prototype")
set(BB5_DIR "${BACKEND_DIR}/bb5-source-resolution-session-prototype")
set(BC1_DIR "${BACKEND_DIR}/bc1-timed-text-prototype")
set(BC2_DIR "${BACKEND_DIR}/bc2-translation-prototype")
set(BC3_DIR "${BACKEND_DIR}/bc3-annotation-prototype")
set(BD1_DIR "${BACKEND_DIR}/bd1-signal-path-prototype")
set(BD2_DIR "${BACKEND_DIR}/bd2-mpv-observability-prototype")
set(BE1_DIR "${BACKEND_DIR}/be1-recommendation-candidate-prototype")
set(BE2_DIR "${BACKEND_DIR}/be2-feedback-taste-state-prototype")
set(BE_DERIVATION_DIR "${BACKEND_DIR}/be-derivation-prototype")
set(BE3_DIR "${BACKEND_DIR}/be3-deterministic-ranker-prototype")
set(BF1_DIR "${BACKEND_DIR}/bf1-context-graph-prototype")
set(BF2_DIR "${BACKEND_DIR}/bf2-related-video-prototype")
set(BF3_DIR "${BACKEND_DIR}/bf3-video-playback-adapter-prototype")
set(BG1_DIR "${BACKEND_DIR}/bg1-catalogue-client-prototype")
set(BG2_DIR "${BACKEND_DIR}/bg2-discovery-enrichment-prototype")
set(W4H_E2E_DIR "${BACKEND_DIR}/w4-end-to-end-prototype")

add_library(colosseum_vinyl_backend STATIC
    ${BA0_DIR}/VinylDomainTypes.cpp
    ${BA1_DIR}/VinylQueue.cpp
    ${BA2A_DIR}/VinylPlaybackSession.cpp
    ${BA2B_DIR}/PlaybackEngineAdapter.cpp
    ${BA2C_DIR}/QueueSessionCoordinator.cpp
    ${BA2D_DIR}/ColosseumMpvBridge.cpp
    ${BA2D_DIR}/MpvSidecarReference.cpp
    ${BA3_DIR}/VinylSessionPersistence.cpp
    ${BA4_DIR}/VinylListenEventLedger.cpp
    ${BA4_DIR}/VinylListenEventPersistence.cpp
    ${BA5_DIR}/VinylLibrary.cpp
    ${BA5_DIR}/VinylPlaylist.cpp
    ${BA5_DIR}/VinylPersonalStatePersistence.cpp
    ${BB1_DIR}/SourceCandidateContract.cpp
    ${BB2_DIR}/ExactTrackResolver.cpp
    ${BB3_DIR}/CueWholeAlbum.cpp
    ${BB4_DIR}/ResolvedPlaybackSource.cpp
    ${BB5_DIR}/SourceResolutionSession.cpp
    ${BC1_DIR}/TimedTextCore.cpp
    ${BC2_DIR}/TimedTextTranslation.cpp
    ${BC3_DIR}/AnnotationMeaningPlane.cpp
    ${BD1_DIR}/VinylSignalPathTypes.cpp
    ${BD2_DIR}/VinylMpvObservability.cpp
    ${BE1_DIR}/RecommendationCandidateContract.cpp
    ${BE1_DIR}/RecommendationEntityRef.cpp
    ${BACKEND_DIR}/../VinylRecommendationIdentity.cpp
    ${BE2_DIR}/FeedbackTasteState.cpp
    ${BE2_DIR}/FeedbackTastePersistence.cpp
    ${BE_DERIVATION_DIR}/RecommendationDerivation.cpp
    ${BE3_DIR}/DeterministicRanker.cpp
    ${BF1_DIR}/VinylContextGraph.cpp
    ${BF2_DIR}/RelatedVideoContract.cpp
    ${BF3_DIR}/VideoPlaybackAdapter.cpp
    ${BG1_DIR}/VinylCatalogueTypes.cpp

    ${BG2_DIR}/DiscoveryEnrichment.cpp
)
target_compile_features(colosseum_vinyl_backend PUBLIC cxx_std_17)

target_include_directories(colosseum_vinyl_backend PUBLIC
    ${BA0_DIR}
    ${BA1_DIR}
    ${BA2A_DIR}
    ${BA2B_DIR}
    ${BA2C_DIR}
    ${BA2D_DIR}
    ${BA3_DIR}
    ${BA4_DIR}
    ${BA5_DIR}
    ${BB1_DIR}
    ${BB2_DIR}
    ${BB3_DIR}
    ${BB4_DIR}
    ${BB5_DIR}
    ${BC1_DIR}
    ${BC2_DIR}
    ${BC3_DIR}
    ${BD1_DIR}
    ${BD2_DIR}
    ${BE1_DIR}
    ${BE2_DIR}
    ${BE_DERIVATION_DIR}
    ${BE3_DIR}
    ${BF1_DIR}
    ${BF2_DIR}
    ${BF3_DIR}
    ${BG1_DIR}
    ${BG2_DIR}
    "${VINYL_MPV_INCLUDE_DIR}"
)

target_link_libraries(colosseum_vinyl_backend PUBLIC
    Qt6::Core
    "${VINYL_MPV_LIBRARY}"
)

set(ARTIST_DIR "${BACKEND_DIR}/artist-live-prototypes-20261015")
set(COMPOSED_DIR "${BACKEND_DIR}/artist-service-composed-20261015")
target_sources(colosseum_vinyl_backend PRIVATE
    ${ARTIST_DIR}/catalogue/ArtistCatalogueQuery.cpp
    ${ARTIST_DIR}/artwork/ArtistArtworkResolver.cpp
    ${ARTIST_DIR}/personal/ArtistPersonalState.cpp
    ${ARTIST_DIR}/media/ArtistMediaQuery.cpp
    ${ARTIST_DIR}/bridge/ArtistServiceBridge.cpp
    ${ARTIST_DIR}/bridge/ArtistServiceBridge.h
    ${COMPOSED_DIR}/ArtistReadAdapter.cpp
    ${COMPOSED_DIR}/SavedArtistAdapter.cpp
    ${COMPOSED_DIR}/RecordingPlaybackPort.cpp
    ${COMPOSED_DIR}/RecordingPlaybackPort.h
    ${COMPOSED_DIR}/ArtistServiceFactory.cpp
)
target_include_directories(colosseum_vinyl_backend PUBLIC
    ${ARTIST_DIR}/catalogue ${ARTIST_DIR}/artwork ${ARTIST_DIR}/personal
    ${ARTIST_DIR}/media ${ARTIST_DIR}/bridge ${COMPOSED_DIR})
set_target_properties(colosseum_vinyl_backend PROPERTIES AUTOMOC ON)
if(MSVC)
    target_compile_options(colosseum_vinyl_backend PRIVATE /utf-8 /W4)
endif()
