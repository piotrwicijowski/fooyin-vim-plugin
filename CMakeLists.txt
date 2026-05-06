cmake_minimum_required(VERSION 3.19)
project(FooyinVimMotions VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Locate fooyin
#
# Two modes:
#   1. Installed: find_package(Fooyin) succeeds — uses installed headers/libs
#      and the create_fooyin_plugin() cmake macro from FooyinMacros.cmake.
#      Requires fooyin installed with -DINSTALL_HEADERS=ON.
#
#   2. Dev tree (default when fooyin is not installed): point directly at the
#      sibling fooyin source tree and its build directory.
#      Only Qt6 needs to be installed; ALSA/FFmpeg/Taglib are not required
#      to build the plugin (they are only needed to build fooyin itself).
# ---------------------------------------------------------------------------
set(FOOYIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fooyin"
    CACHE PATH "Path to fooyin source tree")
set(FOOYIN_BUILD_DIR "${FOOYIN_SRC_DIR}/build"
    CACHE PATH "Path to fooyin CMake build directory")

find_package(Fooyin QUIET)

if(Fooyin_FOUND)
    message(STATUS "Using installed Fooyin")
else()
    message(STATUS "Fooyin not installed — using dev tree: ${FOOYIN_BUILD_DIR}")

    foreach(subdir core gui utils)
        if(NOT EXISTS "${FOOYIN_BUILD_DIR}/src/${subdir}")
            message(FATAL_ERROR
                "fooyin build tree incomplete (missing src/${subdir}) at:\n"
                "  ${FOOYIN_BUILD_DIR}\n"
                "Build fooyin first:\n"
                "  cmake -B ${FOOYIN_BUILD_DIR} ${FOOYIN_SRC_DIR}\n"
                "  cmake --build ${FOOYIN_BUILD_DIR}\n"
                "Or set FOOYIN_BUILD_DIR to an existing build directory.")
        endif()
    endforeach()

    # Generated export headers live in the build tree alongside the libs.
    set(_fooyin_gen_dirs
        "${FOOYIN_BUILD_DIR}/src/core"
        "${FOOYIN_BUILD_DIR}/src/gui"
        "${FOOYIN_BUILD_DIR}/src/utils"
    )

    set(_fooyin_lib_dir "${FOOYIN_BUILD_DIR}/run/lib/fooyin")

    find_library(FOOYIN_CORE_LIB
        NAMES fooyin_core
        PATHS "${_fooyin_lib_dir}"
        NO_DEFAULT_PATH)
    find_library(FOOYIN_GUI_LIB
        NAMES fooyin_gui
        PATHS "${_fooyin_lib_dir}"
        NO_DEFAULT_PATH)

    if(NOT FOOYIN_CORE_LIB OR NOT FOOYIN_GUI_LIB)
        message(FATAL_ERROR
            "Could not find fooyin_core/fooyin_gui in ${_fooyin_lib_dir}.\n"
            "Ensure fooyin is fully built.")
    endif()

    add_library(Fooyin::Core SHARED IMPORTED)
    set_target_properties(Fooyin::Core PROPERTIES
        IMPORTED_LOCATION "${FOOYIN_CORE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${FOOYIN_SRC_DIR}/include;${_fooyin_gen_dirs}")

    add_library(Fooyin::Gui SHARED IMPORTED)
    set_target_properties(Fooyin::Gui PROPERTIES
        IMPORTED_LOCATION "${FOOYIN_GUI_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${FOOYIN_SRC_DIR}/include;${_fooyin_gen_dirs}"
        INTERFACE_LINK_LIBRARIES Fooyin::Core)

    set(FOOYIN_PLUGIN_INSTALL_DIR "lib/fooyin/plugins")
endif()

# ---------------------------------------------------------------------------
# Qt6
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
configure_file(vimmotions.json.in ${CMAKE_CURRENT_BINARY_DIR}/vimmotions.json)

set(PLUGIN_SOURCES
    src/vimmotionsplugin.cpp
    src/vimmotionsplugin.h
    src/vimhandler.cpp
    src/vimhandler.h
    src/viewlocator.cpp
    src/viewlocator.h
)

if(Fooyin_FOUND)
    # Use the official macro (sets output name, rpath, install rules, etc.)
    create_fooyin_plugin(VimMotions
        DEPENDS Fooyin::Gui Qt6::Widgets
        SOURCES ${PLUGIN_SOURCES}
    )
else()
    add_library(fyplugin_vimmotions MODULE ${PLUGIN_SOURCES})

    set_target_properties(fyplugin_vimmotions PROPERTIES
        PREFIX ""
        OUTPUT_NAME "fyplugin_vimmotions"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

    target_include_directories(fyplugin_vimmotions PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(fyplugin_vimmotions PRIVATE
        Fooyin::Gui
        Qt6::Widgets)

    install(TARGETS fyplugin_vimmotions
        LIBRARY DESTINATION ${FOOYIN_PLUGIN_INSTALL_DIR})
endif()
