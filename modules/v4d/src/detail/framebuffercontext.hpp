// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#ifndef SRC_OPENCV_FRAMEBUFFERCONTEXT_HPP_
#define SRC_OPENCV_FRAMEBUFFERCONTEXT_HPP_

#ifdef __EMSCRIPTEN__
#  include <emscripten/threading.h>
#endif

//FIXME
#include "opencv2/v4d/detail/cl.hpp"
#include <opencv2/core/ocl.hpp>
#include "opencv2/v4d/util.hpp"
#include <iostream>

#if defined(__EMSCRIPTEN__) || defined(OPENCV_V4D_USE_ES3)
#define GLFW_INCLUDE_ES3
#define GLFW_INCLUDE_GLEXT
#endif

#include <GLFW/glfw3.h>

struct GLFWwindow;
namespace cv {
namespace v4d {
class V4D;
namespace detail {
typedef cv::ocl::OpenCLExecutionContext CLExecContext_t;
typedef cv::ocl::OpenCLExecutionContextScope CLExecScope_t;

/*!
 * The FrameBufferContext acquires the framebuffer from OpenGL (either by up-/download or by cl-gl sharing)
 */
class FrameBufferContext {
    typedef unsigned int GLuint;
    typedef signed int GLint;

    friend class CLVAContext;
    friend class GLContext;
    friend class NanoVGContext;
    friend class NanoguiContext;
    friend class cv::v4d::V4D;
    V4D* v4d_ = nullptr;
    bool offscreen_;
    string title_;
    int major_;
    int minor_;
    bool compat_;
    int samples_;
    bool debug_;
    GLFWwindow* glfwWindow_ = nullptr;
    bool clglSharing_ = true;
    bool isVisible_;
    GLuint frameBufferID_ = 0;
    GLuint onscreenTextureID_ = 0;
    GLuint textureID_ = 0;
    GLuint renderBufferID_ = 0;
    GLuint pboID_ = 0;
    GLint viewport_[4];
#ifndef __EMSCRIPTEN__
    cl_mem clImage_ = nullptr;
    CLExecContext_t context_;
#endif
    cv::Size frameBufferSize_;
    bool isShared_ = false;
    GLFWwindow* sharedWindow_;
    const FrameBufferContext* parent_;

    //data and handles for webgl copying
    GLint texture_hdl;
    GLint resolution_hdl;

    GLuint shader_program_hdl;

    //vertex array
    GLuint copyVao;
    GLuint copyVbo, copyEbo;

    // vertex position, color
    float copyVertices[12] = {
    //    x      y      z
    -1.0f, -1.0f, -0.0f,
    1.0f, 1.0f, -0.0f,
    -1.0f, 1.0f, -0.0f,
    1.0f, -1.0f, -0.0f };

    unsigned int copyIndices[6] = {
    //  2---,1
    //  | .' |
    //  0'---3
            0, 1, 2, 0, 3, 1 };

    GLuint copyFramebuffer_;
    GLuint copyTexture_;
    int index_;
public:
    /*!
     * Acquires and releases the framebuffer from and to OpenGL.
     */
    class FrameBufferScope {
        FrameBufferContext& ctx_;
        cv::UMat& m_;
    public:
        /*!
         * Aquires the framebuffer via cl-gl sharing.
         * @param ctx The corresponding #FrameBufferContext.
         * @param m The UMat to bind the OpenGL framebuffer to.
         */
        FrameBufferScope(FrameBufferContext& ctx, cv::UMat& m) :
                ctx_(ctx), m_(m) {
            ctx_.acquireFromGL(m_);
        }
        /*!
         * Releases the framebuffer via cl-gl sharing.
         */
        ~FrameBufferScope() {
            ctx_.releaseToGL(m_);
        }
    };

    /*!
     * Setups and tears-down OpenGL states.
     */
    class GLScope {
        FrameBufferContext& ctx_;
    public:
        /*!
         * Setup OpenGL states.
         * @param ctx The corresponding #FrameBufferContext.
         */
        GLScope(FrameBufferContext& ctx, GLenum framebufferTarget = GL_FRAMEBUFFER) :
                ctx_(ctx) {
            ctx_.begin(framebufferTarget);
        }
        /*!
         * Tear-down OpenGL states.
         */
        ~GLScope() {
            ctx_.end();
        }
    };

