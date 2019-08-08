# - Find NewFS
#
# This module accepts the following optional variables:
#    NEWFS_PREFIX = A hint on NewFS install path.
#
# This module defines the following variables:
#    NEWFS_FOUND	= Was NewFS found or not?
#    NEWFS_LIBRARIES	= The list of libraies to link to when using NewFS
#    NEWFS_INCLUDE_DIR	= The path to NewFS include directory
#
# On can set NEWFS_PREFIX before using find_package(NewFS) and the
# module with use the PATH as a hint to find NewFS.
#
# The hint can be given on the command line too:
#   cmake -DNEWFS_PREFIX=/DIR/TO/NEWFS/ /path/to/nfs/source

if(NEWFS_PREFIX)
  message(STATUS "FindNewFS: using PATH HINT: ${NEWFS_PREFIX}")

  # Try to make the prefix override the normal paths
  find_path(NEWFS_INCLUDE_DIR
    NAMES newfs/newfs_c.h
    PATHS ${NEWFS_PREFIX}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
    DOC "The NewFS include headers")

  find_path(NEWFS_LIBRARY_DIR
    NAMES libnewfs.so
    PATHS ${NEWFS_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    NO_DEFAULT_PATH
    DOC "The NewFS libraries")
endif(NEWFS_PREFIX)

if (NOT NEWFS_INCLUDE_DIR)
  find_path(NEWFS_INCLUDE_DIR
    NAMES newfs/newfs_c.h
    PATHS ${NEWFS_PREFIX}
    PATH_SUFFIXES include
    DOC "The NewFS include headers")
endif(NOT NEWFS_INCLUDE_DIR)

if (NOT NEWFS_LIBRARY_DIR)
  find_path(NEWFS_LIBRARY_DIR
    NAMES libnewfs.so
    PATHS ${NEWFS_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    DOC "The NewFS libraries")
endif(NOT NEWFS_LIBRARY_DIR)

find_library(NEWFS_LIBRARY newfs PATH ${NEWFS_LIBRARY_DIR} NO_DEFAULT_PATH)
set(NEWFS_LIBRARY ${NEWFS_LIBRARY})
message(STATUS "Found newfs libraries: ${NEWFS_LIBRARIES}")

# handle the QUIELY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NEWFS
  REQUIRED_VARS NEWFS_INCLUDE_DIR NEWFS_LIBRARY_DIR
)
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                     VERSION_VAR)

mark_as_advanced(NEWFS_INCLUDE_DIR)
mark_as_advanced(NEWFS_LIBRARY_DIR)
