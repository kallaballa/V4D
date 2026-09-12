// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#include "../include/opencv2/v4d/detail/framebuffercontext.hpp"
#include "../include/opencv2/v4d/v4d.hpp"
#include "../include/opencv2/v4d/detail/gl.hpp"
#include <opencv2/core/ocl.hpp>
#include "opencv2/core/opengl.hpp"
#include <opencv2/core/utils/logger.hpp>
#include <exception>
#include <iostream>
#include "../../third/imgui/backends/imgui_impl_glfw.h"

#define GLAD_GL_IMPLEMENTATION
#if !defined(__APPLE__)
#	if!defined(OPENCV_V4D_USE_ES3)
#	  include "glad/gl.h"
#	endif
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using std::cerr;
using std::cout;
using std::endl;

namespace cv {
namespace v4d {

namespace detail {

static void glfw_error_callback(int error, const char* description) {
    CV_UNUSED(error);
    CV_UNUSED(description);
    CV_LOG_DEBUG(nullptr, "GLFW Error: (" + std::to_string(error) + ") "+ description);
}

static void draw_quad()
{
    const static float quadVertices[] = {
        // positions        // texture Coords
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    };
    // setup quad VAO
    static thread_local unsigned int quadVAO = 0;
    static thread_local unsigned int quadVBO = 0;
    if (quadVAO == 0)
    {
        GL_CHECK(glGenVertexArrays(1, &quadVAO));
        GL_CHECK(glGenBuffers(1, &quadVBO));
        GL_CHECK(glBindVertexArray(quadVAO));
        GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, quadVBO));
        GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW));
        GL_CHECK(glEnableVertexAttribArray(0));
        GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0));
        GL_CHECK(glEnableVertexAttribArray(1));
        GL_CHECK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))));
    }
    GL_CHECK(glBindVertexArray(quadVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
    GL_CHECK(glBindVertexArray(0));
}

FrameBufferContext::FrameBufferContext(const string& title, cv::Ptr<FrameBufferContext> other) :
				FrameBufferContext(other->framebufferSize_, title, other->major_,  other->minor_, other->samples_, other->glfwWindow_, other, false, other->configFlags() ) {
}

FrameBufferContext::FrameBufferContext(const cv::Size& framebufferSize,
        const string& title, int major, int minor, int samples, GLFWwindow* parentWindow, cv::Ptr<FrameBufferContext> parent, bool root, int confFlags) :
        title_(title), major_(major), minor_(minor), samples_(samples), configFlags_(confFlags), isVisible_(!(confFlags & FBConfigFlags::OFFSCREEN)), framebufferSize_(framebufferSize), parent_(parent), isRoot_(root), framebuffer_(), view_() {
    CV_UNUSED(parentWindow);
}


static std::mutex make_mtx_;

cv::Ptr<FrameBufferContext> FrameBufferContext::make(const string& title, cv::Ptr<FrameBufferContext> other) {
    std::lock_guard lock(make_mtx_);
    cv::Ptr<FrameBufferContext> ptr = cv::makePtr<FrameBufferContext>(title, other);
	ptr->self_ = ptr;
	ptr->init();
	return ptr;
}

cv::Ptr<FrameBufferContext> FrameBufferContext::make(const cv::Size& sz, const string& title, const int& major, const int& minor, const int& samples, GLFWwindow* parentWindow, cv::Ptr<FrameBufferContext> parent, const bool& root, const int& confFlags) {
    std::lock_guard lock(make_mtx_);
    cv::Ptr<FrameBufferContext> ptr = cv::makePtr<FrameBufferContext>(sz, title, major, minor, samples, parentWindow, parent, root, confFlags);
	ptr->self_ = ptr;
	ptr->init();
	return ptr;
}

FrameBufferContext::~FrameBufferContext() {
	teardown();
	self_ = nullptr;
}

int FrameBufferContext::configFlags() {
	return configFlags_;
}

void FrameBufferContext::loadShaders(const size_t& index) {
    const string vert = R"(
    		precision highp float;
    		
    		layout (location = 0) in vec2 aPos;
    		layout (location = 1) in vec2 aTexCoords;
    		
    		out vec2 TexCoords;
    		
    		void main()
    		{
    			gl_Position = vec4(aPos, 0.0, 1.0);
    			TexCoords = aTexCoords;
    		}
    	)";

    const string frag = R"(
    precision mediump float;
    
	in vec2 TexCoords;
	out vec4 FragColor;
    
    uniform sampler2D texture0;

    void main()
    {      
        vec4 texColor0 = texture(texture0, TexCoords);
        if(texColor0.a == 0.0)
            discard;
        else
            FragColor = texColor0;
    }
)";

    unsigned int handles[3];
    cv::v4d::init_shaders(handles, vert.c_str(), frag.c_str(), "fragColor");
    shader_program_hdls_[index] = handles[0];
}

