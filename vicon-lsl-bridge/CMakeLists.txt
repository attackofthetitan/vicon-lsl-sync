cmake_minimum_required(VERSION 3.23)
project(vicon-lsl-bridge VERSION 1.12.0 LANGUAGES CXX)

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
set(VICON_LSL_LABRECORDER_DEPLOY_DIR "" CACHE PATH
    "Windows LabRecorder deployment directory to include under portable package/labrecorder")
set(VICON_LSL_QT_LICENSE_ROOT "" CACHE PATH
    "Exact Qt source-license root containing LICENSES for Windows release packaging")

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601)
endif()

add_library(vicon-lsl-bridge-logic STATIC
    src/HoloLensGazeSchema.cpp
    src/HoloLensModelTargetSchema.cpp
    src/preview/ObjMesh.cpp
    src/preview/PreviewCalibration.cpp
    src/preview/PreviewCsv.cpp
    src/preview/PreviewFrameAssembler.cpp
    src/preview/PreviewMath.cpp
    src/preview/PreviewLoad.cpp
    src/preview/PreviewRate.cpp
    src/preview/PreviewPlaybackClock.cpp
    src/preview/PreviewParsing.cpp
    src/preview/PreviewXdf.cpp
    src/preview/PreviewXdfMetadata.cpp
    src/preview/PreviewXdfMapping.cpp
    src/preview/PreviewXdfReader.cpp
    src/preview/PreviewXdfRecording.cpp
    src/preview/PreviewXdfTimestamps.cpp
    src/StreamSchema.cpp
    src/CommandLine.cpp
    src/ViconFrameMapper.cpp
)

target_include_directories(vicon-lsl-bridge-logic PUBLIC src)
target_compile_features(vicon-lsl-bridge-logic PUBLIC cxx_std_17)

if(WIN32)
    add_executable(vicon-lsl-bridge-portable-launcher WIN32
        packaging/windows/portable_launcher.cpp
        packaging/windows/portable_payload.cpp
        packaging/windows/portable_process.cpp
        packaging/windows/portable_safe_fs.cpp
    )
    target_compile_features(vicon-lsl-bridge-portable-launcher PRIVATE cxx_std_17)
    target_link_libraries(vicon-lsl-bridge-portable-launcher PRIVATE bcrypt)
    set_target_properties(vicon-lsl-bridge-portable-launcher PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
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
            GIT_TAG        6ca188c266c21f7228dc67077303fa6abaf2e8be
            GIT_SHALLOW    FALSE
        )
        FetchContent_MakeAvailable(liblsl)
        set(VICON_LSL_LIB_TARGET lsl)
    endif()

    if(APPLE)
        set(Boost_USE_STATIC_LIBS ON)
        set(CMAKE_MACOSX_RPATH ON)
        set(CMAKE_INSTALL_RPATH "@executable_path;@loader_path;@executable_path/../Frameworks;@loader_path/Frameworks")
    endif()

    add_subdirectory(
        "${VICON_DATASTREAM_SDK_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/external/vicon-datastream-sdk"
        EXCLUDE_FROM_ALL
    )

    add_library(vicon-lsl-bridge-runtime STATIC
        src/detail/ViconNumericOutlet.cpp
        src/ViconClient.cpp
        src/MarkerStream.cpp
        src/SegmentStream.cpp
        src/StreamOutlet.cpp
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
            # Some Qt 6 macOS packages still expose the removed AGL framework
            # through WrapOpenGL, which breaks linking on current macOS SDKs.
            if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
                get_target_property(_wrap_gl_libs
                    WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
                if(_wrap_gl_libs)
                    list(FILTER _wrap_gl_libs EXCLUDE REGEX ".*AGL.*")
                    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
                        INTERFACE_LINK_LIBRARIES "${_wrap_gl_libs}")
                endif()
                unset(_wrap_gl_libs)
            endif()

            add_library(vicon-lsl-bridge-gui-components STATIC
                src/gui/BridgeWindow.cpp
                src/gui/BridgeWindowUi.cpp
                src/gui/LabRecorderClient.cpp
                src/gui/LabRecorderFilenamePolicy.cpp
                src/gui/CalibrationProfileStore.cpp
                src/gui/RecorderProcessController.cpp
                src/gui/RecordingVerifier.cpp
                src/gui/SessionConfiguration.cpp
                src/gui/SessionState.cpp
                src/gui/StreamDiscoveryWorker.cpp
                src/gui/PreviewPanel.cpp
                src/gui/PreviewFileLoader.cpp
                src/gui/PreviewStreamWorker.cpp
                src/gui/PreviewWidget.cpp
            )
            set_target_properties(vicon-lsl-bridge-gui-components PROPERTIES
                AUTOMOC ON)

            target_link_libraries(vicon-lsl-bridge-gui-components PUBLIC
                vicon-lsl-bridge-runtime
                Qt6::Core
                Qt6::Widgets
                Qt6::Network
            )
            target_include_directories(vicon-lsl-bridge-gui-components PUBLIC src)

            if(APPLE)
                add_executable(vicon-lsl-bridge-gui MACOSX_BUNDLE src/gui/main_gui.cpp)
                set_target_properties(vicon-lsl-bridge-gui PROPERTIES
                    MACOSX_BUNDLE_BUNDLE_NAME "Vicon LSL Bridge"
                    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vicon.lslbridge"
                    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
                )
            else()
                add_executable(vicon-lsl-bridge-gui src/gui/main_gui.cpp)
            endif()
            target_link_libraries(vicon-lsl-bridge-gui PRIVATE
                vicon-lsl-bridge-gui-components)
            target_compile_definitions(vicon-lsl-bridge-gui PRIVATE
                VICON_LSL_BRIDGE_VERSION="${PROJECT_VERSION}"
            )

            if(WIN32)
                include(cmake/WindowsPackaging.cmake)
                vicon_lsl_configure_windows_gui_packaging(
                    "${CMAKE_CURRENT_LIST_DIR}" "${VICON_LSL_LIB_TARGET}")
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

include(cmake/Testing.cmake)
