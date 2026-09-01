cmake_minimum_required(VERSION 3.21)

project(fuck_helium VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "fuck-helium only supports Windows.")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "fuck-helium requires MSVC or clang-cl.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
  message(FATAL_ERROR "The current runtime patch supports x64 Helium only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(fuck_helium SHARED
  src/menu_patch.cc
  src/proxy.cc
  src/version.dll.rc
  src/version.def
)

target_compile_definitions(fuck_helium PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_compile_options(fuck_helium PRIVATE /W4 /permissive- /EHsc)
target_link_options(fuck_helium PRIVATE /DYNAMICBASE /NXCOMPAT)
set_target_properties(fuck_helium PROPERTIES
  OUTPUT_NAME version
  PREFIX ""
)

add_executable(fuck_helium_keeper WIN32
  src/keeper.cc
  src/keeper.rc
)
target_compile_definitions(fuck_helium_keeper PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_compile_options(fuck_helium_keeper PRIVATE /W4 /permissive- /EHsc)
target_link_options(fuck_helium_keeper PRIVATE /DYNAMICBASE /NXCOMPAT)
target_link_libraries(fuck_helium_keeper PRIVATE shell32)
set_target_properties(fuck_helium_keeper PROPERTIES
  OUTPUT_NAME "fuck-helium-keeper"
)
