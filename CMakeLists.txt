cmake_minimum_required(VERSION 3.11)
project(program)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
set(CMAKE_LINKER_FLAGS_DEBUG
    "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

if(MSVC)
  set(CONFIG $<CONFIG>)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
  set(CONFIG ${CMAKE_BUILD_TYPE})
  # set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
  # set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game")
endif()

# add_subdirectory(tools)
add_subdirectory(game)
add_subdirectory(engine)

set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/game/assets")
if(EXISTS ${ASSETS_DIR})
    file(GLOB_RECURSE SHADERS "${ASSETS_DIR}/*.vertex" "${ASSETS_DIR}/*.fragment")
    foreach(SHADER ${SHADERS})
        file(RELATIVE_PATH REL_PATH "${ASSETS_DIR}" "${SHADER}")
        get_filename_component(ASSET_DIR "${SHADER}" DIRECTORY)
        get_filename_component(DEST_NAME "${SHADER}" NAME)
        set(SPV_FILE "${ASSET_DIR}/bin/${DEST_NAME}.spv")

        get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY) 

        add_custom_command(
            OUTPUT "${SPV_FILE}"
            COMMAND glslc "${SHADER}" -o "${SPV_FILE}"
            DEPENDS "${SHADER}"
            COMMENT "Compiling GLSL shader: ${REL_PATH} -> ${REL_DIR}/bin/${DEST_NAME}.spv"
            VERBATIM)

        list(APPEND SHADER_OUTPUTS "${SPV_FILE}")
    endforeach()

  add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})
endif()
