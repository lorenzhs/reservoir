################################################################################
# CMakeLists.txt
#
# Root CMake build script for sampler
#
# Copyright (C) 2015-2017 Timo Bingmann <tb@panthema.net>
# Copyright (C) 2018-2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
################################################################################

cmake_minimum_required(VERSION 3.9.2...3.13)

# custom cmake scripts
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

project(reservoir LANGUAGES CXX)
#set_target_properties(reservoir PROPERTIES LINKER_LANGUAGE CXX)

# prohibit in-source builds
if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
  message(SEND_ERROR "In-source builds are not allowed.")
endif()

# default to Debug building for single-config generators
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message("Defaulting CMAKE_BUILD_TYPE to Debug")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type")
endif()


option(RESERVOIR_USE_LTO
  "Compile with -flto (link-time optimization)." ON)

option(RESERVOIR_USE_MKL
  "Build with Intel MKL if available." ON)

option(RESERVOIR_MORE_TESTS
  "Run more extensive tests." OFF)

################################################################################

# variables to collect compile-time definitions, include dirs, and libraries
set(RESERVOIR_INCLUDE_DIRS "")
set(RESERVOIR_LINK_LIBRARIES "")
set(EXEC_LINK_LIBRARIES "reservoir")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  # Require pthread
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

  # enable -g on Release builds to aid debugging
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

  # enable warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Wextra")

  # enable more warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic")

  # enable -march=native on Debug and Release builds
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-march=native" RESERVOIR_HAS_MARCH_NATIVE)
  if(RESERVOIR_HAS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
  endif()

  # remove -rdynamic from linker flags (smaller binaries which cannot be loaded
  # with dlopen() -- something no one needs)
  string(REGEX REPLACE "-rdynamic" ""
    CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS}")
  string(REGEX REPLACE "-rdynamic" ""
    CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS}")

  # warn on conversions
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion -Werror")

  # enable AddressSanitizer
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

  # enable ThreadSanitizer
  if(OFF)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -pie -fPIC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRESERVOIR_HAVE_THREAD_SANITIZER=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -pie -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DRESERVOIR_HAVE_THREAD_SANITIZER=1")
  endif()

  # enable UndefinedBehaviorSanitizer
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")

  # enable extra warnings on gcc
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wcast-qual")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nonnull-compare -Wno-address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Winit-self -Wnoexcept")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -Wredundant-decls")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstrict-null-sentinel -Wstrict-overflow=5")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wtautological-compare")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fipa-pure-const -Wsuggest-attribute=const")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-promo")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wold-style-cast")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsuggest-override")
  endif()
  # enable extra warnings on clang
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdeprecated")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wabstract-vbase-init")
  endif()
elseif(MSVC)
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
  # raise warnings as errors
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")

  ### disable verbose warnings:
  # warning C4589: Constructor of abstract class '...' ignores initializer for
  # virtual base class '...' (false positive warnings)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4589")
  # warning C4127: conditional expression is constant
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4127")
  # warning C4458: declaration of '...' hides class member
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4458")
  # warning C4459: declaration of '...' hides global declaration
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4459")
  # warning C4702: unreachable code
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4702")
  # warning C4250: ABC inherits XYZ via dominance
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4250")
  # warning C4503: decorated name length exceeded, name was truncated
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4503")
endif()


if(RESERVOIR_USE_LTO)
  # build with link-time optimization
  include(CheckIPOSupported)
  check_ipo_supported()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  set(TLX_USE_LTO ON CACHE BOOL "enable LTO for tlx")
endif()

if(APPLE)
  # disable warnings about "ranlib: file: libsampling.a(...cpp.o) has no symbols"
  set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
  set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
endif()

message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

################################################################################

# find pthreads
find_package(Threads REQUIRED)
set(RESERVOIR_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT} ${RESERVOIR_LINK_LIBRARIES})
if(CMAKE_USE_PTHREADS_INIT)
  set(RESERVOIR_LINK_LIBRARIES pthread atomic ${RESERVOIR_LINK_LIBRARIES})
endif()


# use tlx
add_subdirectory(extlib/tlx)
set(RESERVOIR_LINK_LIBRARIES tlx ${RESERVOIR_LINK_LIBRARIES})


# find MPI
find_package(MPI REQUIRED)
set(RESERVOIR_INCLUDE_DIRS ${MPI_INCLUDE_PATH} ${RESERVOIR_INCLUDE_DIRS})
set(RESERVOIR_LINK_LIBRARIES ${MPI_LIBRARIES} ${RESERVOIR_LINK_LIBRARIES})

# find boost::mpi
find_package(Boost COMPONENTS mpi REQUIRED)
if(Boost_FOUND)
  set(RESERVOIR_LINK_LIBRARIES Boost::mpi ${RESERVOIR_LINK_LIBRARIES})
endif()


# try to find MKL
if(RESERVOIR_USE_MKL)
  find_package(MKL)
  if(MKL_FOUND)
    #message(STATUS "Found MKL: ${MKL_ROOT_DIR}")
    add_definitions(-DRESERVOIR_HAVE_MKL)
    list(APPEND RESERVOIR_INCLUDE_DIRS ${MKL_INCLUDE_DIR})
    list(APPEND EXEC_LINK_LIBRARIES ${MKL_LP_LIBRARY} ${MKL_CORE_LIBRARY} ${MKL_SEQUENTIAL_LIBRARY})
  else()
    message(STATUS "Could not find MKL, no problem, using slower alternatives")
  endif()
endif()

if(RESERVOIR_MORE_TESTS)
  add_definitions("-DRESERVOIR_MORE_TESTS")
endif()


################################################################################
### Define source files

# glob general sources
file(GLOB_RECURSE RESERVOIR_SOURCES "${PROJECT_SOURCE_DIR}/reservoir/*.[ch]pp")
file(GLOB_RECURSE EXECUTABLES "${PROJECT_SOURCE_DIR}/benchmark/*.cpp")

################################################################################
### Build library

add_library(reservoir STATIC ${RESERVOIR_SOURCES})
set_target_properties(reservoir PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(reservoir PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(reservoir SYSTEM PUBLIC ${RESERVOIR_INCLUDE_DIRS})
target_link_libraries(reservoir ${RESERVOIR_LINK_LIBRARIES})

################################################################################
### Build all the executables

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmark")
foreach(target ${EXECUTABLES})
  get_filename_component(target_name ${target} NAME_WE)
  add_executable(${target_name} ${target})
  target_link_libraries(${target_name} ${EXEC_LINK_LIBRARIES})
endforeach(target)
unset(CMAKE_RUNTIME_OUTPUT_DIRECTORY)

################################################################################
### Build the tests

enable_testing()
add_subdirectory(tests)

################################################################################
