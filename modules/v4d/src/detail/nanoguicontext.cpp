// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#include "opencv2/v4d/v4d.hpp"
#include "nanoguicontext.hpp"

namespace cv {
namespace v4d {
namespace detail {

NanoguiContext::NanoguiContext(V4D& v4d, FrameBufferContext& fbContext) :
        mainFbContext_(fbContext), nguiFbContext_(v4d, "NanoGUI", fbContext) {
    run_sync_on_main<3>([this](){ init(); });
}

void NanoguiContext::init() {
//    GL_CHECK(glEnable(GL_DEPTH_TEST));
//    GL_CHECK(glDepthFunc(GL_LESS));
//    GL_CHECK(glEnable(GL_STENCIL_TEST));
//    GL_CHECK(glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP));
//    GL_CHECK(glStencilFunc(GL_ALWAYS, 0, 0xffffffff));
    FrameBufferContext::GLScope glScope(fbCtx(), GL_DRAW_FRAMEBUFFER);
    glClear(GL_STENCIL_BUFFER_BIT);
//    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    screen_ = new nanogui::Screen();
    screen_->initialize(nguiFbContext_.getGLFWWindow(), false);
    fbCtx().setWindowSize(fbCtx().size());
    form_ = new cv::v4d::FormHelper(screen_);
}

void NanoguiContext::render() {
    run_sync_on_main<4>([&,this](){
#ifdef __EMSCRIPTEN__
//    fb_.create(mainFbContext_.size(), CV_8UC4);
//    preFB_.create(mainFbContext_.size(), CV_8UC4);
//    postFB_.create(mainFbContext_.size(), CV_8UC4);
//    {
//        FrameBufferContext::GLScope mainGlScope(mainFbContext_);
//        FrameBufferContext::FrameBufferScope fbScope(mainFbContext_, fb_);
//        fb_.copyTo(preFB_);
//    }
//    {
//        FrameBufferContext::GLScope glGlScope(fbCtx());
//        FrameBufferContext::FrameBufferScope fbScope(fbCtx(), fb_);
//        preFB_.copyTo(fb_);
//    }    glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
#endif
    {

//        GL_CHECK(glEnable(GL_DEPTH_TEST));
//        GL_CHECK(glDepthFunc(GL_LESS));
//        GL_CHECK(glEnable(GL_STENCIL_TEST));
//        GL_CHECK(glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP));
//        GL_CHECK(glStencilFunc(GL_ALWAYS, 0, 0xffffffff));
        FrameBufferContext::GLScope glScope(fbCtx(), GL_FRAMEBUFFER);
        glClear(GL_STENCIL_BUFFER_BIT);
//        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

//        float w = fbCtx().size().width;
//        float h = fbCtx().size().height;
//        float r = fbCtx().getXPixelRatio();

//        nvgSave(context_);
//        nvgBeginFrame(context_, w, h, r);

//        screen().draw_setup();
        screen().draw_widgets();
//        screen().nvg_flush();
//        //FIXME make nvgCancelFrame possible
//        nvgEndFrame(context_);
//        nvgRestore(context_);
    }
#ifdef __EMSCRIPTEN__
//    {
//        FrameBufferContext::GLScope glScope(fbCtx());
//        FrameBufferContext::FrameBufferScope fbScope(fbCtx(), fb_);
//        fb_.copyTo(postFB_);
//    }
//    {
//        FrameBufferContext::GLScope mainGlScope(mainFbContext_);
//        FrameBufferContext::FrameBufferScope fbScope(mainFbContext_, fb_);
//        postFB_.copyTo(fb_);
//    }
#endif
    });
}

void NanoguiContext::build(std::function<void(cv::v4d::FormHelper&)> fn) {
    run_sync_on_main<5>([fn,this](){
        FrameBufferContext::GLScope glScope(fbCtx());
        fn(form());
        screen().perform_layout();
    });
}

nanogui::Screen& NanoguiContext::screen() {
    return *screen_;
}

cv::v4d::FormHelper& NanoguiContext::form() {
    return *form_;
}

FrameBufferContext& NanoguiContext::fbCtx() {
    return nguiFbContext_;
}
}
}
}
