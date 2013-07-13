
add_definitions(-DQT_PLUGIN)

add_library(karchive_tar tar.cpp)
target_link_libraries(karchive_tar KArchive)
