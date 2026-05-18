cmake_minimum_required(VERSION 3.13)
project(openvcl VERSION 0.3.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default install prefix to $PS2DEV when available (mirrors old Makefile behaviour).
if(DEFINED ENV{PS2DEV} AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{PS2DEV}" CACHE PATH
        "Installation prefix (defaults to \$PS2DEV)" FORCE)
endif()

# All translation units that form the openvcl binary.
file(GLOB OPENVCL_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(openvcl ${OPENVCL_SOURCES})

# Place the binary at the repository root, mirroring the old Makefile layout.
# The integration tests and the dual-install command both reference openvcl/openvcl.
set_target_properties(openvcl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

target_compile_options(openvcl PRIVATE
    -Wall
    -Werror
    -Wno-unused-but-set-variable
    -Wno-deprecated-declarations
    -pedantic
)

include(GNUInstallDirs)
install(TARGETS openvcl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Unit and integration tests (cmake -B build; ctest --test-dir build).
# Disable by passing -DBUILD_TESTING=OFF.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
