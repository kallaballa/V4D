#include "viz2d.hpp"
#include "detail/clglcontext.hpp"
#include "detail/clvacontext.hpp"
#include "detail/nanovgcontext.hpp"

namespace kb {
namespace viz2d {
namespace detail {
void gl_check_error(const std::filesystem::path &file, unsigned int line, const char *expression) {
    int errorCode = glGetError();

    if (errorCode != 0) {
        std::cerr << "GL failed in " << file.filename() << " (" << line << ") : " << "\nExpression:\n   " << expression << "\nError code:\n   " << errorCode << "\n   " << std::endl;
        assert(false);
    }
}

void error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error: %s\n", description);
}
}

cv::Scalar color_convert(const cv::Scalar& src, cv::ColorConversionCodes code) {
    cv::Mat tmpIn(1,1,CV_8UC3);
    cv::Mat tmpOut(1,1,CV_8UC3);

    tmpIn.at<cv::Vec3b>(0,0) = cv::Vec3b(src[0], src[1], src[2]);
    cvtColor(tmpIn, tmpOut, code);
    const cv::Vec3b& vdst = tmpOut.at<cv::Vec3b>(0,0);
    cv::Scalar dst(vdst[0],vdst[1],vdst[2], src[3]);
    return dst;
}
std::function<bool(Viz2DWindow*, Viz2DWindow*)> Viz2DWindow::viz2DWin_Xcomparator([](Viz2DWindow* lhs, Viz2DWindow* rhs){ return lhs->position()[0] < rhs->position()[0]; });
std::set<Viz2DWindow*, decltype(Viz2DWindow::viz2DWin_Xcomparator)> Viz2DWindow::all_windows_xsorted_(viz2DWin_Xcomparator);

Viz2DWindow::Viz2DWindow(nanogui::Screen *screen, int x, int y, const string &title) :
        Window(screen, title), screen_(screen), lastDragPos_(x, y) {
    all_windows_xsorted_.insert(this);
    oldLayout_ = new nanogui::AdvancedGridLayout( { 10, 0, 10, 0 }, { });
    oldLayout_->set_margin(10);
    oldLayout_->set_col_stretch(2, 1);
    this->set_position( { x, y });
    this->set_layout(oldLayout_);
    this->set_visible(true);

    minBtn_ = this->button_panel()->add<nanogui::Button>("_");
    maxBtn_ = this->button_panel()->add<nanogui::Button>("+");
    newLayout_ = new nanogui::AdvancedGridLayout( { 10, 0, 10, 0 }, { });

    maxBtn_->set_visible(false);

    maxBtn_->set_callback([&, this]() {
        this->minBtn_->set_visible(true);
        this->maxBtn_->set_visible(false);

        for (auto *child : this->children()) {
            child->set_visible(true);
        }

        this->set_layout(oldLayout_);
        this->set_position(maximizedPos_);
        this->screen_->perform_layout();
        this->minimized_ = false;
    });

    minBtn_->set_callback([&, this]() {
        this->minBtn_->set_visible(false);
        this->maxBtn_->set_visible(true);

        for (auto *child : this->children()) {
            child->set_visible(false);
        }
        this->set_size( { 0, 0 });
        this->set_layout(newLayout_);
        this->screen_->perform_layout();
        int gap = 0;
        int x = 0;
        int w = width();
        int lastX = 0;
        this->maximizedPos_ = this->position();

        for (Viz2DWindow* win : all_windows_xsorted_) {
            if(win != this && win->isMinimized()) {
                x = win->position()[0];
                gap = lastX + x;
                if(gap >= w) {
                    this->set_position({lastX, screen_->height() - this->height()});
                    break;
                }
                lastX = x + win->width();
            }
        }
        if(gap < w) {
            this->set_position({lastX, screen_->height() - this->height()});
        }
        this->minimized_ = true;
    });
}

Viz2DWindow::~Viz2DWindow() {
    all_windows_xsorted_.erase(this);
}

bool Viz2DWindow::isMinimized() {
    return minimized_;
}

bool Viz2DWindow::mouse_drag_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int mods) {
    if (m_drag && (button & (1 << GLFW_MOUSE_BUTTON_1)) != 0) {
        if(maxBtn_->visible()) {
            for (auto *win : all_windows_xsorted_) {
                if (win != this) {
                    if (win->contains(this->position())
                            || win->contains( { this->position()[0] + this->size()[0], this->position()[1] + this->size()[1] })
                            || win->contains( { this->position()[0], this->position()[1] + this->size()[1] })
                            || win->contains( { this->position()[0] + this->size()[0], this->position()[1] })
                            || this->contains(win->position())
                            || this->contains( { win->position()[0] + win->size()[0], win->position()[1] + win->size()[1] })
                            || this->contains( { win->position()[0], win->position()[1] + win->size()[1] })
                            || this->contains( { win->position()[0] + win->size()[0], win->position()[1] })) {
                        this->set_position(lastDragPos_);
                        return true;
                    }
                }
            }
        }
        lastDragPos_ = m_pos;
        bool result = nanogui::Window::mouse_drag_event(p, rel, button, mods);

        return result;
    }
    return false;
}

