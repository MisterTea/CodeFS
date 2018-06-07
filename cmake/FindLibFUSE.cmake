# Try pkg-config first
find_package(PkgConfig)
pkg_check_modules(PKGC_LIBFUSE QUIET fuse)

if(PKGC_LIBFUSE_FOUND)
  # Found lib using pkg-config.
  if(CMAKE_DEBUG)
    message(STATUS "\${PKGC_LIBFUSE_LIBRARIES} = ${PKGC_LIBFUSE_LIBRARIES}")
    message(STATUS "\${PKGC_LIBFUSE_LIBRARY_DIRS} = ${PKGC_LIBFUSE_LIBRARY_DIRS}")
    message(STATUS "\${PKGC_LIBFUSE_LDFLAGS} = ${PKGC_LIBFUSE_LDFLAGS}")
    message(STATUS "\${PKGC_LIBFUSE_LDFLAGS_OTHER} = ${PKGC_LIBFUSE_LDFLAGS_OTHER}")
    message(STATUS "\${PKGC_LIBFUSE_INCLUDE_DIRS} = ${PKGC_LIBFUSE_INCLUDE_DIRS}")
    message(STATUS "\${PKGC_LIBFUSE_CFLAGS} = ${PKGC_LIBFUSE_CFLAGS}")
    message(STATUS "\${PKGC_LIBFUSE_CFLAGS_OTHER} = ${PKGC_LIBFUSE_CFLAGS_OTHER}")
  endif(CMAKE_DEBUG)

  set(LIBFUSE_LIBRARIES ${PKGC_LIBFUSE_LIBRARIES})
  set(LIBFUSE_INCLUDE_DIRS ${PKGC_LIBFUSE_INCLUDE_DIRS})
  #set(LIBFUSE_DEFINITIONS ${PKGC_LIBFUSE_CFLAGS_OTHER})
else(PKGC_LIBFUSE_FOUND)
  # Didn't find lib using pkg-config. Try to find it manually
  message(WARNING "Unable to find LibFUSE using pkg-config! If compilation fails, make sure pkg-config is installed and PKG_CONFIG_PATH is set correctly")

  find_path(LIBFUSE_INCLUDE_DIR fuse.h
            PATH_SUFFIXES fuse)
  find_library(LIBFUSE_LIBRARY NAMES fuse libfuse)

  if(CMAKE_DEBUG)
    message(STATUS "\${LIBFUSE_LIBRARY} = ${LIBFUSE_LIBRARY}")
    message(STATUS "\${LIBFUSE_INCLUDE_DIR} = ${LIBFUSE_INCLUDE_DIR}")
  endif(CMAKE_DEBUG)

  set(LIBFUSE_LIBRARIES ${LIBFUSE_LIBRARY})
  set(LIBFUSE_INCLUDE_DIRS ${LIBFUSE_INCLUDE_DIR})
endif(PKGC_LIBFUSE_FOUND)

include(FindPackageHandleStandardArgs)
# Handle the QUIETLY and REQUIRED arguments and set <PREFIX>_FOUND to TRUE if
# all listed variables are TRUE
find_package_handle_standard_args(LibFUSE DEFAULT_MSG LIBFUSE_LIBRARIES)

