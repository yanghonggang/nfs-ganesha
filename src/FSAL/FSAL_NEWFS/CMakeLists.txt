add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsalnewfs_LIB_SRCS
   main.c
   internal.h
)

#message("RGW_INCLUDE_DIR ${RGW_INCLUDE_DIR}")
#include_directories(${CEPHFS_INCLUDE_DIR})

add_library(fsalnewfs MODULE ${fsalnewfs_LIB_SRCS})
add_sanitizers(fsalnewfs)

target_link_libraries(fsalnewfs ${SYSTEM_LIBRARIES})

set_target_properties(fsalnewfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalnewfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