Viz2D::Viz2D(const cv::Size &size, const cv::Size& frameBufferSize, bool offscreen, const string &title, int major, int minor, int samples, bool debug) :
        initialSize_(size), frameBufferSize_(frameBufferSize), offscreen_(offscreen), title_(title), major_(major), minor_(minor), samples_(samples), debug_(debug) {
    assert(frameBufferSize_.width >= initialSize_.width && frameBufferSize_.height >= initialSize_.height);
    initialize();
}

Viz2D::~Viz2D() {
    //don't delete form_. it is autmatically cleaned up by the base class (nanogui::Screen)
    if (writer_)
        delete writer_;
    if (capture_)
        delete capture_;
    if (nvgContext_)
        delete nvgContext_;
    if (clvaContext_)
        delete clvaContext_;
    if (clglContext_)
        delete clglContext_;
}

void Viz2D::initialize() {
    assert(glfwInit() == GLFW_TRUE);
    glfwSetErrorCallback(kb::viz2d::error_callback);

    if (debug_)
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    if (offscreen_)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfwSetTime(0);

#ifdef __APPLE__
        glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_SAMPLES, samples_);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    /* I figure we don't need double buffering because the texture is our backbuffer
     * But EGL/X11 anyway doesn't support rendering to the front buffer, yet. But on wayland it should work.
     * And I am not sure about vsync on other platforms.
     */
    //    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);

    glfwWindow_ = glfwCreateWindow(initialSize_.width, initialSize_.height, title_.c_str(), nullptr, nullptr);
    if (glfwWindow_ == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(getGLFWWindow());

    screen().initialize(getGLFWWindow(), false);
    form_ = new nanogui::FormHelper(this);
    this->setWindowSize(initialSize_);

    glfwSetWindowUserPointer(getGLFWWindow(), this);

    glfwSetCursorPosCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().cursor_pos_callback_event(x, y);
    }
    );
    glfwSetMouseButtonCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int button, int action, int modifiers) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().mouse_button_callback_event(button, action, modifiers);
    }
    );
    glfwSetKeyCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int key, int scancode, int action, int mods) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().key_callback_event(key, scancode, action, mods);
    }
    );
    glfwSetCharCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, unsigned int codepoint) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().char_callback_event(codepoint);
    }
    );
    glfwSetDropCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int count, const char **filenames) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().drop_callback_event(count, filenames);
    }
    );
    glfwSetScrollCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().scroll_callback_event(x, y);
    }
    );

//FIXME resize internal buffers?
//    glfwSetWindowContentScaleCallback(getGLFWWindow(),
//        [](GLFWwindow* glfwWin, float xscale, float yscale) {
//        }
//    );

    glfwSetFramebufferSizeCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int width, int height) {
        Viz2D* v2d = reinterpret_cast<Viz2D*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().resize_callback_event(width, height);
    }
    );

    clglContext_ = new detail::CLGLContext(this->getFrameBufferSize());
    clvaContext_ = new detail::CLVAContext(*clglContext_);
    nvgContext_ = new detail::NanoVGContext(*this, getNVGcontext(), *clglContext_);
}

cv::ogl::Texture2D& Viz2D::texture() {
    return clglContext_->getTexture2D();
}

nanogui::FormHelper* Viz2D::form() {
    return form_;
}

bool Viz2D::keyboard_event(int key, int scancode, int action, int modifiers) {
    if (nanogui::Screen::keyboard_event(key, scancode, action, modifiers))
        return true;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        setOffscreen(!isOffscreen());
        return true;
    } else if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        auto children = nanogui::Screen::children();
        for(auto* child : children) {
            child->set_visible(!child->visible());
        }

        return true;
    }
    return false;
}

