# FindViconDataStreamSDK.cmake
#
# Finds the Vicon DataStream SDK.
# Set VICON_SDK_DIR to the root of your Vicon DataStream SDK installation.
#
# Creates imported target: ViconDataStreamSDK::ViconDataStreamSDK

if(NOT VICON_SDK_DIR)
    set(VICON_SDK_DIR "" CACHE PATH "Path to Vicon DataStream SDK installation")
endif()

find_path(VICON_SDK_INCLUDE_DIR
    NAMES DataStreamClient.h
    PATHS
        ${VICON_SDK_DIR}
        ${VICON_SDK_DIR}/include
        $ENV{VICON_SDK_DIR}
        $ENV{VICON_SDK_DIR}/include
    PATH_SUFFIXES
        ViconDataStreamSDK/DataStreamClient
        DataStreamClient
)

find_library(VICON_SDK_LIBRARY
    NAMES ViconDataStreamSDK_CPP
    PATHS
        ${VICON_SDK_DIR}
        ${VICON_SDK_DIR}/lib
        ${VICON_SDK_DIR}/lib64
        $ENV{VICON_SDK_DIR}
        $ENV{VICON_SDK_DIR}/lib
        $ENV{VICON_SDK_DIR}/lib64
    PATH_SUFFIXES
        Release
        x64/Release
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ViconDataStreamSDK
    REQUIRED_VARS VICON_SDK_LIBRARY VICON_SDK_INCLUDE_DIR
    FAIL_MESSAGE
        "Could not find Vicon DataStream SDK. Set VICON_SDK_DIR to the SDK root directory.\n"
        "  Download from: https://www.vicon.com/software/datastream-sdk/\n"
        "  Usage: cmake -DVICON_SDK_DIR=/path/to/sdk .."
)

if(ViconDataStreamSDK_FOUND AND NOT TARGET ViconDataStreamSDK::ViconDataStreamSDK)
    add_library(ViconDataStreamSDK::ViconDataStreamSDK UNKNOWN IMPORTED)
    set_target_properties(ViconDataStreamSDK::ViconDataStreamSDK PROPERTIES
        IMPORTED_LOCATION "${VICON_SDK_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${VICON_SDK_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(VICON_SDK_INCLUDE_DIR VICON_SDK_LIBRARY)
