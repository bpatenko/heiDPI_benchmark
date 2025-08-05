cmake_minimum_required(VERSION 3.20)
project(MonoProject)

# Globale Einstellungen (optional)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Teilprojekte einbinden
add_subdirectory(benchmark)
add_subdirectory(heidpi_logger_cpp_port1)
add_subdirectory(heidpi_logger_cpp_port2)
