find_package(GTest QUIET)
if(NOT GTest_FOUND)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2)
    FetchContent_MakeAvailable(googletest)
endif()

set(SRC ${CMAKE_SOURCE_DIR}/src)

# torustiq_add_test(<unit>_test SOURCES <src>... [LIBS <lib>...])
# Each test compiles only the sources it needs.
function(torustiq_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${name}.cpp ${T_SOURCES})
    target_include_directories(${name} PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE torustiq::sdk GTest::gtest_main Threads::Threads
                                          ${T_LIBS})
    torustiq_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

torustiq_add_test(message_test SOURCES ${SRC}/core/message.cpp)
torustiq_add_test(bounded_queue_test)
torustiq_add_test(plugin_registry_test SOURCES ${SRC}/core/plugin_registry.cpp)
torustiq_add_test(paths_test SOURCES ${SRC}/core/paths.cpp)
# POSIX only: the test uses setenv and symlinks.
if(UNIX)
    torustiq_add_test(plugin_loader_test
                      SOURCES ${SRC}/core/plugin_loader.cpp ${SRC}/core/plugin_registry.cpp
                      LIBS ${CMAKE_DL_LIBS})
    # Fake plugin libraries loaded by plugin_loader_test.
    foreach(spec "fake_a:1" "fake_b:1" "fake_bad:999")
        string(REPLACE ":" ";" parts ${spec})
        list(GET parts 0 fake_id)
        list(GET parts 1 fake_abi)
        add_library(${fake_id} MODULE fake_plugin.cpp)
        target_link_libraries(${fake_id} PRIVATE torustiq::sdk)
        target_compile_definitions(${fake_id} PRIVATE FAKE_PLUGIN_ID="${fake_id}"
                                                      FAKE_PLUGIN_ABI=${fake_abi})
        torustiq_set_warnings(${fake_id})
        add_dependencies(plugin_loader_test ${fake_id})
    endforeach()
    target_compile_definitions(plugin_loader_test PRIVATE
        FAKE_PLUGIN_A="$<TARGET_FILE:fake_a>"
        FAKE_PLUGIN_B="$<TARGET_FILE:fake_b>"
        FAKE_PLUGIN_BAD_ABI="$<TARGET_FILE:fake_bad>")
endif()
torustiq_add_test(pipeline_config_test SOURCES ${SRC}/core/pipeline_config.cpp
                  LIBS yaml-cpp::yaml-cpp)
torustiq_add_test(pipeline_test
                  SOURCES ${SRC}/core/pipeline.cpp ${SRC}/core/pipeline_config.cpp
                          ${SRC}/core/message.cpp ${SRC}/core/plugin_registry.cpp
                  LIBS yaml-cpp::yaml-cpp)
torustiq_add_test(stdout_output_test SOURCES ${SRC}/plugins/outputs/stdout_output.cpp
                  ${SRC}/core/message.cpp ${SRC}/core/plugin_registry.cpp)
torustiq_add_test(stdin_input_test SOURCES ${SRC}/plugins/inputs/stdin_input.cpp
                  ${SRC}/core/message.cpp ${SRC}/core/plugin_registry.cpp)
torustiq_add_test(directory_input_test SOURCES ${SRC}/plugins/inputs/directory_input.cpp
                  ${SRC}/core/message.cpp ${SRC}/core/plugin_registry.cpp)

if(TORUSTIQ_WITH_LUA)
    set(LUA_COMMON ${SRC}/plugins/_common/lua/lua_plugin.cpp
                   ${SRC}/plugins/_common/lua/lua_runtime.cpp
                   ${SRC}/core/message.cpp ${SRC}/core/plugin_registry.cpp)
    torustiq_add_test(lua_transform_test
                      SOURCES ${SRC}/plugins/transforms/lua_transform.cpp ${LUA_COMMON}
                      LIBS torustiq::lua)
    torustiq_add_test(lua_input_test
                      SOURCES ${SRC}/plugins/inputs/lua_input.cpp ${LUA_COMMON}
                      LIBS torustiq::lua)
    torustiq_add_test(lua_output_test
                      SOURCES ${SRC}/plugins/outputs/lua_output.cpp ${LUA_COMMON}
                      LIBS torustiq::lua)
    torustiq_add_test(lua_pipeline_test LIBS torustiq_core torustiq_plugins)
    target_compile_definitions(lua_pipeline_test PRIVATE
                               TORUSTIQ_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
endif()
