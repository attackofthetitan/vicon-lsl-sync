cmake_minimum_required(VERSION 3.23)
project(vicon-lsl-bridge VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VICON_DATASTREAM_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/external/vicon-datastream-sdk")
set(VICON_LSL_BRIDGE_HAS_VICON_SDK OFF)
if(EXISTS "${VICON_DATASTREAM_SDK_DIR}/CMakeLists.txt")
    set(VICON_LSL_BRIDGE_HAS_VICON_SDK ON)
endif()

option(VICON_LSL_BRIDGE_BUILD_RUNTIME
    "Build CLI/GUI targets that require liblsl and the Vicon DataStream SDK"
    ${VICON_LSL_BRIDGE_HAS_VICON_SDK}
)
option(VICON_LSL_BRIDGE_BUILD_GUI "Build the optional Qt6 GUI when Qt6 is available" ON)
option(VICON_LSL_BRIDGE_FETCH_CATCH2
    "Fetch Catch2 when BUILD_TESTING is enabled and Catch2 is not installed"
    ON
)
option(VICON_LSL_USE_SYSTEM_LIBLSL
    "Use an installed liblsl package instead of FetchContent"
    OFF
)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601)
endif()

add_library(vicon-lsl-bridge-logic STATIC
    src/HoloLensGazePacket.cpp
    src/StreamSchema.cpp
    src/CommandLine.cpp
    src/ViconFrameMapper.cpp
)

target_include_directories(vicon-lsl-bridge-logic PUBLIC src)
target_compile_features(vicon-lsl-bridge-logic PUBLIC cxx_std_17)

if(VICON_LSL_BRIDGE_BUILD_RUNTIME)
    if(NOT VICON_LSL_BRIDGE_HAS_VICON_SDK)
        message(FATAL_ERROR
            "VICON_LSL_BRIDGE_BUILD_RUNTIME is ON, but ${VICON_DATASTREAM_SDK_DIR}/CMakeLists.txt "
            "was not found. Initialize the vicon-datastream-sdk submodule, or configure with "
            "-DVICON_LSL_BRIDGE_BUILD_RUNTIME=OFF for dependency-light tests."
        )
    endif()

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

    add_subdirectory(
        "${VICON_DATASTREAM_SDK_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/external/vicon-datastream-sdk"
        EXCLUDE_FROM_ALL
    )

    add_library(vicon-lsl-bridge-runtime STATIC
        src/ViconClient.cpp
        src/HoloLensGazeReceiver.cpp
        src/MarkerStream.cpp
        src/SegmentStream.cpp
        src/ViconLSLBridge.cpp
    )

    target_link_libraries(vicon-lsl-bridge-runtime PUBLIC
        vicon-lsl-bridge-logic
        ${VICON_LSL_LIB_TARGET}
        ViconDataStreamSDK_CPP
    )
    target_include_directories(vicon-lsl-bridge-runtime PUBLIC src)
    target_compile_definitions(vicon-lsl-bridge-runtime PUBLIC _BUILD_STATIC_LIB)

    if(WIN32)
        target_link_libraries(vicon-lsl-bridge-runtime PRIVATE ws2_32)
    endif()

    add_executable(vicon-lsl-bridge src/main.cpp)
    target_link_libraries(vicon-lsl-bridge PRIVATE vicon-lsl-bridge-runtime)

    if(VICON_LSL_BRIDGE_BUILD_GUI)
        find_package(Qt6 COMPONENTS Core Widgets Network QUIET)
        if(Qt6_FOUND)
            set(CMAKE_AUTOMOC ON)

            add_executable(vicon-lsl-bridge-gui
                src/gui/main_gui.cpp
                src/gui/BridgeWindow.cpp
                src/gui/LabRecorderClient.cpp
            )

            target_link_libraries(vicon-lsl-bridge-gui PRIVATE
                vicon-lsl-bridge-runtime
                Qt6::Core
                Qt6::Widgets
                Qt6::Network
            )
            target_include_directories(vicon-lsl-bridge-gui PRIVATE src)
            target_compile_definitions(vicon-lsl-bridge-gui PRIVATE
                VICON_LSL_BRIDGE_VERSION="${PROJECT_VERSION}"
            )

            if(WIN32)
                target_link_libraries(vicon-lsl-bridge-gui PRIVATE ws2_32)
                set_target_properties(vicon-lsl-bridge-gui PROPERTIES WIN32_EXECUTABLE TRUE)
            endif()

            message(STATUS "Qt6 found - building vicon-lsl-bridge-gui")
        else()
            message(STATUS "Qt6 not found - skipping GUI build")
        endif()
    else()
        message(STATUS "VICON_LSL_BRIDGE_BUILD_GUI is OFF - skipping GUI build")
    endif()
else()
    message(STATUS "Vicon/LSL runtime targets disabled - building dependency-light targets only")
endif()

include(CTest)
if(BUILD_TESTING)
    set(VICON_LSL_BRIDGE_TEST_SOURCES
        tests/HoloLensGazePacketTests.cpp
        tests/StreamSchemaTests.cpp
        tests/CommandLineTests.cpp
        tests/ViconFrameMapperTests.cpp
    )

    find_package(Catch2 3 QUIET)
    if(NOT TARGET Catch2::Catch2WithMain AND VICON_LSL_BRIDGE_FETCH_CATCH2)
        include(FetchContent)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.15.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    if(TARGET Catch2::Catch2WithMain)
        add_executable(vicon-lsl-bridge-logic-tests ${VICON_LSL_BRIDGE_TEST_SOURCES})
        target_compile_definitions(vicon-lsl-bridge-logic-tests PRIVATE VICON_LSL_USE_CATCH2)
        target_link_libraries(vicon-lsl-bridge-logic-tests PRIVATE
            vicon-lsl-bridge-logic
            Catch2::Catch2WithMain
        )
        set_target_properties(vicon-lsl-bridge-logic-tests PROPERTIES AUTOMOC OFF)
        include(Catch)
        catch_discover_tests(vicon-lsl-bridge-logic-tests)
    else()
        message(STATUS "Catch2 not found - using bundled dependency-light test harness")
        add_executable(vicon-lsl-bridge-logic-tests
            tests/TestMain.cpp
            ${VICON_LSL_BRIDGE_TEST_SOURCES}
        )
        target_link_libraries(vicon-lsl-bridge-logic-tests PRIVATE vicon-lsl-bridge-logic)
        set_target_properties(vicon-lsl-bridge-logic-tests PROPERTIES AUTOMOC OFF)
        add_test(NAME vicon-lsl-bridge-logic-tests COMMAND vicon-lsl-bridge-logic-tests)
    endif()

    if(VICON_LSL_BRIDGE_BUILD_RUNTIME AND Qt6_FOUND)
        add_executable(vicon-lsl-labrecorder-tests
            tests/test_labrecorder_client.cpp
            src/gui/LabRecorderClient.cpp
        )
        target_include_directories(vicon-lsl-labrecorder-tests PRIVATE src)
        target_link_libraries(vicon-lsl-labrecorder-tests PRIVATE Qt6::Core Qt6::Network)
        add_test(NAME vicon-lsl-labrecorder-tests COMMAND vicon-lsl-labrecorder-tests)
    endif()
endif()
