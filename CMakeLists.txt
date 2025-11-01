cmake_minimum_required(VERSION 3.14)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)
project(NeoRampAgent VERSION "1.0.3")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(DIR_SEPARATOR="\\\\")
else()
    add_compile_definitions(DIR_SEPARATOR="/")
endif()

if(DEV)
    message(STATUS "Development mode enabled")
    add_compile_definitions(DEV=1)
endif()


configure_file(
    ${CMAKE_SOURCE_DIR}/src/Version.h.in
    ${CMAKE_BINARY_DIR}/Version.h
)

if(NOT DEFINED AUTH_SECRET)
    set(AUTH_SECRET "")
endif()
configure_file(
    ${CMAKE_SOURCE_DIR}/src/Secret.h.in
    ${CMAKE_BINARY_DIR}/Secret.h
    @ONLY
)

# set DEBUG mode
if (DEBUG)
    add_compile_definitions(
        DEBUG=1
    )
    message(STATUS "DEBUG mode enabled")
endif()

# Add SDK include directory
include_directories(
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Find external dependencies
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory(External/NeoRadarSDK)

# Source files
# To set after starting development
set(SOURCES
    src/NeoRampAgent.cpp
    src/main.cpp
)

# Define the plugin library
add_library(${PROJECT_NAME} SHARED ${SOURCES}  "")


# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE 
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    NeoRadarSDK::NeoRadarSDK
)

# Link Apple frameworks required by httplib's macOS code path
if(APPLE)
    # Prefer explicit framework flags for portability on CI / Xcode
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreFoundation" "-framework CFNetwork")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# Set output directory and properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PREFIX ""  # Remove "lib" prefix on Unix-like systems
)

# Add processor-specific output name for Apple platforms
if(${CMAKE_HOST_APPLE})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}
    )
endif()