void FrameBufferContext::initBlend(const size_t& index) {
    GL_CHECK(glGenFramebuffers(1, &copyFramebuffers_[index]));
    GL_CHECK(glGenTextures(1, &copyTextures_[index]));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, copyFramebuffers_[index]));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, copyTextures_[index]));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size().width, size().height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copyTextures_[index], 0));
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    loadShaders(index);

    // lookup the sampler locations.
    texture_hdls_[index] = glGetUniformLocation(shader_program_hdls_[index], "texture0");
}

void FrameBufferContext::blendFramebuffer(const GLuint& otherID) {
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, getFramebufferID()));
    GL_CHECK(glViewport(0, 0, size().width, size().height));
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, copyTextures_[otherID]));
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    GL_CHECK(glUseProgram(shader_program_hdls_[otherID]));
    GL_CHECK(glUniform1i(texture_hdls_[otherID], 0));

    draw_quad();
    GL_CHECK(glDisable(GL_BLEND));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
//    GL_CHECK(glFinish());
}

GLuint FrameBufferContext::getFramebufferID() {
    return framebufferID_;
}

GLuint FrameBufferContext::getTextureID() {
    return textureID_;
}

void FrameBufferContext::init() {
    if(parent_) {
        if(isRoot()) {
            textureID_ = 0;
            renderBufferID_ = 0;
    		onscreenTextureID_ = parent_->textureID_;
    		onscreenRenderBufferID_ = parent_->renderBufferID_;
        } else {
            textureID_ = parent_->textureID_;
            renderBufferID_ = parent_->renderBufferID_;
            onscreenTextureID_ = parent_->onscreenTextureID_;
            onscreenRenderBufferID_ = parent_->onscreenRenderBufferID_;
        }
    } else if (glfwInit() != GLFW_TRUE) {
    	cerr << "Can't init GLFW" << endl;
    	exit(1);
    }

    glfwSetErrorCallback(cv::v4d::detail::glfw_error_callback);
    glfwSetTime(0);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif defined(OPENCV_V4D_USE_ES3)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND)) {
      glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    } else {
        CV_LOG_WARNING(nullptr, "No Wayland Support");
    }
#else
    CV_LOG_WARNING(nullptr, "Wayland platform selection requires GLFW >= 3.4");
#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API) ;
#endif
    glfwWindowHint(GLFW_SAMPLES, samples_);
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, configFlags() & FBConfigFlags::RESIZEABLE ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, !isRoot() || (configFlags() & FBConfigFlags::OFFSCREEN) ? GLFW_FALSE : GLFW_TRUE );
//    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    glfwWindow_ = glfwCreateWindow(framebufferSize_.width, framebufferSize_.height, title_.c_str(), nullptr, parent_ ? parent_->getGLFWWindow() : nullptr);


    if (glfwWindow_ == nullptr) {
        //retry with native api
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
        glfwWindow_ = glfwCreateWindow(framebufferSize_.width, framebufferSize_.height, title_.c_str(), nullptr, parent_ ? parent_->getGLFWWindow() : nullptr);

        if (glfwWindow_ == nullptr) {
        	CV_Error(Error::StsError, "Unable to initialize window.");
        }
    }

    FrameBufferContext::WindowScope winScope(self());

    if(!hasParent()) {
        glfwSwapInterval(configFlags() & FBConfigFlags::VSYNC ? 1 : 0);
    }

