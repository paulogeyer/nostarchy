cmake_minimum_required(VERSION 3.22)
project(nostarchy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
qt_standard_project_setup()

include(FetchContent)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    secp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG v0.7.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(secp256k1)

set(NOSTARCHY_SOURCES
    src/crypto/Bech32.cpp
    src/crypto/Bech32.h
    src/crypto/Keys.cpp
    src/crypto/Keys.h
    src/nostr/Event.cpp
    src/nostr/Event.h
    src/net/WebSocketClient.cpp
    src/net/WebSocketClient.h
    src/nostr/RelayPool.cpp
    src/nostr/RelayPool.h
    src/TimelineModel.cpp
    src/TimelineModel.h
    src/AppController.cpp
    src/AppController.h
    src/ImageCache.cpp
    src/ImageCache.h
    src/PeopleModel.cpp
    src/PeopleModel.h
    src/Theme.cpp
    src/Theme.h
    src/LogStore.cpp
    src/LogStore.h
)

add_library(nostarchy_core STATIC ${NOSTARCHY_SOURCES})
target_include_directories(nostarchy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nostarchy_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network secp256k1)
target_compile_options(nostarchy_core PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(nostarchy src/main.cpp)
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(nostarchy
    URI Nostarchy
    VERSION 1.0
    QML_FILES qml/Main.qml
)
target_link_libraries(nostarchy PRIVATE nostarchy_core Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_compile_options(nostarchy PRIVATE -Wall -Wextra -Wpedantic)

add_executable(nostarchy-check tests/check.cpp)
target_link_libraries(nostarchy-check PRIVATE nostarchy_core)
target_compile_options(nostarchy-check PRIVATE -Wall -Wextra -Wpedantic)
