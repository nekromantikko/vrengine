cmake_minimum_required(VERSION 3.22.1)
set(PROJECT_NAME vrengine)
project("${PROJECT_NAME}")

# For FetchContent_Declare() and FetchContent_MakeAvailable()
include(FetchContent)

# openxr_loader - From github.com/KhronosGroup
set(BUILD_TESTS
    OFF
    CACHE INTERNAL "Build tests"
)
set(BUILD_API_LAYERS
    ON
    CACHE INTERNAL "Use OpenXR layers"
)
FetchContent_Declare(
        OpenXR
        URL_HASH MD5=924a94a2da0b5ef8e82154c623d88644
        URL https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/refs/tags/release-1.0.34.zip
        SOURCE_DIR
        openxr
)
FetchContent_MakeAvailable(OpenXR)

# Files
set(SOURCES
        "main.cpp"
        "system.cpp")

set (HEADERS
        "typedef.h"
        "system.h")

set (GLSL_SHADERS
        "shaders/vert.glsl"
        "shaders/test_frag.glsl")

add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
        # In this repo
        ../Common/
        # From OpenXR repo
        "${openxr_SOURCE_DIR}/src/common"
        "${openxr_SOURCE_DIR}/external/include"
        )

# export ANativeActivity_onCreate for java to call.
set_property(
        TARGET ${PROJECT_NAME}
        APPEND_STRING
        PROPERTY LINK_FLAGS " -u ANativeActivity_onCreate"
)

# native_app_glue
include(AndroidNdkModules)
android_ndk_import_module_native_app_glue()

target_link_libraries(${PROJECT_NAME} android native_app_glue openxr_loader)
target_compile_options(${PROJECT_NAME} PRIVATE -Wno-cast-calling-convention)

# VulkanNDK
find_library(vulkan-lib vulkan)
target_include_directories(${PROJECT_NAME} PUBLIC ${ANDROID_NDK}/sources/third_party/vulkan/src/include)
target_link_libraries(${PROJECT_NAME} ${vulkan-lib})

# Compile shaders
set(SHADER_DEST "${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets/shaders")
include("${CMAKE_CURRENT_SOURCE_DIR}/glsl_shader.cmake")
set_source_files_properties(shaders/vert.glsl PROPERTIES ShaderType "vert")
set_source_files_properties(shaders/test_frag.glsl PROPERTIES ShaderType "frag")

foreach(FILE ${GLSL_SHADERS})
    get_filename_component(FILE_WE ${FILE} NAME_WE)
    get_source_file_property(shadertype ${FILE} ShaderType)
    glsl_spv_shader(
            INPUT "${CMAKE_CURRENT_SOURCE_DIR}/${FILE}"
            OUTPUT "${SHADER_DEST}/${FILE_WE}.spv"
            STAGE ${shadertype}
            ENTRY_POINT main
            TARGET_ENV vulkan1.0
    )
    # Make our project depend on these files
    target_sources(${PROJECT_NAME} PRIVATE "${SHADER_DEST}/${FILE_WE}.spv")
endforeach()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PUBLIC NEKRO_DEBUG)
endif()

