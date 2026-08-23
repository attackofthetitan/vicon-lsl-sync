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
        tests/LabRecorderFilenameTests.cpp
        tests/BridgeWindowSettingsTests.cpp
        tests/LabRecorderRuntimePolicyTests.cpp
        tests/LabRecorderClientProtocolTests.cpp
        src/gui/BridgeWindowSettings.cpp
        src/gui/LabRecorderClient.cpp
        src/gui/LabRecorderFilenamePolicy.cpp
        src/gui/LabRecorderRuntimePolicy.cpp
    )
    target_include_directories(vicon-lsl-labrecorder-tests PRIVATE src)
    target_link_libraries(vicon-lsl-labrecorder-tests PRIVATE Qt6::Core Qt6::Network)
    set_target_properties(vicon-lsl-labrecorder-tests PROPERTIES AUTOMOC ON)
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

if(VICON_LSL_BRIDGE_BUILD_RUNTIME)
    set(VICON_LSL_RUNTIME_TEST_ENV
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${VICON_LSL_LIB_TARGET}>")

    add_executable(vicon-lsl-bridge-lifecycle-tests
        tests/test_bridge_lifecycle.cpp
    )
    target_include_directories(vicon-lsl-bridge-lifecycle-tests PRIVATE src)
    target_link_libraries(vicon-lsl-bridge-lifecycle-tests PRIVATE
        vicon-lsl-bridge-runtime
    )
    add_test(NAME vicon-lsl-bridge-lifecycle-tests
        COMMAND vicon-lsl-bridge-lifecycle-tests)
    set_tests_properties(vicon-lsl-bridge-lifecycle-tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "${VICON_LSL_RUNTIME_TEST_ENV}"
    )

    add_executable(vicon-lsl-stream-recovery-tests
        tests/test_stream_recovery.cpp
    )
    target_include_directories(vicon-lsl-stream-recovery-tests PRIVATE src)
    target_link_libraries(vicon-lsl-stream-recovery-tests PRIVATE
        vicon-lsl-bridge-runtime
    )
    add_test(NAME vicon-lsl-stream-recovery-tests
        COMMAND vicon-lsl-stream-recovery-tests)
    set_tests_properties(vicon-lsl-stream-recovery-tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "${VICON_LSL_RUNTIME_TEST_ENV}"
    )
endif()
