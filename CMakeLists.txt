cmake_minimum_required(VERSION 3.11)
project(program)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_definitions(DEV_BUILD)

# ── Shaders ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SHADERS
    "${CMAKE_SOURCE_DIR}/assets/*.vertex"
    "${CMAKE_SOURCE_DIR}/assets/*.fragment"
    "${CMAKE_SOURCE_DIR}/assets/*.compute"
)
foreach(SHADER ${SHADERS})
    get_filename_component(DIR  ${SHADER} DIRECTORY)
    get_filename_component(NAME ${SHADER} NAME)

    set(SPV "${DIR}/bin/${NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND glslc -MD -I "${CMAKE_SOURCE_DIR}/assets/shaders" ${SHADER} -o ${SPV}
        DEPFILE "${SPV}.d"
        DEPENDS ${SHADER} VERBATIM
    )

    list(APPEND SHADER_OUTPUTS ${SPV})
endforeach()
if(SHADER_OUTPUTS)
    add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})
endif()

# Scratches
set(WEB_SCRATCHES web_scratch)
set(NATIVE_SCRATCHES xcb_scratch skinning material_scratch spriv)

# Web build
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    file(GLOB_RECURSE 
        PROJECT_SOURCES 
        "${CMAKE_SOURCE_DIR}/engine/src/core/*.c"
        "${CMAKE_SOURCE_DIR}/engine/src/os/web/*.c" 
        "${CMAKE_SOURCE_DIR}/engine/src/*.h"
        "${CMAKE_SOURCE_DIR}/engine/src/core/*.c"
        "${CMAKE_SOURCE_DIR}/engine/src/input.c"
    )

    foreach(NAME ${WEB_SCRATCHES})
        set(SRC "${CMAKE_SOURCE_DIR}/engine/scratch/${NAME}.c")
        add_executable(
            ${NAME} 
            ${SRC}
            ${PROJECT_SOURCES}
        )
        target_include_directories(${NAME} PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")
        target_compile_definitions(${NAME} PRIVATE WEB_BUILD)
        target_link_options(${NAME} PRIVATE
            "-sALLOW_MEMORY_GROWTH"
            "-sASYNCIFY"
            "-sTOTAL_STACK=64mb"
            "-sMIN_WEBGL_VERSION=2"
            # "-sASSERTIONS=2" 
            #     "-sSAFE_HEAP=1"
            #     "-sSTACK_OVERFLOW_CHECK=2"
            #     "-g" # Generate source maps to see C code in browser console
        )
        set_target_properties(${NAME} PROPERTIES SUFFIX ".html")

        target_link_options(${NAME} PRIVATE 
            "--preload-file" "${CMAKE_SOURCE_DIR}/game/src@/src"
        )
    endforeach()
else()
    file(GLOB_RECURSE 
        PROJECT_SOURCES 
        "${CMAKE_SOURCE_DIR}/engine/src/core/*.c"
        "${CMAKE_SOURCE_DIR}/engine/src/os/linux/*.c" 
        "${CMAKE_SOURCE_DIR}/engine/src/gfx/vulkan/tables.c"
        "${CMAKE_SOURCE_DIR}/engine/src/*.h"
    )

    if(MSVC)
        set(CONFIG $<CONFIG>)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    else()
        set(CONFIG ${CMAKE_BUILD_TYPE})
        # set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
        # set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()

    file(GLOB_RECURSE VENDOR_SOURCES  "engine/vendor/*/*.c" "engine/vendor/*/*.h")
    add_executable(${PROJECT_NAME} "engine/src/main.c" ${PROJECT_SOURCES} ${VENDOR_SOURCES})

    find_package(Vulkan REQUIRED)

    target_include_directories(${PROJECT_NAME}
        PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")
    target_include_directories(
        ${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS} "${CMAKE_BINARY_DIR}/engine"
        "${CMAKE_SOURCE_DIR}/engine/vendor")

    # Platform-specific configurations
    if(WIN32)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WINDOWS_BUILD VK_USE_PLATFORM_WIN32_KHR)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4)
        target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan)
    elseif(UNIX)
        target_compile_options(
            ${PROJECT_NAME}
            PRIVATE -fvisibility=hidden
            -Wall
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-pedantic>
            $<$<CONFIG:Debug>:-Werror>
            -Wextra
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-override-init)

        target_link_options(${PROJECT_NAME} PRIVATE -Wl,--export-dynamic)
        target_link_libraries(${PROJECT_NAME} dl xcb xcb-xkb xcb-icccm Vulkan::Vulkan m)
        target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_BUILD)
    else()
        message(FATAL_ERROR "Windows and Linux only supported platforms")
    endif()

    foreach(NAME ${NATIVE_SCRATCHES})
        set(SRC 
            "${CMAKE_SOURCE_DIR}/engine/scratch/${NAME}.c"
            "${CMAKE_SOURCE_DIR}/engine/src/anim.c"
            "${CMAKE_SOURCE_DIR}/engine/src/scene.c"
            "${CMAKE_SOURCE_DIR}/engine/src/input.c"
            "${CMAKE_SOURCE_DIR}/engine/src/gfx/vulkan/tables.c"
        )
        add_executable(${NAME} ${SRC} ${PROJECT_SOURCES} ${VENDOR_SOURCES})
        target_include_directories(
            ${NAME}
            PUBLIC 
            "${CMAKE_SOURCE_DIR}/engine/src"
            "${CMAKE_SOURCE_DIR}/engine/vendor"
        )
        target_link_libraries(${NAME} dl xcb xcb-xkb xcb-icccm Vulkan::Vulkan m)
        target_compile_definitions(${NAME} PRIVATE LINUX_BUILD VK_USE_PLATFORM_XCB_KHR)
        set_target_properties(${NAME} PROPERTIES SUFFIX ".exe")
        target_compile_options(
            ${NAME}
            PRIVATE -fvisibility=hidden
            -Wall
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-pedantic>
            $<$<CONFIG:Debug>:-Werror>
            -Wextra
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-override-init)
    endforeach()
endif()
