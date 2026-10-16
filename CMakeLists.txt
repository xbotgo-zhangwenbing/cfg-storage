cmake_minimum_required(VERSION 3.20)
project(xbotgo-config-storage-demo VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FALCONM_OBS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../falconm-obs" CACHE PATH "Path to falconm-obs")
set(FALCONM_OBS_BUILD_DIR "${FALCONM_OBS_ROOT}/build_macos" CACHE PATH "Path to falconm-obs build")
list(APPEND CMAKE_MODULE_PATH "${FALCONM_OBS_ROOT}/cmake/finders")
list(PREPEND CMAKE_PREFIX_PATH "${FALCONM_OBS_ROOT}/.deps/obs-deps-2026-07-15-universal" "${FALCONM_OBS_ROOT}/.deps/obs-deps-qt6-2026-07-15-universal")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(libobs CONFIG REQUIRED PATHS "${FALCONM_OBS_BUILD_DIR}/libobs" NO_DEFAULT_PATH)
set(STORAGE_SOURCES
    include/xbotgo/storage/common/EnumString.h
    src/common/StorageValidation.h
    include/xbotgo/storage/ImageParametersTypes.h
    include/xbotgo/storage/IImageParametersConfig.h
    include/xbotgo/storage/XBotGoConfigService.h
    src/imageparam/ImageParametersConfigImpl.h src/imageparam/ImageParametersConfigImpl.cpp
    src/imageparam/ImageParametersCodec.h src/imageparam/ImageParametersCodec.cpp
    src/index/IndexState.h src/index/IndexCodec.h src/index/IndexCodec.cpp
    src/io/ObsData.h src/io/StorageWorker.h src/io/StorageWorker.cpp
    src/XBotGoConfigService.cpp
    src/XBotGoConfigServiceImpl.h src/XBotGoConfigServiceImpl.cpp)

add_executable(xbotgo-config-demo
    demo/main.cpp demo/ConfigDemoWindow.h demo/ConfigDemoWindow.cpp
    ${STORAGE_SOURCES})
target_include_directories(xbotgo-config-demo PRIVATE include src)
target_link_libraries(xbotgo-config-demo PRIVATE OBS::libobs Qt6::Core Qt6::Widgets)
set_target_properties(xbotgo-config-demo PROPERTIES AUTOMOC ON)
set_property(TARGET xbotgo-config-demo APPEND PROPERTY BUILD_RPATH "${FALCONM_OBS_ROOT}/.deps/obs-deps-2026-07-15-universal/lib")

include(CTest)
if(BUILD_TESTING)
    add_executable(storage-flow-test tests/StorageFlowTest.cpp ${STORAGE_SOURCES})
    set_target_properties(storage-flow-test PROPERTIES AUTOMOC ON)
    target_include_directories(storage-flow-test PRIVATE include src)
    target_link_libraries(storage-flow-test PRIVATE OBS::libobs Qt6::Core)
    set_property(TARGET storage-flow-test APPEND PROPERTY BUILD_RPATH "${FALCONM_OBS_ROOT}/.deps/obs-deps-2026-07-15-universal/lib")
    add_test(NAME storage-flow COMMAND storage-flow-test)
    set_tests_properties(storage-flow PROPERTIES TIMEOUT 30)
endif()
