# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/ld2451_viewer_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/ld2451_viewer_autogen.dir/ParseCache.txt"
  "ld2451_viewer_autogen"
  )
endif()
