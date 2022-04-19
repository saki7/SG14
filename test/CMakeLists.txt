
find_package(GTest REQUIRED)

set(TEST_SOURCE_FILES
    flat_map_test.cpp
    flat_set_test.cpp
    inplace_function_test.cpp
    plf_colony_test.cpp
    ring_test.cpp
    slot_map_test.cpp
    uninitialized_test.cpp
    unstable_remove_test.cpp
)

set(TEST_NAME utest)
add_executable(${TEST_NAME} ${TEST_SOURCE_FILES})
include_directories(${GTEST_INCLUDE_DIRS} ${SG14_INCLUDE_DIRECTORY})
target_link_libraries(${TEST_NAME} ${CMAKE_THREAD_LIBS_INIT} ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})

# Compile options
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)
	set_source_files_properties(${SG14_TEST_SOURCE_DIRECTORY}/plf_colony_test.cpp PROPERTIES
		COMPILE_FLAGS "-Wno-unused-parameter"
	)
	if (CMAKE_CXX_COMPILER_VERSION MATCHES "^7.*")
		set_source_files_properties(${SG14_TEST_SOURCE_DIRECTORY}/slot_map_test.cpp PROPERTIES
			COMPILE_FLAGS "-Wno-error=unused-variable -Wno-error=unused-but-set-variable") # Fix gcc7 issues with structured bindings
		message("Disabled -Wunused-variable and -Wunused-but-set-variable for gcc ${CMAKE_CXX_COMPILER_VERSION}.")
	endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TEST_NAME})
	target_compile_options(${TEST_NAME} PRIVATE /Zc:__cplusplus /permissive- /W4 /WX)
	add_definitions(-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS)
	set_source_files_properties(${SG14_TEST_SOURCE_DIRECTORY}/plf_colony_test.cpp PROPERTIES
		COMPILE_FLAGS "/wd4127") # Disable conditional expression is constant, use if constexpr
endif()
