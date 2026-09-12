// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#ifndef __OPENCV_PERF_PRECOMP_HPP__
#define __OPENCV_PERF_PRECOMP_HPP__

#include "opencv2/ts.hpp"          // pulls in opencv2/ts/ts_perf.hpp
#include "opencv2/imgproc.hpp"     // cvtColor in the F() test
#include "opencv2/plan/plan.hpp"
#include "MockPlanRuntime.hpp"     // from ../test (added via CMake include dir)
#include "TestedPlan.hpp"          // from ../test

#include <vector>
#include <cstddef>

namespace opencv_test {
using namespace perf;
} // namespace

#endif