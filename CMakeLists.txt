cmake_minimum_required(VERSION 3.10)
project(sg14 CXX)

include(CheckCXXCompilerFlag)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

# C++17 by default, but allow the user to override it with -DCMAKE_CXX_STANDARD.
set(CMAKE_CXX_EXTENSIONS OFF)
if ("${CMAKE_CXX_STANDARD}" EQUAL 14)
  check_cxx_compiler_flag("-std=c++14" HAVE_REALFLAG)
  check_cxx_compiler_flag("-std=c++1y" HAVE_FAKEFLAG)
  check_cxx_compiler_flag("-std:c++14" HAVE_MSVCFLAG)
  if (HAVE_REALFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
  elseif (HAVE_FAKEFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y")
  elseif (HAVE_MSVCFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++14 -permissive- -Zc:__cplusplus")
  else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++14 support. Please use a different C++ compiler.")
  endif()
elseif ("${CMAKE_CXX_STANDARD}" EQUAL 17)
  check_cxx_compiler_flag("-std=c++17" HAVE_REALFLAG)
  check_cxx_compiler_flag("-std=c++1z" HAVE_FAKEFLAG)
  check_cxx_compiler_flag("-std:c++17" HAVE_MSVCFLAG)
  if (HAVE_REALFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  elseif (HAVE_FAKEFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")
  elseif (HAVE_MSVCFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++17 -permissive- -Zc:__cplusplus")
  else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler.")
  endif()
elseif ("${CMAKE_CXX_STANDARD}" EQUAL 20)
  check_cxx_compiler_flag("-std=c++20" HAVE_REALFLAG)
  check_cxx_compiler_flag("-std=c++2a" HAVE_FAKEFLAG)
  check_cxx_compiler_flag("-std:c++20" HAVE_MSVCFLAG)
  if (HAVE_REALFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
  elseif (HAVE_FAKEFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a")
  elseif (HAVE_MSVCFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++20")
  else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++20 support. Please use a different C++ compiler.")
  endif()
elseif ("${CMAKE_CXX_STANDARD}" EQUAL 23)
  check_cxx_compiler_flag("-std=c++23" HAVE_REALFLAG)
  check_cxx_compiler_flag("-std=c++2b" HAVE_FAKEFLAG)
  check_cxx_compiler_flag("-std:c++latest" HAVE_MSVCFLAG)
  if (HAVE_REALFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23")
  elseif (HAVE_FAKEFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2b")
  elseif (HAVE_MSVCFLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std:c++latest")
  else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++23 support. Please use a different C++ compiler.")
  endif()
else()
  message(FATAL_ERROR "Unknown CMAKE_CXX_STANDARD option ${CMAKE_CXX_STANDARD}. Please use 14, 17, 20, or 23.")
endif()

set(SG14_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Output binary to predictable location.
set(BINARY_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_OUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BINARY_OUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BINARY_OUT_DIR})

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
	string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BINARY_OUT_DIR})
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BINARY_OUT_DIR})
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BINARY_OUT_DIR})
endforeach(OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES)

##
# Project
##
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	$<BUILD_INTERFACE:${SG14_INCLUDE_DIRECTORY}>
)

# add_subdirectory(benchmarks)
add_subdirectory(test)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}_targets)

install(EXPORT ${PROJECT_NAME}_targets
    NAMESPACE ${PROJECT_NAME}::
    FILE ${PROJECT_NAME}-config.cmake
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${PROJECT_NAME}"
)

install(DIRECTORY "${SG14_INCLUDE_DIRECTORY}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