#if !defined(__APPLE__) && !defined(OPENCV_V4D_USE_ES3)
    if (!hasParent()) {
    	GladGLContext context;
    	int version = gladLoadGLContext(&context, glfwGetProcAddress);
        if (version == 0) {
            CV_Error(cv::Error::StsError, "Failed to initialize OpenGL context\n");
        }
    }
#endif
    try {
        if (isRoot() && is_clgl_sharing_supported())
            cv::ogl::ocl::initializeContextFromGL();
        else
            clglSharing_ = false;
    } catch (std::exception& ex) {
        CV_LOG_WARNING(nullptr, "CL-GL sharing failed: %s" << ex.what());
        clglSharing_ = false;
    } catch (...) {
    	CV_LOG_WARNING(nullptr, "CL-GL sharing failed with unknown error");
        clglSharing_ = false;
    }

    if(cv::ocl::useOpenCL())
    	context_ = CLExecContext_t::getCurrent();

    setup();
    if(GlobalState::isMain() && !parent_) {
        gwe::init<cv::Point>(
          [](GLFWwindow *window, int key, int scancode, int action, int mods){
              ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
              if(ImGui::GetCurrentContext()) {
                  return ImGui::GetIO().WantCaptureKeyboard;
              } else {
                  return false;
              }
          }, [](GLFWwindow *window, int button, int action, int mods) {
              ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
              if(ImGui::GetCurrentContext()) {
                  return ImGui::GetIO().WantCaptureMouse;
              } else {
                  return false;
              }
          }, [](GLFWwindow *window, double xoffset, double yoffset) {
              ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
              if(ImGui::GetCurrentContext()) {
                  return ImGui::GetIO().WantCaptureMouse;
              }
              return false;
          }, [](GLFWwindow *window, double xpos, double ypos) {
              ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
              if(ImGui::GetCurrentContext()) {
                  return ImGui::GetIO().WantCaptureMouse;
              }
              return false;
          }, [](GLFWwindow *window, int w, int h) {
              CV_UNUSED(window);
              V4D::instance()->set(V4D::Keys::WINDOW_SIZE, cv::Size(w, h), false);
              return false;
          }
      );
    }
}

void FrameBufferContext::setup() {
	cv::Size sz = framebufferSize_;
    CLExecScope_t clExecScope(getCLExecContext());
    framebuffer_.create(sz, CV_8UC4);
	view_ = framebuffer_(cv::Rect(0, 0, sz.width, sz.height));

    if(isRoot()) {
    	GL_CHECK(glGenFramebuffers(1, &framebufferFlippedID_));
    	GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, framebufferFlippedID_));
    	GL_CHECK(glGenTextures(1, &textureFlippedID_));
    	GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureFlippedID_));
    	GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    	GL_CHECK(
    			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz.width, sz.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    	GL_CHECK(
    			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureFlippedID_, 0));
    	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    	GL_CHECK(glGenFramebuffers(1, &framebufferID_));
        GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, framebufferID_));
        GL_CHECK(glGenRenderbuffers(1, &renderBufferID_));

        GL_CHECK(glGenTextures(1, &textureID_));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureID_));
        GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        GL_CHECK(
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz.width, sz.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID_, 0));

/*        GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, renderBufferID_));
        GL_CHECK(
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, sz.width, sz.height));
        GL_CHECK(
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, renderBufferID_));*/

        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    } else if(hasParent()) {
        GL_CHECK(glGenFramebuffers(1, &framebufferID_));
        GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, framebufferID_));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureID_));
        GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        GL_CHECK(
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz.width, sz.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID_, 0));
        GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, renderBufferID_));
        GL_CHECK(
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, sz.width, sz.height));
        GL_CHECK(
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, renderBufferID_));
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    } else
    	CV_Assert(false);

    currentFBO_ = framebufferID_;
}

