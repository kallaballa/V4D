// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#ifndef OPENCV_PLAN_TESTED_PLAN_HPP_
#define OPENCV_PLAN_TESTED_PLAN_HPP_

#include "opencv2/plan/plan.hpp"

namespace cv {
namespace plan {
namespace test {

// Test helper that exposes the protected graph-driver methods of Plan so
// accuracy tests can drive the build/replay cycle manually.
class TestedPlan : public cv::plan::Plan {
public:
    using Plan::makeGraph;
    using Plan::runGraph;
    using Plan::clearGraph;

    const std::vector<cv::Ptr<cv::plan::Node>>& currentNodes() const {
        return currentNodes_;
    }

    size_t nodeCount() const {
        return currentNodes_.size();
    }
};

} // namespace test
} // namespace plan
} // namespace cv

#endif