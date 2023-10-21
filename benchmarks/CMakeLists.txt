
find_package(benchmark REQUIRED)

add_executable(ubench
  unstable_remove_bench.cpp
)
target_include_directories(ubench PRIVATE ${SG14_INCLUDE_DIRECTORY})
target_link_libraries(ubench PRIVATE benchmark::benchmark_main)

## Ad-hoc compiler options ##

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set_property(TARGET ubench APPEND PROPERTY COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_property(TARGET ubench APPEND PROPERTY COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set_property(TARGET ubench APPEND PROPERTY COMPILE_OPTIONS "-W4;-WX;-DNOMINMAX;-D_SCL_SECURE_NO_WARNINGS")
endif()
