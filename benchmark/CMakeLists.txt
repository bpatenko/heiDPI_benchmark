cmake_minimum_required(VERSION 3.10)
project(heidpi_benchmark)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(/usr/local/include)

set(SRC_FILES
        src/main.cpp
        src/generator.cpp
        src/scenario.cpp
        src/status.cpp
        src/watcher.cpp
        src/analyzer.cpp
        src/logger_launcher.cpp
        src/config.cpp
)
add_executable(benchmark ${SRC_FILES})
target_link_libraries(benchmark PRIVATE pthread nlohmann_json::nlohmann_json)
