cmake_minimum_required(VERSION 3.16)
project(heidpi_cpp)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3  # oder neuere Version
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
        json-schema-validator
        GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
        GIT_TAG main
)
FetchContent_MakeAvailable(json-schema-validator)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG f732014  # oder neuer
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
        CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.3.2  # oder neueste Version
)
FetchContent_MakeAvailable(CLI11)

add_executable(heidpi_cpp
        heidpi_srvd.cpp
        heidpi_srvd.h
        heidpi_env.cpp
        heidpi_env.h
        heidpi_logger.cpp
        heidpi_logger.h
        thread_pool.cpp
        thread_pool.h)
target_include_directories(heidpi_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/exprtk)
target_include_directories(heidpi_cpp PRIVATE ${yaml-cpp_SOURCE_DIR}/include)
target_link_libraries(heidpi_cpp PRIVATE
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        CLI11::CLI11
        yaml-cpp
)