# Test tooling only. Include from this directory's standalone CMakeLists.txt;
# never link the seeder or expose it through the application.
if(NOT LIBMPV_PREFIX AND WIN32)
    set(LIBMPV_PREFIX "C:/tools/mpvqt-feasibility/libmpv-prefix" CACHE PATH "Existing libmpv development prefix")
endif()
find_path(VINYL_SEED_MPV_INCLUDE_DIR mpv/client.h
    HINTS "${LIBMPV_PREFIX}/include" ${Libmpv_INCLUDE_DIRS} REQUIRED)
find_library(VINYL_SEED_MPV_LIBRARY NAMES mpv libmpv
    HINTS "${LIBMPV_PREFIX}/lib" REQUIRED)
add_executable(vinyl_seed
    seed_vinyl.cpp
    ../../native/CollectionStore.h
    ../../native/devtools/CollectionsTestProfileController.h
    ../../native/vinyl/VinylLocalSource.cpp
    ../../native/vinyl/VinylLocalSource.h
    ../../native/vinyl/VinylMusicMetadata.cpp
    ../../native/vinyl/VinylMusicMetadata.h
    ../../native/engine/VaultIdentity.cpp
    ../../native/engine/VaultIdentity.h
    ../../native/engine/VaultLocation.h
    ../../native/engine/VaultStoreIo.h)
set_target_properties(vinyl_seed PROPERTIES AUTOMOC ON)
target_compile_features(vinyl_seed PRIVATE cxx_std_17)
target_include_directories(vinyl_seed PRIVATE ../../native "${VINYL_SEED_MPV_INCLUDE_DIR}")
target_link_libraries(vinyl_seed PRIVATE Qt6::Core "${VINYL_SEED_MPV_LIBRARY}")
if(MSVC)
    target_compile_options(vinyl_seed PRIVATE /utf-8)
endif()
