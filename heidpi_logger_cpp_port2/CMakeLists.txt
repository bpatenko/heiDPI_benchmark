cmake_minimum_required(VERSION 3.10)
project(heidpi_cpp VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        f732014
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
        json-schema-validator
        GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
        GIT_TAG main
)
FetchContent_MakeAvailable(json-schema-validator)

FetchContent_Declare(
        maxminddb
        GIT_REPOSITORY https://github.com/maxmind/libmaxminddb.git
        GIT_TAG        1.12.2
        CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DMAXMINDDB_BUILD_BINARIES=OFF
        -DMAXMINDDB_INSTALL=OFF
)
FetchContent_MakeAvailable(maxminddb)

file(GLOB SOURCES src/*.cpp)
add_executable(heidpi_cpp ${SOURCES})
target_include_directories(heidpi_cpp PRIVATE include)
target_link_libraries(heidpi_cpp PRIVATE
        yaml-cpp
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        maxminddb::maxminddb
)

