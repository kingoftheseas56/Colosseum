# Test-only index/identity fixture producer. Never linked into the application.
find_package(Qt6 6.5 REQUIRED COMPONENTS Sql)
add_executable(vault_seed
    seed_vault.cpp
    ../../native/engine/VaultConfig.cpp
    ../../native/engine/VaultConfig.h
    ../../native/devtools/CollectionsTestProfileController.h
    ../../native/engine/VaultIndex.cpp
    ../../native/engine/VaultIndex.h
    ../../native/engine/VaultKit.h
    ../../native/engine/VaultIdentity.cpp
    ../../native/engine/VaultIdentity.h
    ../../native/engine/VaultLocation.h
    ../../native/engine/VaultStoreIo.h)
set_target_properties(vault_seed PROPERTIES AUTOMOC ON)
target_compile_features(vault_seed PRIVATE cxx_std_17)
target_include_directories(vault_seed PRIVATE ../../native)
target_link_libraries(vault_seed PRIVATE Qt6::Core Qt6::Sql)
if(MSVC)
    target_compile_options(vault_seed PRIVATE /utf-8)
endif()

add_executable(repair_vault_fixture repair_vault_fixture.cpp ../../native/engine/VaultIndex.cpp ../../native/engine/VaultIdentity.cpp ../../native/engine/VaultConfig.cpp ../../native/engine/VaultConfig.h)
set_target_properties(repair_vault_fixture PROPERTIES AUTOMOC ON)
target_include_directories(repair_vault_fixture PRIVATE ../../native)
target_link_libraries(repair_vault_fixture PRIVATE Qt6::Core Qt6::Sql)
if(MSVC)
 target_compile_options(repair_vault_fixture PRIVATE /utf-8)
endif()
