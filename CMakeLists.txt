cmake_minimum_required(VERSION 3.20)
project(hyni_websocket_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0167 NEW)

# Find required packages
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_path(CONCURRENTQUEUE_INCLUDE_DIR
    NAMES moodycamel/concurrentqueue.h
    PATHS /usr/include/concurrentqueue
          /usr/local/include/concurrentqueue
          /usr/include
          /usr/local/include
)

if(NOT CONCURRENTQUEUE_INCLUDE_DIR)
    if(EXISTS /etc/fedora-release)
        message(FATAL_ERROR
            "Could not find moodycamel/concurrentqueue.h\n"
            "On Fedora, install it with:\n"
            "  sudo dnf install moodycamel-concurrentqueue-devel\n"
            "Searched in: /usr/include/concurrentqueue")
    else()
        message(FATAL_ERROR
            "Could not find moodycamel/concurrentqueue.h\n"
            "Please install the concurrentqueue development headers.\n"
            "Searched in: /usr/include/concurrentqueue")
    endif()
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    message("Found ccache ${CCACHE_FOUND}")
    message("Using ccache to speed up compilation")
    set(ENV{CCACHE_CPP2} "yes")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_FOUND})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_FOUND})
endif(CCACHE_FOUND)

# Detect CPU architecture and set march/mcpu flags
set(MARCH_NATIVE "")  # Default to empty; fallback to -march=native later

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    # Detect Apple M1/M2/M3 CPUs
    execute_process(
        COMMAND sysctl -n machdep.cpu.brand_string
        OUTPUT_VARIABLE APPLE_CPU_BRAND
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Detected Apple CPU: ${APPLE_CPU_BRAND}")

    if(APPLE_CPU_BRAND MATCHES "M3")
        set(MARCH_NATIVE "-mcpu=apple-m3")
    elseif(APPLE_CPU_BRAND MATCHES "M2")
        set(MARCH_NATIVE "-mcpu=apple-m2")
    elseif(APPLE_CPU_BRAND MATCHES "M1")
        set(MARCH_NATIVE "-mcpu=apple-m1")
    else()
        message(WARNING "Unknown Apple Silicon CPU, using default flags")
    endif()

elseif(UNIX AND NOT APPLE)
    find_program(LSCPU lscpu)
    if(LSCPU)
        execute_process(
            COMMAND ${LSCPU}
            OUTPUT_VARIABLE LSCPU_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "Model name:[^\n]+" CPU_LINE "${LSCPU_OUTPUT}")
        string(REGEX REPLACE ".*Model name:[ \t]*" "" CPU_MODEL "${CPU_LINE}")

        message(STATUS "Detected CPU model: ${CPU_MODEL}")

        if(CPU_MODEL MATCHES "Zen 2")
            set(MARCH_NATIVE "-march=znver2")
        elseif(CPU_MODEL MATCHES "Zen 3")
            set(MARCH_NATIVE "-march=znver3")
        elseif(CPU_MODEL MATCHES "Zen 4")
            set(MARCH_NATIVE "-march=znver4")
        elseif(CPU_MODEL MATCHES "Alder Lake")
            set(MARCH_NATIVE "-march=alderlake")
        elseif(CPU_MODEL MATCHES "Ryzen AI 9 HX 370")
            set(MARCH_NATIVE "-march=znver5")
        endif()
    endif()
endif()

if(MARCH_NATIVE STREQUAL "")
    set(MARCH_NATIVE "-march=native")
endif()

message(STATUS "Using architecture-specific flags: ${MARCH_NATIVE}")

string(
    APPEND opts
        "$<$<AND:$<CONFIG:RELEASE>,$<COMPILE_LANGUAGE:CXX>>:-Wall;-Wno-deprecated-declarations;-O3;${MARCH_NATIVE};-std=c++20;-fPIC;-flto;-fomit-frame-pointer;>"
        "$<$<AND:$<CONFIG:DEBUG>,$<COMPILE_LANGUAGE:CXX>>:-Wall;-Wno-deprecated-declarations;-O0;-g3;-std=c++20;-fPIC>"
        "$<$<AND:$<CONFIG:RELWITHDEBINFO>,$<COMPILE_LANGUAGE:CXX>>:-Wall;-Wno-deprecated-declarations;-O3;${MARCH_NATIVE};-g;-std=c++20;-fPIC;-flto;-fomit-frame-pointer;>"
        "$<$<AND:$<CONFIG:RELEASE>,$<COMPILE_LANGUAGE:C>>:-Wall;-Wno-deprecated-declarations;-O3;${MARCH_NATIVE};-fPIC;-flto;-fomit-frame-pointer;>"
        "$<$<AND:$<CONFIG:DEBUG>,$<COMPILE_LANGUAGE:C>>:-Wall;-Wno-deprecated-declarations;-O0;-g3;-fPIC>"
        "$<$<AND:$<CONFIG:RELWITHDEBINFO>,$<COMPILE_LANGUAGE:C>>:-Wall;-Wno-deprecated-declarations;-O3;${MARCH_NATIVE};-g;-fPIC;-flto;-fomit-frame-pointer;>"
)
add_compile_options("${opts}")

# Library target
add_library(${PROJECT_NAME} STATIC
    src/hyni_websocket_server.h
    src/hyni_websocket_server.cpp
    src/hyni_audio_data.h
)

message(STATUS "-------------- ${CONCURRENTQUEUE_INCLUDE_DIR}")

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CONCURRENTQUEUE_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include/hyni>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    nlohmann_json::nlohmann_json
    Boost::system
    Threads::Threads
)

# Tests
if(BUILD_TESTING)
    enable_testing()

    # Find Google Test
    find_package(GTest REQUIRED)

    # Test executable
    set(TEST_TARGET wstream_tests)

    set(TEST_SOURCES
        tests/test_websocket_server.cpp
        tests/test_websocket_server_command.cpp
    )

    add_executable(${TEST_TARGET} ${TEST_SOURCES})

    target_link_libraries(${TEST_TARGET} PRIVATE
        ${PROJECT_NAME}
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
    )

    target_include_directories(${TEST_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Add tests to CTest
    include(GoogleTest)
    gtest_discover_tests(${TEST_TARGET}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTIES TIMEOUT 120
    )

    # Custom target to run tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${TEST_TARGET}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()


# Install targets
install(TARGETS ${PROJECT_NAME}
    EXPORT hyni_websocket_serverTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES
    src/hyni_websocket_server.h
    src/hyni_audio_data.h
    DESTINATION include/hyni
)

include(CMakePackageConfigHelpers)

# Create a directory for the config files
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hyni_websocket_serverConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hyni_websocket_serverConfig.cmake"
    @ONLY
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hyni_websocket_serverConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

# Export targets
install(EXPORT hyni_websocket_serverTargets
    FILE hyni_websocket_serverTargets.cmake
    NAMESPACE hyni_websocket_server::
    DESTINATION lib/cmake/hyni_websocket_server
)

# Install config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/hyni_websocket_serverConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/hyni_websocket_serverConfigVersion.cmake"
    DESTINATION lib/cmake/hyni_websocket_server
)
