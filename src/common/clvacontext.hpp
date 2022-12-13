#ifndef SRC_COMMON_CLVACONTEXT_HPP_
#define SRC_COMMON_CLVACONTEXT_HPP_

#include "clglcontext.hpp"
#include "util.hpp"

namespace kb {
class Viz2D;

class CLVAContext {
    friend class Viz2D;
    CLExecContext_t context_;
    CLGLContext &clglContext_;
    cv::UMat frameBuffer_;
    cv::UMat videoFrame_;
    cv::UMat rgbBuffer_;
    bool hasContext_ = false;
    cv::Size videoFrameSize_;
    CLExecContext_t getCLExecContext();
public:
    CLVAContext(CLGLContext &fbContext);
    cv::Size getVideoFrameSize();
    void setVideoFrameSize(const cv::Size& sz);
    bool capture(std::function<void(cv::UMat&)> fn);
    void write(std::function<void(const cv::UMat&)> fn);
    /*FIXME only public till https://github.com/opencv/opencv/pull/22780 is resolved.
     * required for manual initialization of VideoCapture/VideoWriter
     */
    bool hasContext();
    void copyContext();
};
} /* namespace kb */

#endif /* SRC_COMMON_CLVACONTEXT_HPP_ */
