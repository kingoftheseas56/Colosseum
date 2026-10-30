# Test tooling only; include from this directory's CMakeLists.txt. Never linked
# into the application. The fixture controller target remains owned separately.
add_executable(collections_seed
    seed_collections.cpp
    ../../native/NamedCollectionsStore.cpp
    ../../native/NamedCollectionsStore.h
    ../../native/engine/ColosseumTitleIdentityRegistry.cpp
    ../../native/engine/ColosseumTitleIdentityRegistry.h)
set_target_properties(collections_seed PROPERTIES AUTOMOC ON)
target_compile_features(collections_seed PRIVATE cxx_std_17)
target_link_libraries(collections_seed PRIVATE Qt6::Core Qt6::Gui)
