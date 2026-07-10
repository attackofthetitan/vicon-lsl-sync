cmake_minimum_required(VERSION 3.23)
project(vicon-lsl-bridge VERSION 1.7.1 LANGUAGES CXX)

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
option(VICON_LSL_DEPLOY_QT_RUNTIME
    "Run windeployqt after building the Windows GUI so the build output can run directly"
    ON
)
set(VICON_LSL_ENIGMA_PROJECT "" CACHE FILEPATH
    "Optional Enigma Virtual Box .evb project used by the portable GUI target")
set(VICON_LSL_ENIGMA_CONSOLE "" CACHE FILEPATH
    "Optional path to enigmavbconsole.exe used by the portable GUI target")

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601)
endif()

add_library(vicon-lsl-bridge-logic STATIC
    src/HoloLensGazeSchema.cpp
    src/preview/ObjMesh.cpp
    src/preview/PreviewCalibration.cpp
    src/preview/PreviewCsv.cpp
    src/preview/PreviewMath.cpp
    src/preview/PreviewPlaybackClock.cpp
    src/preview/PreviewParsing.cpp
    src/preview/PreviewXdf.cpp
    src/preview/PreviewXdfReader.cpp
    src/StreamSchema.cpp
    src/CommandLine.cpp
    src/ViconFrameMapper.cpp
)

target_include_directories(vicon-lsl-bridge-logic PUBLIC src)
target_compile_features(vicon-lsl-bridge-logic PUBLIC cxx_std_17)

if(WIN32)
    add_executable(vicon-lsl-bridge-portable-launcher WIN32
        packaging/windows/portable_launcher.cpp
    )
    target_compile_features(vicon-lsl-bridge-portable-launcher PRIVATE cxx_std_17)
