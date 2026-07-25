cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)

project(NeoSSFCpp VERSION 1.1.0 DESCRIPTION "C++17/wxWidgets SSF soundset editor" LANGUAGES CXX)
set(NEOSHARED_ROOT "" CACHE PATH
    "Path to the neoshared source repository; defaults to ../neoshared")

if(NOT NEOSHARED_ROOT AND DEFINED ENV{NEOSHARED_ROOT}
   AND NOT "$ENV{NEOSHARED_ROOT}" STREQUAL "")
    set(NEOSHARED_ROOT "$ENV{NEOSHARED_ROOT}" CACHE PATH
        "Path to the neoshared source repository" FORCE)
endif()

if(NOT NEOSHARED_ROOT)
    get_filename_component(NEOSHARED_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/../neoshared" ABSOLUTE)
else()
    get_filename_component(NEOSHARED_ROOT "${NEOSHARED_ROOT}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()
set(NEOSHARED_ROOT "${NEOSHARED_ROOT}" CACHE PATH
    "Path to the neoshared source repository" FORCE)

if(NOT EXISTS "${NEOSHARED_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "neoshared was not found at '${NEOSHARED_ROOT}'. "
        "Clone the neoshared repository beside this repository, or configure "
        "with -DNEOSHARED_ROOT=/path/to/neoshared.")
endif()

if(MSVC)
    foreach(_neo_runtime_flag_var IN ITEMS
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        if(DEFINED ${_neo_runtime_flag_var})
            string(REGEX REPLACE "(^|[ /-])MDd?($|[ ]|/)" "\\1MT\\2" ${_neo_runtime_flag_var} "${${_neo_runtime_flag_var}}")
        endif()
    endforeach()
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(neo_force_static_msvc_runtime target_name)
    if(MSVC AND TARGET ${target_name})
        set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:/MTd>
            $<$<NOT:$<CONFIG:Debug>>:/MT>
        )
    endif()
endfunction()

function(neo_force_static_msvc_runtime_for_directory_targets)
    if(MSVC)
        get_property(_neo_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_neo_target IN LISTS _neo_targets)
            get_target_property(_neo_type ${_neo_target} TYPE)
            if(NOT _neo_type STREQUAL "INTERFACE_LIBRARY" AND NOT _neo_type STREQUAL "UTILITY")
                neo_force_static_msvc_runtime(${_neo_target})
            endif()
        endforeach()
    endif()
endfunction()

include(GNUInstallDirs)

message(STATUS "Using neoshared: ${NEOSHARED_ROOT}")
include("${NEOSHARED_ROOT}/cmake/NeoMinimalRelease.cmake")
if(NOT TARGET neoshared::wx)
    add_subdirectory("${NEOSHARED_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/neoshared" EXCLUDE_FROM_ALL)
endif()



option(NEOSSF_BUILD_CLI "Build the NeoSSF command-line utility" ON)

add_library(neossf_core
    src/core/AppModel.cpp
    src/core/CoreTypes.cpp
    src/core/Common.cpp
    src/core/SSFFile.cpp
    src/core/SSFJson.cpp
    src/core/SsfPatcher.cpp
)
target_include_directories(neossf_core PUBLIC src)
target_link_libraries(neossf_core PUBLIC neoshared::tabular neoshared::json neoshared::xml neoshared::tslpatcher neoshared::tlk)
target_compile_features(neossf_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_definitions(neossf_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(neossf_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NEOSSF_BUILD_CLI)
    add_executable(neossf-cli src/cli/main_cli.cpp)
    target_link_libraries(neossf-cli PRIVATE neossf_core)
    if(NOT MSVC)
        target_compile_options(neossf-cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    install(TARGETS neossf-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()


option(NEOSSF_BUILD_WX_GUI "Build the wxWidgets desktop GUI when wxWidgets is available" ON)
option(NEOSSF_REQUIRE_WX_GUI "Fail configure if wxWidgets is unavailable while NEOSSF_BUILD_WX_GUI is ON" OFF)


function(neossf_link_wx target_name)
    target_compile_definitions(${target_name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    set(_wx_targets)
    foreach(_wx_target IN ITEMS wx::core wx::base wx::adv wx::aui wxWidgets::wxWidgets)
        if(TARGET ${_wx_target})
            list(APPEND _wx_targets ${_wx_target})
        endif()
    endforeach()

    if(_wx_targets)
        target_link_libraries(${target_name} PRIVATE ${_wx_targets})
    else()
        if(wxWidgets_USE_FILE)
            include(${wxWidgets_USE_FILE})
        endif()
        target_include_directories(${target_name} SYSTEM PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(${target_name} PRIVATE ${wxWidgets_DEFINITIONS})
        target_link_libraries(${target_name} PRIVATE ${wxWidgets_LIBRARIES})
    endif()

    neo_force_static_msvc_runtime(${target_name})
endfunction()


if(NEOSSF_BUILD_WX_GUI)
    set(NEOSSF_WX_COMPONENTS core base adv aui)
    # vcpkg provides CMake package targets such as wx::core and wx::base.
    # Prefer CONFIG mode on Windows/vcpkg, but keep FindwxWidgets/wx-config as
    # the first choice on Unix-like systems with traditional wxWidgets installs.
    set(_neo_prefer_wx_config_package OFF)
    if(WIN32 OR DEFINED VCPKG_TARGET_TRIPLET OR CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
        set(_neo_prefer_wx_config_package ON)
    endif()

    if(_neo_prefer_wx_config_package)
        find_package(wxWidgets CONFIG QUIET COMPONENTS ${NEOSSF_WX_COMPONENTS})
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET)
        endif()
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets QUIET MODULE COMPONENTS ${NEOSSF_WX_COMPONENTS})
        endif()
    else()
        find_package(wxWidgets QUIET MODULE COMPONENTS ${NEOSSF_WX_COMPONENTS})
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET COMPONENTS ${NEOSSF_WX_COMPONENTS})
        endif()
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET)
        endif()
    endif()

    if(wxWidgets_FOUND OR TARGET wx::core OR TARGET wxWidgets::wxWidgets)
        set(NEOSSF_WX_SOURCES src/wx/main.cpp)
        if(WIN32)
            list(APPEND NEOSSF_WX_SOURCES resources/neossf.rc)
        endif()

        if(WIN32)
            add_executable(NeoSSF WIN32 ${NEOSSF_WX_SOURCES})
        elseif(APPLE)
            add_executable(NeoSSF MACOSX_BUNDLE ${NEOSSF_WX_SOURCES})
        else()
            add_executable(NeoSSF ${NEOSSF_WX_SOURCES})
        endif()
        target_link_libraries(NeoSSF PRIVATE neossf_core)
        target_include_directories(NeoSSF PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
        target_link_libraries(NeoSSF PRIVATE neoshared::wx)
        neossf_link_wx(NeoSSF)
        target_compile_features(NeoSSF PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(NeoSSF PRIVATE /W4 /permissive-)
        else()
            target_compile_options(NeoSSF PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        install(TARGETS NeoSSF
            BUNDLE DESTINATION .
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        if(UNIX AND NOT APPLE)
            foreach(NEO_ICON_SIZE 16 24 32 48 64 128 256)
                install(FILES resources/icons/${NEO_ICON_SIZE}x${NEO_ICON_SIZE}/neossf.png
                    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${NEO_ICON_SIZE}x${NEO_ICON_SIZE}/apps
                    RENAME neossf.png
                )
            endforeach()
            configure_file(resources/neossf.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/neossf.desktop @ONLY)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/neossf.desktop
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
            )
        endif()
    elseif(NEOSSF_REQUIRE_WX_GUI)
        message(FATAL_ERROR "wxWidgets was not found, but NEOSSF_REQUIRE_WX_GUI is ON. With vcpkg, run: C:/vcpkg/vcpkg.exe install wxwidgets:x64-windows-static and configure with -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static.")
    else()
        message(WARNING "wxWidgets was not found. Core and CLI targets will still build; install wxWidgets to build NeoSSF.")
    endif()
endif()


neo_force_static_msvc_runtime_for_directory_targets()
neo_apply_minimal_release_to_directory_targets()
