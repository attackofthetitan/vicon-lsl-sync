cmake_minimum_required(VERSION 3.23)
project(vicon-lsl-bridge VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601)
endif()

option(VICON_LSL_USE_SYSTEM_LIBLSL "Use an installed liblsl package instead of FetchContent" OFF)

if(VICON_LSL_USE_SYSTEM_LIBLSL)
    find_package(LSL CONFIG REQUIRED)
    if(TARGET LSL::lsl)
        set(VICON_LSL_LIB_TARGET LSL::lsl)
    elseif(TARGET lsl)
        set(VICON_LSL_LIB_TARGET lsl)
    else()
        message(FATAL_ERROR "Found LSL package, but no LSL::lsl or lsl target was exported")
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(liblsl
        GIT_REPOSITORY https://github.com/sccn/liblsl.git
        GIT_TAG        v1.16.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(liblsl)
    set(VICON_LSL_LIB_TARGET lsl)
endif()

add_subdirectory(external/vicon-datastream-sdk EXCLUDE_FROM_ALL)

set(VICON_LSL_BRIDGE_SOURCES
    src/CommandLine.cpp
    src/ViconClient.cpp
    src/HoloLensGazeReceiver.cpp
    src/MarkerStream.cpp
    src/SegmentStream.cpp
    src/ViconLSLBridge.cpp
)

add_library(vicon-lsl-bridge-core STATIC ${VICON_LSL_BRIDGE_SOURCES})

target_link_libraries(vicon-lsl-bridge-core PUBLIC
    ${VICON_LSL_LIB_TARGET}
    ViconDataStreamSDK_CPP
)

target_include_directories(vicon-lsl-bridge-core PUBLIC src)
target_compile_definitions(vicon-lsl-bridge-core PUBLIC _BUILD_STATIC_LIB)

if(WIN32)
    target_link_libraries(vicon-lsl-bridge-core PUBLIC ws2_32)
endif()

add_executable(vicon-lsl-bridge src/main.cpp)
target_link_libraries(vicon-lsl-bridge PRIVATE vicon-lsl-bridge-core)

# Optional Qt6 GUI
find_package(Qt6 COMPONENTS Core Widgets Network QUIET)
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)

    add_executable(vicon-lsl-bridge-gui
        src/gui/main_gui.cpp
        src/gui/BridgeWindow.cpp
        src/gui/LabRecorderClient.cpp
    )

    target_link_libraries(vicon-lsl-bridge-gui PRIVATE
        vicon-lsl-bridge-core
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )

    target_include_directories(vicon-lsl-bridge-gui PRIVATE src)
    target_compile_definitions(vicon-lsl-bridge-gui PRIVATE _BUILD_STATIC_LIB)

    if(WIN32)
        target_link_libraries(vicon-lsl-bridge-gui PRIVATE ws2_32)
        set_target_properties(vicon-lsl-bridge-gui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    message(STATUS "Qt6 found - building vicon-lsl-bridge-gui")
else()
    message(STATUS "Qt6 not found - skipping GUI build")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(vicon-lsl-bridge-tests
        tests/test_core.cpp
    )
    set_target_properties(vicon-lsl-bridge-tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(vicon-lsl-bridge-tests PRIVATE vicon-lsl-bridge-core)
    add_test(NAME vicon-lsl-bridge-tests COMMAND vicon-lsl-bridge-tests)

    if(Qt6_FOUND)
        add_executable(vicon-lsl-labrecorder-tests
            tests/test_labrecorder_client.cpp
            src/gui/LabRecorderClient.cpp
        )
        target_include_directories(vicon-lsl-labrecorder-tests PRIVATE src)
        target_link_libraries(vicon-lsl-labrecorder-tests PRIVATE Qt6::Core Qt6::Network)
        add_test(NAME vicon-lsl-labrecorder-tests COMMAND vicon-lsl-labrecorder-tests)
    endif()
endif()
