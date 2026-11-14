cmake_minimum_required(VERSION 3.22)
project(model1_android LANGUAGES C CXX ASM)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SDL_SOURCE "${REPO}/build/deps/SDL3-3.4.18")
set(FT_SOURCE "${REPO}/build/deps/freetype-2.14.3")
if(NOT EXISTS "${SDL_SOURCE}/CMakeLists.txt" OR NOT EXISTS "${FT_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "SDL and FreeType sources are missing. Run tools/android/build.sh.")
endif()

find_program(HOST_MAKE make REQUIRED)
find_program(HOST_CC NAMES cc clang gcc REQUIRED NO_CMAKE_FIND_ROOT_PATH)
execute_process(COMMAND "${HOST_MAKE}" -s "HOSTCC=${HOST_CC}" build/musashi/m68kops.c
    WORKING_DIRECTORY "${REPO}" RESULT_VARIABLE generator_result)
if(NOT generator_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the Musashi opcode tables on the host")
endif()
execute_process(COMMAND "${HOST_MAKE}" -s android-sources
    WORKING_DIRECTORY "${REPO}" RESULT_VARIABLE source_result OUTPUT_VARIABLE source_output
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT source_result EQUAL 0)
    message(FATAL_ERROR "Could not obtain the VR native source list")
endif()
string(REPLACE "\n" ";" source_list "${source_output}")
list(TRANSFORM source_list PREPEND "${REPO}/")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO}/Makefile")

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/sdl")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory("${FT_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/freetype")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    "#define M1_VERSION \"android-dev\"\n#define M1_DATE \"\"\n#define M1_COMMIT \"\"\n")
add_library(main SHARED ${source_list})
target_compile_features(main PRIVATE cxx_std_20)
target_compile_options(main PRIVATE -O2)
target_compile_definitions(main PRIVATE M1_NATIVE=1 M1_GPU3D_SDL=1 SLJIT_CONFIG_AUTO=1
    RMLUI_STATIC_LIB RMLUI_FONT_ENGINE_FREETYPE RMLUI_SDL_VERSION_MAJOR=3)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${REPO}/third_party/glm" "${REPO}/src/core/shim_include"
    "${REPO}/third_party/musashi" "${REPO}/build/musashi"
    "${REPO}/third_party/rmlui/Include" "${REPO}/third_party/rmlui/Backends"
    "${REPO}/third_party/arcade-lan/include")
target_link_libraries(main PRIVATE SDL3::SDL3 freetype z)
if(ANDROID)
    target_compile_definitions(main PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
    target_link_options(main PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384")
    target_link_libraries(main PRIVATE log android)
endif()
