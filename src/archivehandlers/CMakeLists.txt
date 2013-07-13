
add_definitions(-DQT_PLUGIN)

add_library(karchive_tar tar.cpp)
target_link_libraries(karchive_tar KArchive)

add_library(karchive_ar ar.cpp)
target_link_libraries(karchive_ar KArchive)

add_library(karchive_zip zip.cpp)
target_link_libraries(karchive_zip KArchive)
