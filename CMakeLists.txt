cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(sqlite3 STATIC vendor/sqlite/sqlite3.c vendor/sqlite/sqlite3.h)
target_include_directories(sqlite3 PUBLIC vendor/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION _CRT_SECURE_NO_WARNINGS)
set_target_properties(sqlite3 PROPERTIES COMPILE_WARNING_AS_ERROR OFF POSITION_INDEPENDENT_CODE ON)

add_library(replay-timeline-domain STATIC src/domain/timeline-mapper.cpp src/domain/timeline-mapper.hpp)
target_include_directories(replay-timeline-domain PUBLIC src)
target_compile_features(replay-timeline-domain PUBLIC cxx_std_17)
set_target_properties(replay-timeline-domain PROPERTIES CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(replay-timeline-paths STATIC src/media/replay-path-resolver.cpp src/media/replay-path-resolver.hpp)
target_include_directories(replay-timeline-paths PUBLIC src)
target_link_libraries(replay-timeline-paths PUBLIC Qt6::Core)
target_compile_features(replay-timeline-paths PUBLIC cxx_std_17)
set_target_properties(replay-timeline-paths PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

add_library(replay-timeline-storage STATIC src/persistence/repository.cpp src/persistence/repository.hpp)
target_include_directories(replay-timeline-storage PUBLIC src vendor/sqlite)
target_link_libraries(replay-timeline-storage PUBLIC replay-timeline-domain Qt6::Core PRIVATE sqlite3)
target_compile_features(replay-timeline-storage PUBLIC cxx_std_17)
set_target_properties(replay-timeline-storage PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

add_library(
  replay-timeline-export
  STATIC
  src/export/csv-exporter.cpp
  src/export/csv-exporter.hpp
  src/export/editor-exporter.cpp
  src/export/editor-exporter.hpp
)
target_include_directories(replay-timeline-export PUBLIC src)
target_link_libraries(replay-timeline-export PUBLIC replay-timeline-storage Qt6::Core)
target_compile_features(replay-timeline-export PUBLIC cxx_std_17)
set_target_properties(replay-timeline-export PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src vendor/sqlite)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)

find_package(FFmpeg 6.1 REQUIRED avcodec avformat avutil swscale)
list(GET FFMPEG_SHARED_LIBRARIES 0 FFMPEG_RUNTIME_LIBRARY)
get_filename_component(FFMPEG_RUNTIME_DIR "${FFMPEG_RUNTIME_LIBRARY}" DIRECTORY)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE replay-timeline-export replay-timeline-paths FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON CXX_EXTENSIONS OFF
)

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/controller/session-controller.cpp
    src/controller/session-controller.hpp
    src/media/media-probe.cpp
    src/media/media-probe.hpp
    src/media/thumbnail-generator.cpp
    src/media/thumbnail-generator.hpp
    src/obs/application-metadata.cpp
    src/obs/application-metadata.hpp
    src/obs/obs-event-bridge.cpp
    src/obs/obs-event-bridge.hpp
    src/plugin/plugin-controller.cpp
    src/plugin/plugin-controller.hpp
    src/plugin/plugin-main.cpp
    src/ui/replay-timeline-dock.cpp
    src/ui/replay-timeline-dock.hpp
    src/ui/replay-preview-widget.cpp
    src/ui/replay-preview-widget.hpp
)

include(CTest)
if(BUILD_TESTING)
  add_executable(timeline-mapper-test tests/timeline-mapper-test.cpp)
  target_link_libraries(timeline-mapper-test PRIVATE replay-timeline-domain)
  set_target_properties(timeline-mapper-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME timeline-mapper COMMAND timeline-mapper-test)

  add_executable(repository-test tests/repository-test.cpp)
  target_link_libraries(repository-test PRIVATE replay-timeline-storage Qt6::Core)
  set_target_properties(repository-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME repository COMMAND repository-test)
  set_tests_properties(
    repository
    PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )

  add_executable(csv-exporter-test tests/csv-exporter-test.cpp)
  target_link_libraries(csv-exporter-test PRIVATE replay-timeline-export Qt6::Core)
  set_target_properties(csv-exporter-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME csv-exporter COMMAND csv-exporter-test)
  set_tests_properties(
    csv-exporter
    PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )

  add_executable(editor-exporter-test tests/editor-exporter-test.cpp)
  target_link_libraries(editor-exporter-test PRIVATE replay-timeline-export Qt6::Core)
  set_target_properties(editor-exporter-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME editor-exporter COMMAND editor-exporter-test)
  set_tests_properties(
    editor-exporter
    PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )

  add_executable(replay-path-resolver-test tests/replay-path-resolver-test.cpp)
  target_link_libraries(replay-path-resolver-test PRIVATE replay-timeline-paths Qt6::Core)
  set_target_properties(replay-path-resolver-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME replay-path-resolver COMMAND replay-path-resolver-test)
  set_tests_properties(
    replay-path-resolver
    PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )

  add_executable(media-probe-test tests/media-probe-test.cpp src/media/media-probe.cpp src/media/media-probe.hpp)
  target_include_directories(media-probe-test PRIVATE src)
  target_link_libraries(media-probe-test PRIVATE Qt6::Core FFmpeg::avformat FFmpeg::avutil)
  set_target_properties(media-probe-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME media-probe COMMAND media-probe-test)
  set_tests_properties(
    media-probe
    PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${FFMPEG_RUNTIME_DIR}"
      TIMEOUT 15
  )

  add_executable(
    thumbnail-generator-test
    tests/thumbnail-generator-test.cpp
    src/media/thumbnail-generator.cpp
    src/media/thumbnail-generator.hpp
  )
  target_include_directories(thumbnail-generator-test PRIVATE src)
  target_link_libraries(
    thumbnail-generator-test
    PRIVATE Qt6::Core Qt6::Gui FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale
  )
  set_target_properties(thumbnail-generator-test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME thumbnail-generator COMMAND thumbnail-generator-test)
  set_tests_properties(
    thumbnail-generator
    PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${FFMPEG_RUNTIME_DIR}"
      TIMEOUT 15
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