void FrameBufferContext::teardown() {
    using namespace cv::ocl;
#ifdef HAVE_OPENCL
    if(cv::ocl::useOpenCL() && clImage_ != nullptr && !getCLExecContext().empty()) {
        CLExecScope_t clExecScope(getCLExecContext());

        cl_int status = 0;
        cl_command_queue q = (cl_command_queue) Queue::getDefault().ptr();

        status = clEnqueueReleaseGLObjects(q, 1, &clImage_, 0, NULL, NULL);
        if (status != CL_SUCCESS)
            CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clEnqueueReleaseGLObjects failed: %d", status));

        status = clFinish(q); // TODO Use events
        if (status != CL_SUCCESS)
            CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clFinish failed: %d", status));

        status = clReleaseMemObject(clImage_); // TODO RAII
        if (status != CL_SUCCESS)
            CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clReleaseMemObject failed: %d", status));
        clImage_ = nullptr;
    }
#endif
    glBindTexture(GL_TEXTURE_2D, 0);
    glGetError();
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetError();

    GL_CHECK(glDeleteRenderbuffers(1, &renderBufferID_));
    GL_CHECK(glDeleteTextures(1, &textureID_));
    GL_CHECK(glDeleteFramebuffers(1, &framebufferID_));
    if(textureFlippedID_) {
        GL_CHECK(glDeleteTextures(1, &textureFlippedID_));
    }
    if(framebufferFlippedID_) {
        GL_CHECK(glDeleteFramebuffers(1, &framebufferFlippedID_));
    }
}

void FrameBufferContext::flip() {
	GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferID_));
//    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureID_));
//    GL_CHECK(
//            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID_, 0));
    assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebufferFlippedID_));
    assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	blitFrameBufferToFrameBuffer(cv::Rect(0, 0, size().width, size().height), size(), false, true);
}

void FrameBufferContext::unflip() {
	GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferFlippedID_));
    assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	//    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureFlippedID_));
//    GL_CHECK(
//            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureFlippedID_, 0));
//    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebufferID_));
    assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	blitFrameBufferToFrameBuffer(cv::Rect(0, 0, size().width, size().height), size(), false, true);
}

#ifdef HAVE_OPENCL
void FrameBufferContext::toGLTexture2D(cv::UMat& u, const GLuint& texID) {
	CV_UNUSED(texID);
    CV_Assert(clImage_ != nullptr);
	using namespace cv::ocl;

    cl_int status = 0;
    cl_command_queue q = (cl_command_queue) context_.getQueue().ptr();
    cl_mem clBuffer = (cl_mem) u.handle(ACCESS_READ);

//    status = clFinish(q); // TODO Use events
//    if (status != CL_SUCCESS)
//        CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clFinish failed: %d", status));

    size_t offset = 0;
    size_t dst_origin[3] = { 0, 0, 0 };
    size_t region[3] = { (size_t) u.cols, (size_t) u.rows, 1 };
    status = clEnqueueCopyBufferToImage(q, clBuffer, clImage_, offset, dst_origin, region, 0, NULL,
    NULL);
    if (status != CL_SUCCESS)
        throw std::runtime_error("OpenCL: clEnqueueCopyBufferToImage failed: " + std::to_string(status));

    status = clEnqueueReleaseGLObjects(q, 1, &clImage_, 0, NULL, NULL);
    if (status != CL_SUCCESS)
         throw std::runtime_error("OpenCL: clEnqueueReleaseGLObjects failed: " + std::to_string(status));

//    status = clFinish(q); // TODO Use events
//    if (status != CL_SUCCESS)
//        CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clFinish failed: %d", status));
}

