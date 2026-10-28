# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-job src/job.cpp)
add_library(oa::sim::job ALIAS oa-sim-job)
target_include_directories(oa-sim-job PUBLIC include)
target_compile_features(oa-sim-job PUBLIC cxx_std_20)
target_link_libraries(oa-sim-job PUBLIC oa-core-types PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-job-test tests/job_test.cpp)
  target_link_libraries(oa-sim-job-test PRIVATE oa-sim-job)
  add_test(NAME sim-job COMMAND oa-sim-job-test)
endif()
