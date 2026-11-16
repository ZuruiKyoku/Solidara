cmake_minimum_required(VERSION 3.24)
project(Solidara VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(CTest)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(OpenCASCADE REQUIRED COMPONENTS
    FoundationClasses ModelingData ModelingAlgorithms Visualization
    ApplicationFramework DataExchange)
if(OpenCASCADE_MAJOR_VERSION LESS 7 OR
   (OpenCASCADE_MAJOR_VERSION EQUAL 7 AND OpenCASCADE_MINOR_VERSION LESS 8))
    message(FATAL_ERROR "Solidara requires Open CASCADE 7.8 or newer")
endif()

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(solidara_core STATIC ${CORE_SOURCES})
target_include_directories(solidara_core PUBLIC include)
target_include_directories(solidara_core SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(solidara_core PUBLIC Qt6::Core
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKBool TKFillet TKMesh TKShHealing TKDESTEP TKDESTL)

add_library(solidara_viewport STATIC src/viewport/viewport.cpp)
target_include_directories(solidara_viewport PUBLIC include)
target_include_directories(solidara_viewport SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(solidara_viewport PUBLIC Qt6::Widgets TKService TKV3d TKOpenGl TKTopAlgo)

add_executable(Solidara WIN32
    src/app/main.cpp
    src/ui/main_window.cpp
    resources/resources.qrc)
target_link_libraries(Solidara PRIVATE solidara_core solidara_viewport Qt6::Concurrent)
if(WIN32)
    enable_language(RC)
    configure_file(packaging/Solidara.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Solidara.rc" @ONLY)
    target_sources(Solidara PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Solidara.rc")
endif()
if(MSVC)
    foreach(target solidara_core solidara_viewport Solidara)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PUBLIC NOMINMAX)
    endforeach()
else()
    foreach(target solidara_core solidara_viewport Solidara)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(solidara_core_tests tests/core_tests.cpp)
    target_link_libraries(solidara_core_tests PRIVATE solidara_core Qt6::Test)
    add_test(NAME solidara_core_tests COMMAND solidara_core_tests)
    set_tests_properties(solidara_core_tests PROPERTIES TIMEOUT 120)
    add_executable(solidara_viewport_tests tests/viewport_tests.cpp)
    target_link_libraries(solidara_viewport_tests PRIVATE solidara_viewport solidara_core Qt6::Test)
    if(UNIX AND NOT APPLE)
        find_program(SOLIDARA_XVFB_RUN xvfb-run)
    endif()
    if(SOLIDARA_XVFB_RUN)
        set(gui_test_launcher "${SOLIDARA_XVFB_RUN}" -a -s "-screen 0 1440x900x24")
    endif()
    add_test(NAME solidara_viewport_tests COMMAND ${gui_test_launcher} $<TARGET_FILE:solidara_viewport_tests>)
    add_test(NAME solidara_gui_smoke COMMAND ${gui_test_launcher} $<TARGET_FILE:Solidara>
        --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/smoke-test")
    set_tests_properties(solidara_viewport_tests solidara_gui_smoke PROPERTIES TIMEOUT 120)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(solidara_offline_runner tests/offline_runner.cpp)
        set_target_properties(solidara_offline_runner PROPERTIES AUTOMOC OFF)
        add_test(NAME solidara_core_offline COMMAND solidara_offline_runner $<TARGET_FILE:solidara_core_tests>)
        add_test(NAME solidara_gui_offline COMMAND ${gui_test_launcher}
            $<TARGET_FILE:solidara_offline_runner> $<TARGET_FILE:Solidara>
            --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/smoke-test-offline")
        set_tests_properties(solidara_core_offline solidara_gui_offline PROPERTIES TIMEOUT 120)
        set_tests_properties(solidara_gui_offline PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;LIBGL_ALWAYS_SOFTWARE=1")
    endif()
    if(UNIX AND NOT APPLE)
        set_tests_properties(solidara_viewport_tests solidara_gui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()

install(TARGETS Solidara RUNTIME DESTINATION bin)
install(FILES README.md docs/development/DEPENDENCIES.md DESTINATION share/Solidara)
if(IS_DIRECTORY "${OpenCASCADE_RESOURCE_DIR}")
    install(DIRECTORY "${OpenCASCADE_RESOURCE_DIR}/" DESTINATION share/Solidara/opencascade)
endif()
if(WIN32)
    set(qt_tools_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin")
    foreach(location IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_NONE)
        get_target_property(qmake_path Qt6::qmake "${location}")
        if(qmake_path)
            get_filename_component(qt_tools_dir "${qmake_path}" DIRECTORY)
            list(APPEND qt_tools_dirs "${qt_tools_dir}")
        endif()
    endforeach()
    find_program(SOLIDARA_WINDEPLOYQT NAMES windeployqt windeployqt6
        HINTS ${qt_tools_dirs} REQUIRED)
    set(solidara_deploy_tool "${SOLIDARA_WINDEPLOYQT}")
    if(TARGET Qt6::windeployqt)
        # vcpkg maps Debug to a wrapper using the Debug Qt paths and plugins.
        set(solidara_deploy_tool "$<TARGET_FILE:Qt6::windeployqt>")
    endif()
    add_custom_command(TARGET Solidara POST_BUILD
        COMMAND "${solidara_deploy_tool}" --$<IF:$<CONFIG:Debug>,debug,release>
            --no-translations --no-system-d3d-compiler --no-opengl-sw
            "$<TARGET_FILE:Solidara>"
        COMMENT "Deploy Qt platform plugins for local execution"
        VERBATIM)
    configure_file(packaging/deploy-windows.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/deploy-windows.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy-windows.cmake")
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "Solidara-${PROJECT_VERSION}-windows-x64")
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_NAME Solidara)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR Solidara)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline parametric CAD for makers")
include(CPack)
