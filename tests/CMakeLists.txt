################################################################################
# CMakeLists.txt
#
# CMake build script for tests
#
# Copyright (C) 2015-2017 Timo Bingmann <tb@panthema.net>
# Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
################################################################################


# macro for building test target programs with correct libraries
macro(reservoir_build_target TARGETNAME)
  add_executable(${TARGETNAME} ${ARGN})
  target_link_libraries(${TARGETNAME} reservoir tlx)
endmacro(reservoir_build_target)

# macro for building test programs, without main() in gtest_main
macro(reservoir_build_plain PROGNAME)
  string(REPLACE "/" "_" TESTNAME "${PROGNAME}") # replace slashes
  reservoir_build_target(${TESTNAME} ${PROGNAME}.cpp ${ARGN})
endmacro(reservoir_build_plain)

# macro for building test programs, adding gtest runner in gtest_main
macro(reservoir_build_only PROGNAME)
  # append gtest runner program.
  reservoir_build_plain(${PROGNAME} ${ARGN})
endmacro(reservoir_build_only)

# macro for registering test programs: maybe prepend valgrind
macro(reservoir_test_only TESTNAME)
  set(TARGETNAME ${TESTNAME} ${ARGN})
  string(REPLACE "/" "_" TARGETNAME "${TARGETNAME}") # replace slashes
  string(REPLACE ";" "_" TARGETNAME "${TARGETNAME}") # stringify list

  add_test(
    NAME ${TARGETNAME}
    COMMAND ${TESTNAME} ${ARGN})
endmacro(reservoir_test_only)

# macro for building and running test programs
macro(reservoir_build_test PROGNAME)
  reservoir_build_only(${PROGNAME})
  string(REPLACE "/" "_" TESTNAME "${PROGNAME}") # replace slashes
  reservoir_test_only(${TESTNAME})
endmacro(reservoir_build_test)

reservoir_build_only(btree_speedtest)

reservoir_build_test(btree_test)

reservoir_build_test(btree_fail)

################################################################################
