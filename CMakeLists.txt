cmake_minimum_required(VERSION 3.13)
project(MiniPB)
cmake_policy(SET CMP0077 NEW) # Ignore normal variables in option

option(MINIPB_BUILD_TESTS "Configure CMake to build tests (or not)" OFF)
option(MINIPB_BUILD_GENERATOR "Configure CMake to build tests (or not)" OFF)
if(MINIPB_BUILD_TESTS AND NOT MINIPB_BUILD_GENERATOR)
    set(MINIPB_BUILD_GENERATOR ON CACHE BOOL "")
    set(MINIPB_BUILD_GENERATOR ON)
    message(STATUS "MINIPB_BUILD_GENERATOR was automatically enabled as a dependency of MINIPB_BUILD_TESTS")
endif()

add_library(minipb INTERFACE)
target_include_directories(minipb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_compile_features(minipb INTERFACE cxx_std_11)

if(MINIPB_BUILD_GENERATOR)
    find_package(Protobuf REQUIRED)
    add_executable(proto-minipb ${CMAKE_CURRENT_SOURCE_DIR}/src/minipb_generator.cpp)
    target_link_libraries(proto-minipb protobuf::libprotobuf protobuf::libprotoc)
    target_include_directories(proto-minipb
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${PROTOBUF_INCLUDE_DIRS}
    )

    function(PROTOBUF_GENERATE_MINIPB SRCS HDRS)
    if(NOT ARGN)
        message(SEND_ERROR "Error: PROTOBUF_GENERATE_MINIPB() called without any proto files")
        return()
    endif()

    if(PROTOBUF_GENERATE_CPP_APPEND_PATH) # This variable is common for all types of output.
        # Create an include path for each file specified
        foreach(FIL ${ARGN})
        get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
        get_filename_component(ABS_PATH ${ABS_FIL} PATH)
        list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
        if(${_contains_already} EQUAL -1)
            list(APPEND _protobuf_include_path -I ${ABS_PATH})
        endif()
        endforeach()
    else()
        set(_protobuf_include_path -I ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(DEFINED PROTOBUF_IMPORT_DIRS)
        foreach(DIR ${Protobuf_IMPORT_DIRS})
        get_filename_component(ABS_PATH ${DIR} ABSOLUTE)
        list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
        if(${_contains_already} EQUAL -1)
            list(APPEND _protobuf_include_path -I ${ABS_PATH})
        endif()
        endforeach()
    endif()

    set(${SRCS})
    set(${HDRS})
    foreach(FIL ${ARGN})
        get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
        get_filename_component(FILE ${FIL} NAME)

        list(APPEND ${SRCS} "${CMAKE_CURRENT_BINARY_DIR}/${FILE}.cpp")
        list(APPEND ${HDRS} "${CMAKE_CURRENT_BINARY_DIR}/${FILE}.h")

        add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FILE}.cpp"
                "${CMAKE_CURRENT_BINARY_DIR}/${FILE}.h"
        COMMAND  ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --minipb_out=${CMAKE_CURRENT_BINARY_DIR}
            --plugin=protoc-gen-minipb=$<TARGET_FILE:proto-minipb>
            ${_protobuf_include_path} ${ABS_FIL}
        DEPENDS ${ABS_FIL} ${Protobuf_PROTOC_EXECUTABLE}
        COMMENT "Running MiniPB C++ protocol buffer compiler on ${FIL}"
        VERBATIM)
    endforeach()

    set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)
    endfunction()
else()
    function(PROTOBUF_GENERATE_MINIPB SRCS HDRS)
        message(FATAL "MINIPB_BUILD_GENERATOR not enabled")
    endfunction()
endif()

if(MINIPB_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)
    find_package(GTest REQUIRED)
    PROTOBUF_GENERATE_MINIPB(SAMPLE_SRCS SAMPLE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/src/sample.proto)
    add_executable(minipb-test
        ${SAMPLE_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp
    )
    target_include_directories(minipb-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_options(minipb-test PRIVATE -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-error=deprecated-declarations)
    target_compile_options(minipb-test PRIVATE "$<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-Weffc++>")
    target_compile_options(minipb-test PRIVATE "$<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-Wold-style-cast>")
    target_link_libraries(minipb-test minipb GTest::gtest GTest::gtest_main)
    gtest_discover_tests(minipb-test)
endif()