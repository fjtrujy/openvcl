cmake_minimum_required(VERSION 3.15)

project(masp C)

option(BUILD_INTERNAL_LIBIBERTY "Build bundled libiberty from source" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(BUILD_INTERNAL_LIBIBERTY)
  add_subdirectory(libiberty)
  set(LIBIBERTY_TARGET libiberty)
  set(LIBIBERTY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
else()
  find_package(Libiberty)
  if(TARGET Libiberty::Libiberty)
    set(LIBIBERTY_TARGET Libiberty::Libiberty)
  else()
    message(STATUS "External libiberty not found; falling back to bundled copy")
    add_subdirectory(libiberty)
    set(LIBIBERTY_TARGET libiberty)
  endif()
endif()

add_subdirectory(src)


