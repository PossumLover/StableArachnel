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
    ${core}/catalog/catalog_identity.cpp
    ${core}/catalog/catalog_search_utils.cpp
    ${core}/catalog/catalog_model.cpp
    ${core}/catalog/catalog_filter_service.cpp
    ${core}/catalog/content_rating_store.cpp
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
    ${core}/library/library_model.cpp
    ${core}/library/library_view_model.cpp
    ${core}/catalog/torbox_cache_service.cpp
    ${core}/library/library_store.cpp
    ${core}/library/achievement_files.cpp
    ${core}/library/achievement_service.cpp
    ${core}/library/storage_library_model.cpp
    ${core}/plugins/source_plugin_model.cpp)
target_include_directories(network_tests PRIVATE
    ${core}/catalog ${core}/install ${core}/jobs ${core}/settings
    ${core}/library ${core}/plugins ${core}/runtime ${core}/torrent ${core}/i18n
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_definitions(network_tests PRIVATE ARACHNEL_VERSION="test")
target_link_libraries(network_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
if(WIN32)
    target_link_libraries(network_tests PRIVATE crypt32)
endif()
add_test(NAME network_tests COMMAND network_tests)

# A plugin that answers with string literals, loaded through the real plugin host and
# unloaded again; everything the host kept from it must still be readable.
find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Gui)
add_library(boundary_test_plugin MODULE plugin_boundary_test_plugin.cpp)
target_include_directories(boundary_test_plugin PRIVATE
    ${core}/plugins ${core}/catalog ${core}/install ${core}/library)
target_link_libraries(boundary_test_plugin PRIVATE Qt6::Core)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT WIN32)
    # GNU unique symbols keep a library mapped after dlclose; this one has to go.
    target_compile_options(boundary_test_plugin PRIVATE -fno-gnu-unique)
endif()
add_executable(plugin_boundary_tests
    plugin_boundary_tests.cpp
    ${core}/plugins/plugin_boundary.cpp
    ${core}/plugins/plugin_host.cpp
    ${core}/plugins/plugin_host_async.cpp
    ${core}/plugins/plugin_host_packages.cpp
    ${core}/plugins/plugin_catalog_json.cpp
    ${core}/plugins/plugin_version.cpp
    ${core}/plugins/source_plugin_model.cpp
    ${core}/catalog/catalog_disk_cache.cpp
    ${core}/catalog/catalog_parser.cpp
    ${core}/catalog/catalog_types.cpp
    ${core}/catalog/catalog_genre_normalize.cpp
    ${core}/install/install_kind.cpp
    ${core}/install/install_analysis.cpp
    ${core}/library/library_model.cpp
    ${core}/library/library_store.cpp
    ${core}/util/file_utils.cpp)
target_include_directories(plugin_boundary_tests PRIVATE
    ${core}/catalog ${core}/install ${core}/jobs ${core}/settings ${core}/library
    ${core}/plugins ${core}/runtime ${core}/torrent ${core}/util ${core}/i18n
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_definitions(plugin_boundary_tests PRIVATE ARACHNEL_VERSION="test"
    BOUNDARY_TEST_PLUGIN="$<TARGET_FILE:boundary_test_plugin>")
target_link_libraries(plugin_boundary_tests PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Test)
add_dependencies(plugin_boundary_tests boundary_test_plugin)
add_test(NAME plugin_boundary_tests COMMAND plugin_boundary_tests)

if(WIN32)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml)
    add_executable(window_resize_tests window_resize_tests.cpp
        ../src/app/window_frame.cpp)
    target_compile_definitions(window_resize_tests PRIVATE
        SPROUT_WINDOW_CHROME_QML="${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/WindowChrome.qml")
    target_link_libraries(window_resize_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Test user32)
    add_test(NAME window_resize_tests COMMAND window_resize_tests -o -,txt)
    set_tests_properties(window_resize_tests PROPERTIES TIMEOUT 30)
endif()
