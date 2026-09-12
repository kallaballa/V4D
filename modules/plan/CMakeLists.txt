cmake_policy(SET CMP0079 NEW)

set(the_description "Plan-DSL Module")

# Check CXX Features
get_property(known_features GLOBAL PROPERTY CMAKE_CXX_KNOWN_FEATURES)
list (FIND known_features "cxx_std_20" idx)
if (${idx} LESS 0)
  message(STATUS "Module opencv_plan disabled because it requires C++20")
  ocv_module_disable(plan)
endif()

if(NOT (TARGET ${the_module}))
  ocv_add_module(plan opencv_core opencv_imgproc)

  ocv_glob_module_sources()
  ocv_module_include_directories("${CMAKE_CURRENT_LIST_DIR}/include")

  ocv_create_module()
  set_target_properties(${the_module} PROPERTIES LINKER_LANGUAGE CXX)

  target_compile_features(${the_module} PRIVATE cxx_std_20)

  target_compile_options(${the_module} PRIVATE
    $<$<CONFIG:Release>:-march=native -ftree-vectorize -funroll-loops>
    $<$<CONFIG:RelWithDebInfo>:-march=native -ftree-vectorize -funroll-loops>
  )
endif()

# Accuracy test suite (Plan features only; no V4D).
# Requires C++20 for the test target because plan.hpp uses <barrier>/<semaphore>.
if(BUILD_TESTS AND NOT BUILD_opencv_world)
  ocv_add_accuracy_tests()
  if(TARGET opencv_test_plan)
    target_compile_features(opencv_test_plan PRIVATE cxx_std_20)
    target_compile_options(opencv_test_plan PRIVATE
      $<$<CONFIG:Release>:-march=native -ftree-vectorize -funroll-loops>
      $<$<CONFIG:RelWithDebInfo>:-march=native -ftree-vectorize -funroll-loops>
    )
  endif()
endif()

# Performance test suite (Plan features only; no V4D).
# Requires C++20 for the perf target too; reuses MockPlanRuntime/TestedPlan
# from the accuracy test dir.
if(BUILD_PERF_TESTS AND NOT BUILD_opencv_world)
  ocv_add_perf_tests()
  if(TARGET opencv_perf_plan)
    target_compile_features(opencv_perf_plan PRIVATE cxx_std_20)
    target_include_directories(opencv_perf_plan PRIVATE "${CMAKE_CURRENT_LIST_DIR}/test")
    target_compile_options(opencv_perf_plan PRIVATE
      $<$<CONFIG:Release>:-march=native -ftree-vectorize -funroll-loops>
      $<$<CONFIG:RelWithDebInfo>:-march=native -ftree-vectorize -funroll-loops>
    )
  endif()
endif()