void FrameBufferContext::fromGLTexture2D(const GLuint& texID, cv::UMat& u) {

	using namespace cv::ocl;
    cl_command_queue q = (cl_command_queue) context_.getQueue().ptr();
    cl_int status = 0;

//    status = clFinish(q); // TODO Use events
//    if (status != CL_SUCCESS)
//        CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clFinish failed: %d", status));

    if(clImage_ == nullptr) {
		Context& ctx = context_.getContext();
		cl_context context = (cl_context) ctx.ptr();
		clImage_ = clCreateFromGLTexture(context, CL_MEM_READ_WRITE, 0x0DE1, 0, texID,
				&status);
    }
	if (status != CL_SUCCESS)
		throw std::runtime_error("OpenCL: clCreateFromGLTexture failed: " + std::to_string(status));

    status = clEnqueueAcquireGLObjects(q, 1, &clImage_, 0, NULL, NULL);
    if (status != CL_SUCCESS)
        throw std::runtime_error("OpenCL: clEnqueueAcquireGLObjects failed: " + std::to_string(status));

    cl_mem clBuffer = (cl_mem) u.handle(ACCESS_WRITE);

    size_t offset = 0;
    size_t src_origin[3] = { 0, 0, 0 };
    size_t region[3] = { (size_t) u.cols, (size_t) u.rows, 1 };
    status = clEnqueueCopyImageToBuffer(q, clImage_, clBuffer, src_origin, region, offset, 0, NULL,
    NULL);
    if (status != CL_SUCCESS)
        throw std::runtime_error("OpenCL: clEnqueueCopyImageToBuffer failed: " + std::to_string(status));

//    status = clFinish(q); // TODO Use events
//    if (status != CL_SUCCESS)
//        CV_Error_(cv::Error::OpenCLApiCallError, ("OpenCL: clFinish failed: %d", status));

}
#endif
const cv::Size& FrameBufferContext::size() const {
    return framebufferSize_;
}

void FrameBufferContext::copyTo(cv::UMat& dst) {
	FrameBufferContext::WindowScope winScope(self());
	if(!getCLExecContext().empty()) {
		CLExecScope_t clExecScope(getCLExecContext());
		FrameBufferContext::GLScope glScope(self(), GL_FRAMEBUFFER);
		FrameBufferContext::FrameBufferScope fbScope(self(), framebuffer_);
		framebuffer_.copyTo(dst);
	} else {
		FrameBufferContext::GLScope glScope(self(), GL_FRAMEBUFFER);
		FrameBufferContext::FrameBufferScope fbScope(self(), framebuffer_);
		framebuffer_.copyTo(dst);
	}
}

void FrameBufferContext::copyFrom(const cv::UMat& src) {
	FrameBufferContext::WindowScope winScope(self());
	if(!getCLExecContext().empty()) {
		CLExecScope_t clExecScope(getCLExecContext());
		FrameBufferContext::GLScope glScope(self(), GL_FRAMEBUFFER);
		FrameBufferContext::FrameBufferScope fbScope(self(), framebuffer_);
		src.copyTo(framebuffer_);
	} else {
		FrameBufferContext::GLScope glScope(self(), GL_FRAMEBUFFER);
		FrameBufferContext::FrameBufferScope fbScope(self(), framebuffer_);
		src.copyTo(framebuffer_);
	}
}

void FrameBufferContext::copyToRootWindow() {
	FrameBufferContext::WindowScope winScope(self());
	FrameBufferContext::GLScope glScope(self(), GL_READ_FRAMEBUFFER);
	GL_CHECK(glReadBuffer(GL_COLOR_ATTACHMENT0));
	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, onscreenTextureID_));
	GL_CHECK(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size().width, size().height));
}

GLFWwindow* FrameBufferContext::getGLFWWindow() const {
    return glfwWindow_;
}

CLExecContext_t& FrameBufferContext::getCLExecContext() {
    return context_;
}

