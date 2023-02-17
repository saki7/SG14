
find_package(benchmark REQUIRED)

set(BENCH_SOURCE_FILES
    dummy_bench.cpp
)

set(BENCH_NAME ubench)
add_executable(${BENCH_NAME} ${BENCH_SOURCE_FILES})
include_directories(${BENCHMARK_INCLUDE_DIRS} ${SG14_INCLUDE_DIRECTORY})
target_link_libraries(${BENCH_NAME} ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(${BENCH_NAME} benchmark::benchmark_main)

# Compile options
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	target_compile_options(${BENCH_NAME} PRIVATE -Wall -Wextra -Werror)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(${BENCH_NAME} PRIVATE -Wall -Wextra -Werror)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${BENCH_NAME})
	target_compile_options(${BENCH_NAME} PRIVATE /Zc:__cplusplus /permissive- /W4 /WX)
	add_definitions(-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS)
endif()