CLGLContext& Viz2D::clgl() {
    return *clglContext_;
}

CLVAContext& Viz2D::clva() {
    return *clvaContext_;
}

NanoVGContext& Viz2D::nvg() {
    return *nvgContext_;
}

nanogui::Screen& Viz2D::screen() {
    return *dynamic_cast<nanogui::Screen*>(this);
}

cv::Size Viz2D::getVideoFrameSize() {
    return clva().getVideoFrameSize();
}

void Viz2D::setVideoFrameSize(const cv::Size& sz) {
    clva().setVideoFrameSize(sz);
}

void Viz2D::opengl(std::function<void(const cv::Size&)> fn) {
    detail::CLExecScope_t scope(clglContext_->getCLExecContext());
    detail::CLGLContext::GLScope glScope(*clglContext_);
    fn(getFrameBufferSize());
}

void Viz2D::opencl(std::function<void(cv::UMat&)> fn) {
    clgl().opencl(fn);
}

void Viz2D::nanovg(std::function<void(const cv::Size&)> fn) {
    nvg().render(fn);
}

bool Viz2D::capture() {
    return clva().capture([=, this](cv::UMat &videoFrame) {
        *(this->capture_) >> videoFrame;
    });
}

void Viz2D::write() {
    clva().write([=, this](const cv::UMat &videoFrame) {
        *(this->writer_) << videoFrame;
    });
}

void Viz2D::makeGLFWContextCurrent() {
    glfwMakeContextCurrent(getGLFWWindow());
}

