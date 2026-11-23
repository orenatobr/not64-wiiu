# Minimal wrapper that builds only decaf-emu's latte-assembler (build-time
# tool; nothing from it ends up in the emulator). Mirrors the relevant
# parts of decaf-emu's top-level and libraries/ CMakeLists.
cmake_minimum_required(VERSION 3.16)
project(latte-assembler-wrapper C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DECAF_SRC "/decaf" CACHE PATH "decaf-emu checkout")
add_definitions(-DDECAF_USE_STDLAYOUT_BITFIELD)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof -w")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
find_package(Threads REQUIRED)
link_libraries(stdc++fs)

macro(GroupSources)
endmacro()
macro(AutoGroupPCHFiles)
endmacro()
set(DECAF_PCH OFF)
set(DECAF_VULKAN OFF)
set(DECAF_QT OFF)
set(DECAF_BUILD_TOOLS ON)
set(DECAF_INSTALL_BINDIR bin)

set(LIB ${DECAF_SRC}/libraries)
add_library(cnl INTERFACE IMPORTED GLOBAL)
set_target_properties(cnl PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIB}/cnl/include")
add_library(gsl INTERFACE IMPORTED GLOBAL)
set_target_properties(gsl PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "GSL_THROWS_FOR_TESTING"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB}/gsl-lite/include")
add_library(excmd INTERFACE IMPORTED GLOBAL)
set_target_properties(excmd PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIB}/excmd/src")
add_library(peglib INTERFACE IMPORTED GLOBAL)
set_target_properties(peglib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIB}/cpp-peglib")
add_subdirectory(${LIB}/fmt fmt)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(${LIB}/spdlog spdlog)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(${LIB}/glslang glslang)
get_target_property(_f SPIRV COMPILE_OPTIONS)
if(_f)
  list(REMOVE_ITEM _f -fno-rtti)
  set_target_properties(SPIRV PROPERTIES COMPILE_OPTIONS "${_f}")
endif()

include_directories(${DECAF_SRC}/src)
# decaf's common/ contains x86-only exception handling, so build only the
# parts the assembler links against.
add_library(common STATIC
    ${DECAF_SRC}/src/common/src/assert.cpp
    ${DECAF_SRC}/src/common/src/log.cpp
    ${DECAF_SRC}/src/common/src/murmur3.cpp
    ${DECAF_SRC}/src/common/src/platform_posix_debug.cpp
    ${DECAF_SRC}/src/common/src/platform_posix_time.cpp
    ${DECAF_SRC}/src/common/src/platform_posix_stacktrace.cpp)
target_include_directories(common PUBLIC ${DECAF_SRC}/src/common)
target_link_libraries(common cnl fmt::fmt gsl spdlog::spdlog)
add_subdirectory(${DECAF_SRC}/src/libgfd libgfd)
add_subdirectory(${DECAF_SRC}/tools/latte-assembler latte-assembler)