void FrameBufferContext::blitFrameBufferToFrameBuffer(const cv::Rect& srcViewport,
        const cv::Size& targetFbSize, bool stretch, bool flipY) {
	double hf = double(targetFbSize.height) / framebufferSize_.height;
    double wf = double(targetFbSize.width) / framebufferSize_.width;
    double f;
    if (hf > wf)
        f = wf;
    else
        f = hf;

    double fbws = framebufferSize_.width * f;
    double fbhs = framebufferSize_.height * f;

//    double diffw = (targetFbSize.width - srcViewport.width) / 2.0;
//    double diffh = (targetFbSize.height - srcViewport.height) / 2.0;

//    double marginw = (targetFbSize.width - framebufferSize_.width) / 2.0;
//    double marginh = (targetFbSize.height - framebufferSize_.height) / 2.0;
    double marginws = (targetFbSize.width - fbws) / 2.0;
    double marginhs = (targetFbSize.height - fbhs) / 2.0;

    GLint srcX0 = 0;
    GLint srcY0 = 0;
    GLint srcX1 = srcViewport.width;
    GLint srcY1 = srcViewport.height;
    GLint dstX0 = stretch ? marginws : srcViewport.x;
    GLint dstY0 = stretch ? marginhs : srcViewport.y;
    GLint dstX1 = stretch ? marginws + fbws : srcViewport.x + srcViewport.width;
    GLint dstY1 = stretch ? marginhs + fbhs : srcViewport.y + srcViewport.height;
    if(flipY) {
        GLint tmp = dstY0;
        dstY0 = dstY1;
        dstY1 = tmp;
    }

    GL_CHECK(glBlitFramebuffer( srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            GL_COLOR_BUFFER_BIT, GL_NEAREST));

//    std::cerr << "BLIT: " << srcX0 <<  " " << srcY0 <<  " " << srcX1 <<  " " << srcY1 <<
//            " " << dstX0 <<  " " << dstY0 <<  " " << dstX1 <<  " " << dstY1 << std::endl;

}

cv::UMat& FrameBufferContext::fb() {
	return view_;
}

void FrameBufferContext::begin(GLenum framebufferTarget, GLuint framebufferID) {
		glBindFramebuffer(framebufferTarget, framebufferID);
		assert(glCheckFramebufferStatus(framebufferTarget) == GL_FRAMEBUFFER_COMPLETE);
}

void FrameBufferContext::end(bool copyBack) {
	if(copyBack) {
		if(copyFramebuffers_.find(currentFBO_) == copyFramebuffers_.end()) {
			initBlend(currentFBO_);
	    }
		GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
		GLint dims[4] = {0};
		glGetIntegerv(GL_VIEWPORT, dims);
		GLint fbX = dims[0];
		GLint fbY = dims[1];
		GLint fbWidth = dims[2];
		GLint fbHeight = dims[3];
		GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFramebuffers_[currentFBO_]));
	    assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	    cv::Rect vp = V4D::instance()->get<cv::Rect>(V4D::Keys::VIEWPORT);
		blitFrameBufferToFrameBuffer(cv::Rect(fbX, fbY, fbWidth, fbHeight), vp.size(), false, false);
		blendFramebuffer(currentFBO_);
	}
}

void FrameBufferContext::download(cv::UMat& m) {
    cv::Mat tmp = m.getMat(cv::ACCESS_WRITE);
    assert(tmp.data != nullptr);
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureID_));
    GL_CHECK(
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID_, 0));
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    GL_CHECK(glReadPixels(0, 0, tmp.cols, tmp.rows, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data));
    tmp.release();
}

void FrameBufferContext::upload(const cv::UMat& m) {
	cv::Mat tmp = m.getMat(cv::ACCESS_READ);
	assert(!tmp.empty());
    assert(tmp.data != nullptr);

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureID_));
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID_, 0));
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    GL_CHECK(
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tmp.cols, tmp.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data));

    tmp.release();
}

