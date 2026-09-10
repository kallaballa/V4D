// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#include <functional>
#include <opencv2/core/types.hpp>

#ifndef MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_V4DCONTEXT_HPP_
#define MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_V4DCONTEXT_HPP_

namespace cv {
namespace v4d {
namespace detail {

class V4DContext {
public:
	virtual ~V4DContext() {}
    virtual int execute(const cv::Rect& vp, std::function<void()> fn) = 0;
};

class PlainContext : public V4DContext {
public:
	virtual ~PlainContext() {}
    virtual int execute(const cv::Rect& vp, std::function<void()> fn) override {
    	CV_UNUSED(vp);
    	fn();
    	return 1;
    }
};

}
}
}

#endif /* MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_V4DCONTEXT_HPP_ */
