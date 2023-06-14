// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#include "clvacontext.hpp"
#include <opencv2/imgproc.hpp>

namespace cv {
namespace v4d {
namespace detail {

CLVAContext::CLVAContext(FrameBufferContext& mainFbContext) : mainFbContext_(mainFbContext) {
}

bool CLVAContext::capture(std::function<void(cv::UMat&)> fn, cv::UMat& output) {
    cv::Size fbSize = mainFbContext_.size();
    if (!context_.empty()) {
#ifndef __EMSCRIPTEN__
        CLExecScope_t scope(context_);
#endif
        fn(readFrame_);
    } else {
        fn(readFrame_);
    }

    if (readFrame_.empty())
        return false;
    resizePreserveAspectRatio(readFrame_, readRGBBuffer_, mainFbContext_.size());
    cv::cvtColor(readRGBBuffer_, output, cv::COLOR_RGB2BGRA);

    return true;
}

void CLVAContext::write(std::function<void(const cv::UMat&)> fn, cv::UMat& input) {
#ifndef __EMSCRIPTEN__
        CLExecScope_t scope(context_);
#endif
        cv::cvtColor(input, writeRGBBuffer_, cv::COLOR_BGRA2RGB);
        fn(writeRGBBuffer_);
}

bool CLVAContext::hasContext() {
    return !context_.empty();
}

void CLVAContext::copyContext() {
#ifndef __EMSCRIPTEN__
    context_ = CLExecContext_t::getCurrent();
#endif
}

CLExecContext_t CLVAContext::getCLExecContext() {
    return context_;
}
}
}
}
