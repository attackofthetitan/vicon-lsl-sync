include(CTest)

if(NOT BUILD_TESTING)
    return()
endif()

set(VICON_LSL_BRIDGE_TEST_SOURCES
    tests/StreamSchemaTests.cpp
    tests/CommandLineTests.cpp
    tests/PreviewParsingTests.cpp
    tests/PreviewCalibrationTests.cpp
    tests/PreviewPlaybackTests.cpp
    tests/PreviewCsvTests.cpp
    tests/PreviewFrameAssemblerTests.cpp
    tests/PreviewXdfTests.cpp
    tests/PreviewRateTests.cpp
    tests/PreviewDeliveryTests.cpp
    tests/ViconFrameMapperTests.cpp
)

find_package(Catch2 3 QUIET)
if(NOT TARGET Catch2::Catch2WithMain AND VICON_LSL_BRIDGE_FETCH_CATCH2)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        6ee0826dcae55ed1e06b2c5701981221e979e1e6 # v3.15.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(vicon-lsl-bridge-logic-tests ${VICON_LSL_BRIDGE_TEST_SOURCES})
target_link_libraries(vicon-lsl-bridge-logic-tests PRIVATE vicon-lsl-bridge-logic)

if(TARGET Catch2::Catch2WithMain)
    target_compile_definitions(vicon-lsl-bridge-logic-tests PRIVATE VICON_LSL_USE_CATCH2)
    target_link_libraries(vicon-lsl-bridge-logic-tests PRIVATE
        Catch2::Catch2WithMain
    )
    include(Catch)
    catch_discover_tests(vicon-lsl-bridge-logic-tests)
else()
    message(STATUS "Catch2 not found - using bundled dependency-light test harness")
    target_sources(vicon-lsl-bridge-logic-tests PRIVATE tests/TestMain.cpp)
    add_test(NAME vicon-lsl-bridge-logic-tests COMMAND vicon-lsl-bridge-logic-tests)
endif()

target_compile_definitions(vicon-lsl-bridge-logic-tests PRIVATE
    VICON_LSL_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(NOT VICON_LSL_BRIDGE_BUILD_RUNTIME)
    return()
endif()

# Lets the test programs find the liblsl library they were built with.
set(VICON_LSL_RUNTIME_TEST_ENV
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:${VICON_LSL_LIB_TARGET}>")
if(APPLE)
    list(APPEND VICON_LSL_RUNTIME_TEST_ENV
        "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:${VICON_LSL_LIB_TARGET}>"
        "DYLD_FRAMEWORK_PATH=path_list_prepend:$<TARGET_FILE_DIR:${VICON_LSL_LIB_TARGET}>")
endif()

if(TARGET vicon-lsl-bridge-gui-components)
    # Qt tests also need the Qt libraries and a platform that works without a display.
    set(VICON_LSL_QT_TEST_ENV
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        ${VICON_LSL_RUNTIME_TEST_ENV})
    if(WIN32)
        list(APPEND VICON_LSL_QT_TEST_ENV "QT_QPA_PLATFORM=set:windows")
    else()
        list(APPEND VICON_LSL_QT_TEST_ENV "QT_QPA_PLATFORM=set:offscreen")
    endif()

    # A small program the recorder tests start in place of LabRecorderCLI.
    add_executable(vicon-lsl-recorder-process-fixture tests/RecorderProcessFixture.cpp)

    add_executable(vicon-lsl-labrecorder-tests
        tests/test_labrecorder_client.cpp
        tests/LabRecorderFilenameTests.cpp
        tests/LabRecorderClientProtocolTests.cpp
        tests/SessionGuiModelTests.cpp
        tests/RecordingVerifierTests.cpp
        tests/RecorderProcessControllerTests.cpp
        tests/SessionSequencerTests.cpp
        tests/SetupCheckPolicyTests.cpp
        tests/StreamInventoryTests.cpp
    )
    target_link_libraries(vicon-lsl-labrecorder-tests PRIVATE vicon-lsl-bridge-gui-components)
    add_dependencies(vicon-lsl-labrecorder-tests vicon-lsl-recorder-process-fixture)
    add_test(NAME vicon-lsl-labrecorder-tests COMMAND vicon-lsl-labrecorder-tests)

    add_executable(vicon-lsl-bridge-gui-tests
        tests/test_bridge_gui.cpp
        tests/SessionFlowTests.cpp)
    target_link_libraries(vicon-lsl-bridge-gui-tests PRIVATE vicon-lsl-bridge-gui-components)
    add_dependencies(vicon-lsl-bridge-gui-tests vicon-lsl-recorder-process-fixture)
    add_test(NAME vicon-lsl-bridge-gui-test COMMAND vicon-lsl-bridge-gui-tests --test)
    add_test(NAME vicon-lsl-bridge-gui-scaled-test COMMAND vicon-lsl-bridge-gui-tests --test)

    set_tests_properties(vicon-lsl-labrecorder-tests vicon-lsl-bridge-gui-test PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "${VICON_LSL_QT_TEST_ENV}"
    )
    set_tests_properties(vicon-lsl-bridge-gui-scaled-test PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "${VICON_LSL_QT_TEST_ENV};QT_SCALE_FACTOR=set:1.5"
    )
endif()

add_executable(vicon-lsl-bridge-lifecycle-tests tests/test_bridge_lifecycle.cpp)
target_link_libraries(vicon-lsl-bridge-lifecycle-tests PRIVATE vicon-lsl-bridge-runtime)
add_test(NAME vicon-lsl-bridge-lifecycle-tests COMMAND vicon-lsl-bridge-lifecycle-tests)

add_executable(vicon-lsl-stream-recovery-tests tests/test_stream_recovery.cpp)
target_link_libraries(vicon-lsl-stream-recovery-tests PRIVATE vicon-lsl-bridge-runtime)
add_test(NAME vicon-lsl-stream-recovery-tests COMMAND vicon-lsl-stream-recovery-tests)

set_tests_properties(vicon-lsl-bridge-lifecycle-tests vicon-lsl-stream-recovery-tests PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION "${VICON_LSL_RUNTIME_TEST_ENV}"
)
