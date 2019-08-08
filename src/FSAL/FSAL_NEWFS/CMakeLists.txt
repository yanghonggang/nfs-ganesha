add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsalnewfs_LIB_SRCS
   main.c
   export.c
   handle.c
   internal.c
   internal.h
)

message("NEWFS_INCLUDE_DIR ${NEWFS_INCLUDE_DIR}")
message("NEWFS_LIBRARIES ${NEWFS_LIBRARY}")
include_directories(${NEWFSFS_INCLUDE_DIR})

add_library(fsalnewfs MODULE ${fsalnewfs_LIB_SRCS})
add_sanitizers(fsalnewfs)

target_link_libraries(fsalnewfs ${NEWFS_LIBRARY} ${SYSTEM_LIBRARIES})

set_target_properties(fsalnewfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalnewfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
