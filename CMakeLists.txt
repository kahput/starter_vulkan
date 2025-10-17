cmake_minimum_required(VERSION 3.11)
project(program)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CONFIG $<CONFIG>)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    set(CONFIG ${CMAKE_BUILD_TYPE})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
endif()

include(FetchContent)
FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm.git
  FIND_PACKAGE_ARGS NAMES cglm
)

find_package(Vulkan REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" "deps/*/*.c" "deps/*/*.h")
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC "src/" "deps/")
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS} ${CMAKE_BINARY_DIR} )

# Platform-specific configurations
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
else()
    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
    if (NOT WAYLAND_SCANNER_EXECUTABLE)
        message(FATAL_ERROR "Failed to find wayland-scanner")
    endif()

    macro(generate_wayland_protocol protocol_file)
        set(protocol_path "${CMAKE_SOURCE_DIR}/deps/wayland/${protocol_file}")

        string(REGEX REPLACE "\\.xml$" "-client-protocol.h" header_file ${protocol_file})
        string(REGEX REPLACE "\\.xml$" "-client-protocol-code.h" code_file ${protocol_file})

        add_custom_command(OUTPUT ${header_file}
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${protocol_path}" ${header_file}
            DEPENDS "${protocol_path}"
            VERBATIM)

        add_custom_command(OUTPUT ${code_file}
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${protocol_path}" ${code_file}
            DEPENDS "${protocol_path}"
            VERBATIM)

        target_sources(${PROJECT_NAME} PRIVATE ${header_file} ${code_file})
    endmacro()

    generate_wayland_protocol("wayland.xml")
    generate_wayland_protocol("viewporter.xml")
    generate_wayland_protocol("xdg-shell.xml")
    generate_wayland_protocol("fractional-scale-v1.xml")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_WAYLAND PLATFORM_X11)
endif()

if(MSVC)
    # /WX
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan cglm)
else()
    # -Wpedantic
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -pedantic -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)
    target_link_libraries( ${PROJECT_NAME} dl Vulkan::Vulkan cglm m)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    # Set source and destination directories
    set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")

    # Get all files in the assets directory
    file(GLOB_RECURSE ASSET_FILES "${ASSETS_DIR}/*.*")

    # Create custom command to copy each asset file
    foreach(ASSET_FILE ${ASSET_FILES})
        # Get relative path to preserve folder structure
        file(RELATIVE_PATH REL_PATH "${ASSETS_DIR}" "${ASSET_FILE}")
        set(DEST_FILE "${CMAKE_BINARY_DIR}/bin/${CONFIG}/assets/${REL_PATH}")

        get_filename_component(FILE_EXTENSION "${ASSET_FILE}" EXT)
        if (${FILE_EXTENSION} STREQUAL ".glsl") 
            get_filename_component(DEST_DIR ${DEST_FILE} DIRECTORY)
            get_filename_component(DEST_NAME ${DEST_FILE} NAME_WE)
            set(SPV_FILE ${DEST_DIR}/${DEST_NAME}.spv)
            add_custom_command(
                OUTPUT "${SPV_FILE}"
                COMMAND glslc "${ASSET_FILE}" -o "${SPV_FILE}"
                DEPENDS "${ASSET_FILE}"
                COMMENT "Compiling GLSL shader: ${REL_PATH} -> ${REL_PATH}.spv"
                VERBATIM
            )

            list(APPEND ASSET_OUTPUTS "${SPV_FILE}")
        else()
        add_custom_command(
            OUTPUT "${DEST_FILE}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASSET_FILE}" "${DEST_FILE}"
            DEPENDS "${ASSET_FILE}"
            COMMENT "Copying asset: ${REL_PATH}"
            VERBATIM
        )

        list(APPEND ASSET_OUTPUTS "${DEST_FILE}")
        endif()
    endforeach()

    # Create a custom target that depends on all copied assets
    add_custom_target(copy_assets ALL DEPENDS ${ASSET_OUTPUTS})
endif()
