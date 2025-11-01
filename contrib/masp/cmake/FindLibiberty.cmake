# Try to find an external libiberty
#
#  Provides imported target: Libiberty::Libiberty
#
#  Variables (advanced):
#   LIBIBERTY_LIBRARY     - library path
#   LIBIBERTY_INCLUDE_DIR - headers path (optional; we often use local headers)

include(FindPackageHandleStandardArgs)

set(_HINT_DIRS
  # pkg managers
  /usr/lib /usr/local/lib /opt/local/lib
  /usr/lib64 /usr/local/lib64
  # Debian/Ubuntu binutils-dev
  /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
  # Homebrew binutils
  /opt/homebrew/opt/binutils/lib
  /usr/local/opt/binutils/lib
)

find_library(LIBIBERTY_LIBRARY NAMES iberty libiberty PATHS ${_HINT_DIRS})

set(_INCLUDE_HINTS
  /usr/include /usr/local/include /opt/local/include
  /opt/homebrew/opt/binutils/include
  /usr/local/opt/binutils/include
)
find_path(LIBIBERTY_INCLUDE_DIR NAMES libiberty.h ansidecl.h PATHS ${_INCLUDE_HINTS})

find_package_handle_standard_args(Libiberty
  REQUIRED_VARS LIBIBERTY_LIBRARY
)

if(Libiberty_FOUND AND NOT TARGET Libiberty::Libiberty)
  add_library(Libiberty::Libiberty UNKNOWN IMPORTED)
  set_target_properties(Libiberty::Libiberty PROPERTIES
    IMPORTED_LOCATION "${LIBIBERTY_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBIBERTY_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(LIBIBERTY_LIBRARY LIBIBERTY_INCLUDE_DIR)