cv::VideoWriter& Viz2D::makeVAWriter(const string &outputFilename, const int fourcc, const float fps, const cv::Size &frameSize, const int vaDeviceIndex) {
    writerPath_ = outputFilename;
    vaWriterDeviceIndex_ = vaDeviceIndex;

    writer_ = new cv::VideoWriter(outputFilename, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), fps, frameSize, { cv::VIDEOWRITER_PROP_HW_DEVICE, vaDeviceIndex, cv::VIDEOWRITER_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::VIDEOWRITER_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    setVideoFrameSize(frameSize);

    if (!clva().hasContext()) {
        clva().copyContext();
    }
    return *writer_;
}

cv::VideoCapture& Viz2D::makeVACapture(const string &inputFilename, const int vaDeviceIndex) {
    capturePath_ = inputFilename;
    vaCaptureDeviceIndex_ = vaDeviceIndex;
    capture_ = new cv::VideoCapture(inputFilename, cv::CAP_FFMPEG, { cv::CAP_PROP_HW_DEVICE, vaDeviceIndex, cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::CAP_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    float w = capture_->get(cv::CAP_PROP_FRAME_WIDTH);
    float h = capture_->get(cv::CAP_PROP_FRAME_HEIGHT);
    setVideoFrameSize(cv::Size(w,h));

    if (!clva().hasContext()) {
        clva().copyContext();
    }

    return *capture_;
}

cv::VideoWriter& Viz2D::makeWriter(const string &outputFilename, const int fourcc, const float fps, const cv::Size &frameSize) {
    writerPath_ = outputFilename;
    writer_ = new cv::VideoWriter(outputFilename, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), fps, frameSize, {});
    setVideoFrameSize(frameSize);

    return *writer_;
}

cv::VideoCapture& Viz2D::makeCapture(const string &inputFilename) {
    capturePath_ = inputFilename;
    capture_ = new cv::VideoCapture(inputFilename, cv::CAP_FFMPEG, {});
    float w = capture_->get(cv::CAP_PROP_FRAME_WIDTH);
    float h = capture_->get(cv::CAP_PROP_FRAME_HEIGHT);
    setVideoFrameSize(cv::Size(w,h));


    return *capture_;
}

void Viz2D::clear(const cv::Scalar &rgba) {
    const float &r = rgba[0] / 255.0f;
    const float &g = rgba[1] / 255.0f;
    const float &b = rgba[2] / 255.0f;
    const float &a = rgba[3] / 255.0f;
    GL_CHECK(glClearColor(r, g, b, a));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
}

cv::Size Viz2D::getNativeFrameBufferSize() {
    int w, h;
    glfwGetFramebufferSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

cv::Size Viz2D::getFrameBufferSize() {
    return frameBufferSize_;
}

cv::Size Viz2D::getWindowSize() {
    int w, h;
    glfwGetWindowSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

cv::Size Viz2D::getInitialSize() {
    return initialSize_;
}

float Viz2D::getXPixelRatio() {
#if defined(EMSCRIPTEN)
        return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return xscale;
#endif
}

float Viz2D::getYPixelRatio() {
#if defined(EMSCRIPTEN)
        return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return yscale;
#endif
}

void Viz2D::setWindowSize(const cv::Size &sz) {
    set_size(nanogui::Vector2i(sz.width / getXPixelRatio(), sz.height / getYPixelRatio()));
}

bool Viz2D::isFullscreen() {
    return glfwGetWindowMonitor(getGLFWWindow()) != nullptr;
}

void Viz2D::setFullscreen(bool f) {
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (f) {
        glfwSetWindowMonitor(getGLFWWindow(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        setWindowSize(getNativeFrameBufferSize());
    } else {
        glfwSetWindowMonitor(getGLFWWindow(), nullptr, 0, 0, getInitialSize().width, getInitialSize().height, 0);
        setWindowSize(getInitialSize());
    }
}

bool Viz2D::isResizable() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_RESIZABLE) == GLFW_TRUE;
}

void Viz2D::setResizable(bool r) {
    glfwWindowHint(GLFW_RESIZABLE, r ? GLFW_TRUE : GLFW_FALSE);
}

bool Viz2D::isVisible() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_VISIBLE) == GLFW_TRUE;
}

void Viz2D::setVisible(bool v) {
    glfwWindowHint(GLFW_VISIBLE, v ? GLFW_TRUE : GLFW_FALSE);
    screen().set_visible(v);
//    setSize(size_);
    screen().perform_layout();
}

bool Viz2D::isOffscreen() {
    return offscreen_;
}

void Viz2D::setOffscreen(bool o) {
    offscreen_ = o;
    setVisible(!o);
}

nanogui::Window* Viz2D::makeWindow(int x, int y, const string &title) {
    return form()->add_window(nanogui::Vector2i(x, y), title);
}

nanogui::Label* Viz2D::makeGroup(const string &label) {
    return form()->add_group(label);
}

nanogui::detail::FormWidget<bool>* Viz2D::makeFormVariable(const string &name, bool &v, const string &tooltip) {
    auto var = form()->add_variable(name, v);
    if (!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

void Viz2D::setAccelerated(bool a) {
    if(a != cv::ocl::useOpenCL()) {
        clglContext_->getCLExecContext().setUseOpenCL(a);
        clvaContext_->getCLExecContext().setUseOpenCL(a);
        cv::ocl::setUseOpenCL(a);
        double w = 0;
        double h = 0;
        double fps = 0;
        double fourcc = 0;

        if(writer_) {
            double w = writer_->get(cv::CAP_PROP_FRAME_WIDTH);
            double h = writer_->get(cv::CAP_PROP_FRAME_HEIGHT);
            double fps = writer_->get(cv::CAP_PROP_FPS);
            double fourcc = writer_->get(cv::CAP_PROP_FOURCC);
        }

        if(a) {
            if(capture_) {
                delete capture_;
                makeVACapture(capturePath_, vaCaptureDeviceIndex_);
            }

            if(writer_) {
                delete writer_;
                makeVAWriter(writerPath_, fourcc, fps, cv::Size(w, h), vaWriterDeviceIndex_);
            }
        } else {
            if(capture_) {
                delete capture_;
                makeCapture(capturePath_);
            }
            if(writer_) {
                delete writer_;
                makeWriter(writerPath_, fourcc, fps, cv::Size(w, h));
            }
        }
    }
}

bool Viz2D::display() {
    bool result = true;
    if (!offscreen_) {
        glfwPollEvents();
        screen().draw_contents();
        clglContext_->blitFrameBufferToScreen(getWindowSize());
        screen().draw_widgets();
        glfwSwapBuffers(glfwWindow_);
        result = !glfwWindowShouldClose(glfwWindow_);
    }

    return result;
}

bool Viz2D::isClosed() {
    return closed_;

}
void Viz2D::close() {
    setVisible(false);
    closed_ = true;
}

GLFWwindow* Viz2D::getGLFWWindow() {
    return glfwWindow_;
}

NVGcontext* Viz2D::getNVGcontext() {
    return screen().nvg_context();
}
}
}
