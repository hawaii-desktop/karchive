remove_definitions(-DQT_NO_CAST_FROM_ASCII)

include(ECMMarkAsTest)

macro(KARCHIVE_EXECUTABLE_TESTS)
   foreach(_testname ${ARGN})
      add_executable(${_testname} ${_testname}.cpp) # TODO NOGUI
      target_link_libraries(${_testname} KArchive)
      ecm_mark_as_test(${_testname})
   endforeach(_testname)
endmacro(KARCHIVE_EXECUTABLE_TESTS)

karchive_executable_tests(
  ktartest
  kziptest
)

if(liblzma_found)
   karchive_executable_tests(
      k7ziptest
   )
endif()