    /*!
     * Create a FrameBufferContext with given size.
     * @param frameBufferSize The frame buffer size.
     */
    FrameBufferContext(V4D& v4d, const cv::Size& frameBufferSize, bool offscreen,
            const string& title, int major, int minor, bool compat, int samples, bool debug, GLFWwindow* sharedWindow, const FrameBufferContext* parent);

    FrameBufferContext(V4D& v4d, const string& title, const FrameBufferContext& other);

    /*!
     * Default destructor.
     */
    virtual ~FrameBufferContext();

    GLuint getFramebufferID();
    GLuint getTextureID();
    /*!
     * Get the framebuffer size.
     * @return The framebuffer size.
     */
    cv::Size size();
    void copyTo(cv::UMat& dst);
    void copyFrom(const cv::UMat& src);

    /*!
      * Execute function object fn inside a framebuffer context.
      * The context acquires the framebuffer from OpenGL (either by up-/download or by cl-gl sharing)
      * and provides it to the functon object. This is a good place to use OpenCL
      * directly on the framebuffer.
      * @param fn A function object that is passed the framebuffer to be read/manipulated.
      */
    void execute(std::function<void(cv::UMat&)> fn);
    cv::Vec2f position();
    float pixelRatioX();
    float pixelRatioY();
    void makeCurrent();
    bool isResizable();
    void setResizable(bool r);
    void setWindowSize(const cv::Size& sz);
    cv::Size getWindowSize();
    bool isFullscreen();
    void setFullscreen(bool f);
    cv::Size getNativeFrameBufferSize();
    void setVisible(bool v);
    bool isVisible();
    void close();
    bool isClosed();
    bool isShared();
protected:
    V4D& getV4D();
    int getIndex();
    void setup(const cv::Size& sz);
    void teardown();
    void initWebGLCopy(FrameBufferContext& dst);
    void doWebGLCopy(FrameBufferContext& dst);
    /*!
     * The UMat used to copy or bind (depending on cl-gl interop capability) the OpenGL framebuffer.
     */
    /*!
     * The internal framebuffer exposed as OpenGL Texture2D.
     * @return The texture object.
     */
    cv::ogl::Texture2D& getTexture2D();

    GLFWwindow* getGLFWWindow();

#ifndef __EMSCRIPTEN__
    /*!
     * Get the current OpenCLExecutionContext
     * @return The current OpenCLExecutionContext
     */
    CLExecContext_t& getCLExecContext();
#endif
    /*!
     * Blit the framebuffer to the screen
     * @param viewport ROI to blit
     * @param windowSize The size of the window to blit to
     * @param stretch if true stretch the framebuffer to window size
     */
    void blitFrameBufferToScreen(const cv::Rect& viewport, const cv::Size& windowSize,
            bool stretch = false, GLuint drawFramebufferID = 0);
private:
    void loadBuffers();
    void loadShader();
    void init();
    /*!
     * Setup OpenGL states.
     */
    void begin(GLenum framebufferTarget);
    /*!
     * Tear-down OpenGL states.
     */
    void end();
    /*!
     * Download the framebuffer to UMat m.
     * @param m The target UMat.
     */
    void download(cv::UMat& m);
    /*!
     * Uploat UMat m to the framebuffer.
     * @param m The UMat to upload.
     */
    void upload(const cv::UMat& m);
    /*!
     * Acquire the framebuffer using cl-gl sharing.
     * @param m The UMat the framebuffer will be bound to.
     */
    void acquireFromGL(cv::UMat& m);
    /*!
     * Release the framebuffer using cl-gl sharing.
     * @param m The UMat the framebuffer is bound to.
     */
    void releaseToGL(cv::UMat& m);
    void toGLTexture2D(cv::UMat& u, cv::ogl::Texture2D& texture);
    void fromGLTexture2D(const cv::ogl::Texture2D& texture, cv::UMat& u);

    double blitScale();
    GLint blitOffsetX();
    GLint blitOffsetY();

    cv::UMat framebuffer_;
    /*!
     * The texture bound to the OpenGL framebuffer.
     */
    cv::ogl::Texture2D* texture_ = nullptr;
};
}
}
}

#endif /* SRC_OPENCV_FRAMEBUFFERCONTEXT_HPP_ */
