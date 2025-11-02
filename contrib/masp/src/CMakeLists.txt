# Sources for MASP
set(MASP_SOURCES
  compat.c
  masp.c
  macro.c
  sb.c
  hash.c
)

# Generate a minimal config.h for CMake builds
configure_file(${CMAKE_SOURCE_DIR}/cmake/app_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

add_executable(masp ${MASP_SOURCES})

target_include_directories(masp
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(masp PRIVATE PACKAGE_VERSION="0.1.16")

# Make warnings conditional on compiler support to accommodate older toolchains
include(CheckCCompilerFlag)

set(MASP_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Wno-unused-parameter
  -Wno-unused-but-set-variable
  -Wno-format-overflow
  -Wno-cast-function-type-mismatch
  -Wno-cast-function
  -Wno-deprecated-non-prototype
)

set(_masp_flag_index 0)
foreach(_w IN LISTS MASP_WARNINGS)
  math(EXPR _masp_flag_index "${_masp_flag_index} + 1")
  set(_flag_var "HAS_FLAG_${_masp_flag_index}")
  check_c_compiler_flag("${_w}" ${_flag_var})
  if(${_flag_var})
    target_compile_options(masp PRIVATE "${_w}")
  endif()
endforeach()

option(MASP_WERROR "Treat warnings as errors" OFF)
if(MASP_WERROR)
  target_compile_options(masp PRIVATE -Werror)
endif()

# Standard install target
include(GNUInstallDirs)
install(TARGETS masp
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Link against gnurx on MinGW where POSIX regex functions require an extra lib
if(MINGW)
  target_link_libraries(masp PRIVATE gnurx)
endif()

