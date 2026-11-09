cmake_minimum_required(VERSION 3.21)
project(torustiq VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TORUSTIQ_BUILD_TESTS "Build unit tests" ON)
option(TORUSTIQ_WITH_LUA "Build the Lua input/transform/output plugins" ON)

include(FetchContent)
find_package(Threads REQUIRED)

# --- Third-party dependencies: prefer system packages, fall back to FetchContent. ---
find_package(yaml-cpp 0.8 QUIET)
if(NOT yaml-cpp_FOUND)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

find_package(CLI11 2 QUIET)
if(NOT CLI11_FOUND)
    FetchContent_Declare(CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.4.2)
    FetchContent_MakeAvailable(CLI11)
endif()

function(torustiq_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# --- Lua (optional): system package, or built from source. Target: torustiq::lua ---
if(TORUSTIQ_WITH_LUA)
    find_package(Lua 5.4 QUIET)
    if(LUA_FOUND)
        add_library(torustiq_lua INTERFACE)
        target_include_directories(torustiq_lua SYSTEM INTERFACE ${LUA_INCLUDE_DIR})
        target_link_libraries(torustiq_lua INTERFACE ${LUA_LIBRARIES})
    else()
        message(STATUS "System Lua not found, fetching Lua 5.4")
        enable_language(C)
        FetchContent_Declare(lua_src
            GIT_REPOSITORY https://github.com/lua/lua.git
            GIT_TAG v5.4.7)
        FetchContent_MakeAvailable(lua_src)  # Lua has no CMakeLists.txt: this only downloads it
        file(GLOB LUA_SOURCES ${lua_src_SOURCE_DIR}/*.c)
        list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
        add_library(torustiq_lua STATIC ${LUA_SOURCES})
        target_include_directories(torustiq_lua SYSTEM PUBLIC ${lua_src_SOURCE_DIR})
        if(UNIX)
            target_link_libraries(torustiq_lua PUBLIC m)
        endif()
    endif()
    add_library(torustiq::lua ALIAS torustiq_lua)
endif()

# --- SDK: separate project, imported the same way plugins do it. ---
# For a local checkout: -DFETCHCONTENT_SOURCE_DIR_TORUSTIQ_SDK=/path/to/torustiq-sdk
find_package(torustiq_sdk QUIET)
if(NOT torustiq_sdk_FOUND)
    FetchContent_Declare(torustiq_sdk
        GIT_REPOSITORY git@github.com:ignytis/torustiq-sdk.git
        GIT_TAG main)
    FetchContent_MakeAvailable(torustiq_sdk)
endif()

# --- Core (no dependency on concrete plugins). ---
set(TORUSTIQ_CORE_SOURCES
    src/core/message.cpp
    src/core/pipeline.cpp
    src/core/pipeline_config.cpp
    src/core/plugin_registry.cpp)
add_library(torustiq_core STATIC ${TORUSTIQ_CORE_SOURCES})
target_include_directories(torustiq_core PUBLIC src)
target_link_libraries(torustiq_core PUBLIC torustiq::sdk yaml-cpp::yaml-cpp Threads::Threads)
torustiq_set_warnings(torustiq_core)

# --- Built-in plugins (use only the SDK C API). ---
set(TORUSTIQ_PLUGIN_SOURCES
    src/plugins/builtin_plugins.cpp
    src/plugins/directory_input.cpp
    src/plugins/stdin_input.cpp
    src/plugins/stdout_output.cpp)
add_library(torustiq_plugins STATIC ${TORUSTIQ_PLUGIN_SOURCES})
target_include_directories(torustiq_plugins PUBLIC src)
target_link_libraries(torustiq_plugins PUBLIC torustiq::sdk torustiq_core)
if(TORUSTIQ_WITH_LUA)
    target_sources(torustiq_plugins PRIVATE src/plugins/lua_plugins.cpp src/plugins/lua_runtime.cpp)
    target_link_libraries(torustiq_plugins PUBLIC torustiq::lua)
    target_compile_definitions(torustiq_plugins PRIVATE TORUSTIQ_WITH_LUA)
endif()
torustiq_set_warnings(torustiq_plugins)

# --- Executable. ---
add_executable(torustiq src/main.cpp)
target_link_libraries(torustiq PRIVATE torustiq_core torustiq_plugins CLI11::CLI11)
torustiq_set_warnings(torustiq)

if(TORUSTIQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
