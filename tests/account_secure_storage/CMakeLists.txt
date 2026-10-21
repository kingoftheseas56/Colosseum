cmake_minimum_required(VERSION 3.16)
project(colosseum_account_secure_storage_tests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
enable_testing()
add_subdirectory(../../native/account/credentials credentials)
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    qt_add_executable(tst_account_secure_storage MANUAL_FINALIZATION tst_account_secure_storage.cpp)
    set_target_properties(tst_account_secure_storage PROPERTIES
        QT_ANDROID_PACKAGE_NAME "org.brotherhood.colosseum.accountstoragetest")
else()
    add_executable(tst_account_secure_storage tst_account_secure_storage.cpp)
endif()
target_link_libraries(tst_account_secure_storage PRIVATE colosseum_account_credentials Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(tst_account_secure_storage PRIVATE Qt6::DBus)
    target_compile_definitions(tst_account_secure_storage PRIVATE COLOSSEUM_SECRET_SERVICE_TEST)
endif()
if(ANDROID)
    target_link_libraries(tst_account_secure_storage PRIVATE Qt6::Gui)
    qt_finalize_executable(tst_account_secure_storage)
    # Android runs this test APK through adb; host CTest cannot execute a .so.
    return()
endif()
add_test(NAME account_secure_storage.contract COMMAND tst_account_secure_storage)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _credential_test_tag)
add_test(NAME account_secure_storage.platform COMMAND tst_account_secure_storage --integration)
set_tests_properties(account_secure_storage.platform PROPERTIES
    ENVIRONMENT "COLOSSEUM_APPDATA_TAG=account-storage-test-${_credential_test_tag}" TIMEOUT 90)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME account_secure_storage.unavailable COMMAND tst_account_secure_storage --unavailable)
    set_tests_properties(account_secure_storage.unavailable PROPERTIES TIMEOUT 20
        ENVIRONMENT "COLOSSEUM_APPDATA_TAG=account-storage-test-no-service;DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/colosseum-account-storage-no-bus")
endif()

# Real controller/session regression tests, using fixture transport and memory
# credentials. No production account service or daily account vault is touched.
add_executable(tst_account_identity
    ../auto/account_identity/tst_account_identity.cpp
    ../support/account/AccountFixtureTransport.cpp
    ../../native/account/AccountBootstrapStore.cpp
    ../../native/account/AccountDeviceIdentity.cpp
    ../../native/account/AccountHttpTransport.cpp
    ../../native/account/AccountClient.cpp
    ../../native/account/AccountLifecycleCoordinator.cpp
    ../../native/account/AccountController.cpp
    ../../native/account/ProfilePaths.cpp
    ../../native/account/SyncAdapter.h
    ../../native/account/SyncAdapterRegistry.cpp
    ../../native/account/SyncOwnershipInventory.cpp
    ../../native/account/SyncPayloadFirewall.cpp
    ../../native/account/SyncProtocol.cpp
    ../../native/account/SyncHybridClock.cpp
    ../../native/account/SyncStateStore.cpp
    ../../native/account/SyncEngine.cpp
    ../../native/account/AccountTransport.h)
target_include_directories(tst_account_identity PRIVATE ../../native ../support/account)
target_link_libraries(tst_account_identity PRIVATE colosseum_account_credentials Qt6::Network Qt6::Test)
add_test(NAME account_secure_storage.identity COMMAND tst_account_identity)
