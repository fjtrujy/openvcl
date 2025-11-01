set(LIBIBERTY_SOURCES
  _doprnt.c
  alloca.c
  argv.c
  asprintf.c
  atexit.c
  basename.c
  bcmp.c
  bcopy.c
  bsearch.c
  bzero.c
  calloc.c
  choose-temp.c
  clock.c
  concat.c
  copysign.c
  cp-demangle.c
  cplus-dem.c
  dyn-string.c
  fdmatch.c
  ffs.c
  fibheap.c
  floatformat.c
  fnmatch.c
  getopt.c
  getopt1.c
  getpagesize.c
  getpwd.c
  getruntime.c
  hashtab.c
  hex.c
  index.c
  insque.c
  lbasename.c
  make-temp-file.c
  md5.c
  memchr.c
  memcmp.c
  memset.c
  mkstemps.c
  objalloc.c
  obstack.c
  partition.c
  pexecute.c
  putenv.c
  random.c
  regex.c
  rindex.c
  safe-ctype.c
  setenv.c
  sigsetmask.c
  sort.c
  spaces.c
  splay-tree.c
  strcasecmp.c
  strchr.c
  strdup.c
  strerror.c
  strncasecmp.c
  strncmp.c
  strrchr.c
  strsignal.c
  strstr.c
  strtod.c
  strtol.c
  strtoul.c
  ternary.c
  tmpnam.c
  vasprintf.c
  vfork.c
  vfprintf.c
  vprintf.c
  vsprintf.c
  waitpid.c
  xatexit.c
  xexit.c
  xmalloc.c
  xmemdup.c
  xstrdup.c
  xstrerror.c
)

include(CheckFunctionExists)
include(CheckTypeSize)
check_function_exists(strerror HAVE_STRERROR)
check_function_exists(strsignal HAVE_STRSIGNAL)
check_type_size("uintptr_t" SIZEOF_UINTPTR_T)

# Exclude POSIX-only sources when building on Windows toolchains
if (WIN32 OR MINGW)
  list(REMOVE_ITEM LIBIBERTY_SOURCES
    pexecute.c
    vfork.c
    waitpid.c
    vsprintf.c
    vfprintf.c
    vprintf.c
  )
endif()

if(HAVE_STRERROR)
  list(REMOVE_ITEM LIBIBERTY_SOURCES strerror.c)
endif()
if(HAVE_STRSIGNAL)
  list(REMOVE_ITEM LIBIBERTY_SOURCES strsignal.c)
endif()

add_library(libiberty STATIC ${LIBIBERTY_SOURCES})

# Suppress warnings for mktemp in choose-temp.c (used only for legacy API)
  set_target_properties(libiberty PROPERTIES COMPILE_FLAGS 
  "-Wno-implicit-function-declaration -Wno-builtin-declaration-mismatch"
  )

target_include_directories(libiberty
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(libiberty PRIVATE _GNU_SOURCE HAVE_CONFIG_H)
if (SIZEOF_UINTPTR_T)
  target_compile_definitions(libiberty PRIVATE HAVE_UINTPTR_T)
endif()

# Minimal config.h for libiberty when building under CMake
configure_file(${CMAKE_SOURCE_DIR}/cmake/libiberty_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)


