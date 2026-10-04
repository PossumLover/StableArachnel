cmake_minimum_required(VERSION 3.20)
project(sprout_network_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Test)
enable_testing()
set(core "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
add_executable(network_tests
    network_tests.cpp
    transport_stubs.cpp
    ${core}/torrent/torrent_session.h
    ${core}/jobs/http_download_session.h
    ${core}/catalog/hydra_catalog_client.cpp
    ${core}/catalog/catalog_parser.cpp
    ${core}/catalog/catalog_types.cpp
    ${core}/catalog/catalog_genre_normalize.cpp
    ${core}/install/install_kind.cpp
    ${core}/jobs/torbox_download_session.cpp
    ${core}/jobs/job_store.cpp
    ${core}/jobs/job_model.cpp
    ${core}/jobs/job_kind.cpp
    ${core}/jobs/job_display.cpp
    ${core}/jobs/job_orchestrator.cpp
    ${core}/jobs/job_orchestrator_progress.cpp
    ${core}/jobs/job_orchestrator_completion.cpp
    ${core}/jobs/job_status.cpp
    ${core}/settings/settings_store.cpp
    ${core}/settings/settings_store_persistence.cpp
    ${core}/settings/settings_store_debrid.cpp
    ${core}/library/storage_library.cpp
    ${core}/library/storage_library_model.cpp
    ${core}/plugins/source_plugin_model.cpp)
target_include_directories(network_tests PRIVATE
    ${core}/catalog ${core}/install ${core}/jobs ${core}/settings
    ${core}/library ${core}/plugins ${core}/runtime ${core}/torrent ${core}/i18n)
target_compile_definitions(network_tests PRIVATE ARACHNEL_VERSION="test")
target_link_libraries(network_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
if(WIN32)
    target_link_libraries(network_tests PRIVATE crypt32)
endif()
add_test(NAME network_tests COMMAND network_tests)