endif()

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
        find_package(Qt6 COMPONENTS Core Widgets Network OpenGLWidgets QUIET)
        if(Qt6_FOUND)
            set(CMAKE_AUTOMOC ON)

            add_executable(vicon-lsl-bridge-gui
                src/gui/main_gui.cpp
                src/gui/BridgeWindow.cpp
                src/gui/LabRecorderClient.cpp
                src/gui/PreviewPanel.cpp
                src/gui/PreviewStreamWorker.cpp
                src/gui/PreviewWidget.cpp
            )

            target_link_libraries(vicon-lsl-bridge-gui PRIVATE
                vicon-lsl-bridge-runtime
                Qt6::Core
                Qt6::Widgets
                Qt6::Network
                Qt6::OpenGLWidgets
            )
            target_include_directories(vicon-lsl-bridge-gui PRIVATE src)
            target_compile_definitions(vicon-lsl-bridge-gui PRIVATE
                VICON_LSL_BRIDGE_VERSION="${PROJECT_VERSION}"
            )

            if(WIN32)
                target_link_libraries(vicon-lsl-bridge-gui PRIVATE ws2_32)
                set_target_properties(vicon-lsl-bridge-gui PROPERTIES WIN32_EXECUTABLE TRUE)

                find_program(VICON_LSL_POWERSHELL pwsh powershell)
                find_program(VICON_LSL_WINDEPLOYQT windeployqt)

                if(VICON_LSL_DEPLOY_QT_RUNTIME AND VICON_LSL_WINDEPLOYQT)
                    add_custom_command(TARGET vicon-lsl-bridge-gui POST_BUILD
                        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "$<TARGET_FILE:${VICON_LSL_LIB_TARGET}>"
                            "$<TARGET_FILE_DIR:vicon-lsl-bridge-gui>/$<TARGET_FILE_NAME:${VICON_LSL_LIB_TARGET}>"
                        COMMAND "${VICON_LSL_WINDEPLOYQT}"
                            "$<TARGET_FILE:vicon-lsl-bridge-gui>"
                        COMMENT "Deploy Qt and liblsl runtime files next to vicon-lsl-bridge-gui"
                        VERBATIM
                    )
                endif()

                if(VICON_LSL_POWERSHELL AND VICON_LSL_WINDEPLOYQT)
                    set(VICON_LSL_PORTABLE_STAGE
                        "${CMAKE_CURRENT_BINARY_DIR}/vicon-lsl-bridge-gui-portable-stage")
                    set(VICON_LSL_PORTABLE_PACKAGE_ARGS
                        -DeployDir "${VICON_LSL_PORTABLE_STAGE}"
                        -OutputExe "${CMAKE_CURRENT_BINARY_DIR}/vicon-lsl-bridge-gui-portable.exe"
                        -LauncherExe "$<TARGET_FILE:vicon-lsl-bridge-portable-launcher>"
                    )
                    if(VICON_LSL_ENIGMA_PROJECT)
                        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
                            -Mode Enigma
                            -ProjectFile "${VICON_LSL_ENIGMA_PROJECT}")
                    endif()
                    if(VICON_LSL_ENIGMA_CONSOLE)
                        list(APPEND VICON_LSL_PORTABLE_PACKAGE_ARGS
                            -EnigmaConsole "${VICON_LSL_ENIGMA_CONSOLE}")
                    endif()

                    add_custom_target(vicon-lsl-bridge-gui-portable
                        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${VICON_LSL_PORTABLE_STAGE}"
                        COMMAND "${CMAKE_COMMAND}" -E make_directory "${VICON_LSL_PORTABLE_STAGE}"
                        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "$<TARGET_FILE:vicon-lsl-bridge-gui>"
                            "${VICON_LSL_PORTABLE_STAGE}/vicon-lsl-bridge-gui.exe"
                        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "$<TARGET_FILE:${VICON_LSL_LIB_TARGET}>"
                            "${VICON_LSL_PORTABLE_STAGE}/$<TARGET_FILE_NAME:${VICON_LSL_LIB_TARGET}>"
                        COMMAND "${VICON_LSL_WINDEPLOYQT}"
                            --dir "${VICON_LSL_PORTABLE_STAGE}"
                            "${VICON_LSL_PORTABLE_STAGE}/vicon-lsl-bridge-gui.exe"
                        COMMAND "${VICON_LSL_POWERSHELL}"
                            -ExecutionPolicy Bypass
                            -File "${CMAKE_CURRENT_LIST_DIR}/packaging/windows/package_gui_single_exe.ps1"
                            ${VICON_LSL_PORTABLE_PACKAGE_ARGS}
                        DEPENDS vicon-lsl-bridge-gui vicon-lsl-bridge-portable-launcher
                        COMMENT "Package vicon-lsl-bridge-gui as a single portable Windows executable"
                        VERBATIM
                    )
                else()
                    message(STATUS
                        "pwsh/powershell or windeployqt not found - portable GUI target unavailable")
                endif()
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
        tests/StreamSchemaTests.cpp
        tests/CommandLineTests.cpp
        tests/PreviewCoreTests.cpp
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
        set(VICON_LSL_QT_TEST_ENV
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        if(NOT WIN32)
            list(APPEND VICON_LSL_QT_TEST_ENV
                "QT_QPA_PLATFORM=set:offscreen")
        endif()

        add_executable(vicon-lsl-labrecorder-tests
            tests/test_labrecorder_client.cpp
            src/gui/LabRecorderClient.cpp
        )
        target_include_directories(vicon-lsl-labrecorder-tests PRIVATE src)
        target_link_libraries(vicon-lsl-labrecorder-tests PRIVATE Qt6::Core Qt6::Network)
        add_test(NAME vicon-lsl-labrecorder-tests COMMAND vicon-lsl-labrecorder-tests)
        set_tests_properties(vicon-lsl-labrecorder-tests PROPERTIES
            TIMEOUT 30
            ENVIRONMENT_MODIFICATION "${VICON_LSL_QT_TEST_ENV}"
        )

        if(TARGET vicon-lsl-bridge-gui)
            set(VICON_LSL_GUI_TEST_ENV
                ${VICON_LSL_QT_TEST_ENV}
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:${VICON_LSL_LIB_TARGET}>")
            add_test(NAME vicon-lsl-bridge-gui-test
                COMMAND vicon-lsl-bridge-gui --test)
            set_tests_properties(vicon-lsl-bridge-gui-test PROPERTIES
                TIMEOUT 30
                ENVIRONMENT_MODIFICATION "${VICON_LSL_GUI_TEST_ENV}"
            )
        endif()
    endif()
endif()
