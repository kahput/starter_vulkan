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
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
endif()

add_subdirectory(game)
add_subdirectory(engine)

set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
if(EXISTS ${ASSETS_DIR})
  file(GLOB_RECURSE ASSET_FILES "${ASSETS_DIR}/*.*")

  foreach(ASSET_FILE ${ASSET_FILES})
    file(RELATIVE_PATH REL_PATH "${ASSETS_DIR}" "${ASSET_FILE}")
    set(DEST_FILE "${CMAKE_BINARY_DIR}/bin/${CONFIG}/assets/${REL_PATH}")

    get_filename_component(FILE_EXTENSION "${ASSET_FILE}" EXT)
    if(${FILE_EXTENSION} STREQUAL ".glsl")
      get_filename_component(DEST_DIR ${DEST_FILE} DIRECTORY)
      get_filename_component(DEST_NAME ${DEST_FILE} NAME_WE)

      set(VERT_SPV_FILE ${DEST_DIR}/${DEST_NAME}.vert.spv)
      set(FRAG_SPV_FILE ${DEST_DIR}/${DEST_NAME}.frag.spv)

      add_custom_command(
        OUTPUT "${VERT_SPV_FILE}"
        COMMAND glslc -DSHADER_STAGE_VERTEX "${ASSET_FILE}" -o
                "${VERT_SPV_FILE}"
        DEPENDS "${ASSET_FILE}"
        COMMENT "Compiling GLSL shader: ${REL_PATH} -> ${REL_PATH}.vert.spv"
        VERBATIM)

      add_custom_command(
        OUTPUT "${FRAG_SPV_FILE}"
        COMMAND glslc -DSHADER_STAGE_FRAGMENT "${ASSET_FILE}" -o
                "${FRAG_SPV_FILE}"
        DEPENDS "${ASSET_FILE}"
        COMMENT "Compiling GLSL shader: ${REL_PATH} -> ${REL_PATH}.frag.spv"
        VERBATIM)

      list(APPEND ASSET_OUTPUTS "${VERT_SPV_FILE}" "${FRAG_SPV_FILE}")
    else()
      add_custom_command(
        OUTPUT "${DEST_FILE}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASSET_FILE}"
                "${DEST_FILE}"
        DEPENDS "${ASSET_FILE}"
        COMMENT "Copying asset: ${REL_PATH}"
        VERBATIM)

      list(APPEND ASSET_OUTPUTS "${DEST_FILE}")
    endif()
  endforeach()

  # Create a custom target that depends on all copied assets
  add_custom_target(copy_assets ALL DEPENDS ${ASSET_OUTPUTS})
endif()