void FrameBufferContext::acquireFromGL(cv::UMat& m) {
#ifdef HAVE_OPENCL
	if (cv::ocl::useOpenCL() && clglSharing_) {
        try {
            flip();
            GL_CHECK(fromGLTexture2D(textureFlippedID_, m));
            return;
        } catch(std::exception& ex) {
                    CV_LOG_WARNING(nullptr, "CL-GL failed to acquire: " + std::string(ex.what()));
                    clglSharing_ = false;
        } catch(...) {
        	CV_LOG_WARNING(nullptr, "CL-GL failed to acquire.");
            clglSharing_ = false;
        }
	} else
#endif
    {

        download(m);
        cv::flip(m, m, 0);
    }
}

void FrameBufferContext::releaseToGL(cv::UMat& m) {

#ifdef HAVE_OPENCL
    if (cv::ocl::useOpenCL() && clglSharing_) {
        try
        {
        	GL_CHECK(toGLTexture2D(m, textureFlippedID_));
        	unflip();
        	return;
        } catch(...) {
        	CV_LOG_WARNING(nullptr, "CL-GL failed to release.");
            clglSharing_ = false;
        }
    }
#endif
    {
        cv::flip(m, m, 0);
    	upload(m);
    }
}

cv::Vec2f FrameBufferContext::position() {
    int x, y;
    glfwGetWindowPos(getGLFWWindow(), &x, &y);
    return cv::Vec2f(x, y);
}

float FrameBufferContext::pixelRatioX() {
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);

    return xscale;
}

float FrameBufferContext::pixelRatioY() {
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);

    return yscale;
}

void FrameBufferContext::makeCurrent() {
	//found a race condition in libglx-nvidia
	static std::mutex mtx;
	std::lock_guard guard(mtx);
	glfwMakeContextCurrent(getGLFWWindow());
}

void FrameBufferContext::makeNoneCurrent() {
}


bool FrameBufferContext::isResizable() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_RESIZABLE) == GLFW_TRUE;
}

void FrameBufferContext::setResizable(bool r) {
    glfwSetWindowAttrib(getGLFWWindow(), GLFW_RESIZABLE, r ? GLFW_TRUE : GLFW_FALSE);
}

void FrameBufferContext::setWindowSize(const cv::Size& sz) {
	std::cerr << "SZ: " << sz << std::endl;
    glfwSetWindowSize(getGLFWWindow(), sz.width, sz.height);
}

cv::Size FrameBufferContext::getWindowSize() {
	cv::Size sz;
    glfwGetWindowSize(getGLFWWindow(), &sz.width, &sz.height);
    return sz;
}

bool FrameBufferContext::isFullscreen() {
    return glfwGetWindowMonitor(getGLFWWindow()) != nullptr;
}

void FrameBufferContext::setFullscreen(bool f) {
	std::cerr << "FULL: " << f << std::endl;
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (f) {
    	glfwSetWindowMonitor(getGLFWWindow(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
//    	setWindowSize(cv::Size(mode->width, mode->height));
        setWindowSize(size());
    } else {
        glfwSetWindowMonitor(getGLFWWindow(), nullptr, 0, 0, size().width,
                size().height, 0);
        setWindowSize(size());
    }
}

cv::Size FrameBufferContext::getNativeFrameBufferSize() {
    int w, h;
    glfwGetFramebufferSize(getGLFWWindow(), &w, &h);
    return cv::Size{w, h};
}

//cache window visibility instead of performing a heavy window attrib query.
bool FrameBufferContext::isVisible() {
    return isVisible_;
}

void FrameBufferContext::setVisible(bool v) {
    isVisible_ = v;
    if (isVisible_)
        glfwShowWindow(getGLFWWindow());
    else
        glfwHideWindow(getGLFWWindow());
}

bool FrameBufferContext::isClosed() {
    return glfwWindow_ == nullptr;
}

void FrameBufferContext::close() {
    teardown();
    glfwDestroyWindow(getGLFWWindow());
    glfwWindow_ = nullptr;
}

bool FrameBufferContext::isRoot() {
    return isRoot_;
}


bool FrameBufferContext::hasParent() {
    return parent_;
}

}
}
}
