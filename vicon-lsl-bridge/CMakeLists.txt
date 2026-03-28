cmake_minimum_required(VERSION 3.23)
project(vicon-lsl-bridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(liblsl
    GIT_REPOSITORY https://github.com/sccn/liblsl.git
    GIT_TAG        v1.16.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(liblsl)

add_subdirectory(external/vicon-datastream-sdk)

add_executable(vicon-lsl-bridge
    src/main.cpp
    src/ViconClient.cpp
    src/MarkerStream.cpp
    src/SegmentStream.cpp
    src/ViconLSLBridge.cpp
)

target_link_libraries(vicon-lsl-bridge PRIVATE
    lsl
    ViconDataStreamSDK_CPP
)

target_include_directories(vicon-lsl-bridge PRIVATE src)
