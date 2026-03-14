cmake_minimum_required(VERSION 3.20)
project(HedgeFundApp VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Low-latency optimisation flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -DNDEBUG -ffast-math")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3 -fsanitize=address,undefined")

add_compile_options(-Wall -Wextra -fno-omit-frame-pointer)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchDependencies)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Threads REQUIRED)

# Common header-only library shared by backend and frontend
add_library(hf_common INTERFACE)
target_include_directories(hf_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(hf_common INTERFACE nlohmann_json::nlohmann_json)

add_subdirectory(backend)
add_subdirectory(frontend)
