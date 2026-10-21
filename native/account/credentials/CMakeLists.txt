cmake_minimum_required(VERSION 3.16)

add_library(colosseum_account_credentials STATIC
    ../PlatformAccountCredentialStore.cpp
    ../PlatformAccountCredentialStore.h
    AccountSecretBackend.h)
target_include_directories(colosseum_account_credentials PUBLIC ../..)
target_link_libraries(colosseum_account_credentials PUBLIC Qt6::Core)
target_compile_features(colosseum_account_credentials PUBLIC cxx_std_17)
set_target_properties(colosseum_account_credentials PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    target_sources(colosseum_account_credentials PRIVATE AndroidAccountSecretBackend.cpp)
elseif(WIN32)
    target_sources(colosseum_account_credentials PRIVATE WindowsAccountSecretBackend.cpp)
    target_link_libraries(colosseum_account_credentials PRIVATE Advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ACCOUNT_LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.19)
    find_package(Threads REQUIRED)
    target_sources(colosseum_account_credentials PRIVATE LinuxAccountSecretBackend.cpp)
    target_link_libraries(colosseum_account_credentials PRIVATE PkgConfig::ACCOUNT_LIBSECRET Threads::Threads)
else()
    # Preserve local-only builds on platforms without an account backend.
    target_sources(colosseum_account_credentials PRIVATE UnavailableAccountSecretBackend.cpp)
endif()
