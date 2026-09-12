#include <opencv2/v4d/v4d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <cstdlib>
#include <cstring>

using namespace cv;
using namespace cv::v4d;
using namespace cv::v4d::event;

class ImshowReimplementation : public V4DPlan {
    UMat image_;
    UMat bgra_;
    UMat rgba_;
    string filename_;
    Property<cv::Size> size_ = P<cv::Size>(V4D::Keys::SIZE);

    // Shared between the rendering pipeline and the ImGui menu/help thread.
    struct State {
        // Image
        int   imageHandle_  = -1;
        int   imageWidth_   = 0;
        int   imageHeight_  = 0;
        int   channels_     = 0;

        // View transform
        float zoom_         = 1.0f;
        cv::Point2f pan_    = {0.0f, 0.0f};
        bool  isDragging_   = false;

        // Cursor tracking
        cv::Point2f mousePos_ = {-1.0f, -1.0f};
        bool  mouseInside_  = false;

        // UI toggles
        bool  showProperties_ = false;
        bool  showHelp_       = true;
        bool  showStatusBar_  = true;

        // Deep zoom threshold (mirrors OpenCV's QT imshow behaviour)
        static constexpr float kDeepZoomThreshold = 30.0f;

        // Save dialog state
        bool  showSaveDialog_    = false;
        bool  showSaveViewDialog_= false;
        bool  lastSaveOk_        = true;
        std::string lastSaveMsg_;
        char  saveBuf_[1024]     = {};
        int   saveFormat_        = 0; // 0=PNG, 1=JPG, 2=BMP

        // Cached filename for the status bar (avoids capturing 'this').
        std::string filenameCopy_;
    };
    static State state_;

    Event<Mouse> scroll_      = E<Mouse>(Mouse::SCROLL);
    Event<Mouse> drag_        = E<Mouse>(Mouse::DRAG);
    Event<Mouse> pressLeft_   = E<Mouse>(Mouse::PRESS,   Mouse::LEFT);
    Event<Mouse> releaseLeft_ = E<Mouse>(Mouse::RELEASE, Mouse::LEFT);
    Event<Mouse> pressRight_  = E<Mouse>(Mouse::PRESS,   Mouse::RIGHT);
    Event<Mouse> pressMiddle_ = E<Mouse>(Mouse::PRESS,   Mouse::MIDDLE);
    Event<Mouse> move_        = E<Mouse>(Mouse::MOVE);
    Event<Mouse> hoverEnter_  = E<Mouse>(Mouse::HOVER_ENTER);
    Event<Mouse> hoverExit_   = E<Mouse>(Mouse::HOVER_EXIT);

public:
    ImshowReimplementation(const string& filename) : filename_(filename) {
        cv::Mat tmp = imread(filename, IMREAD_UNCHANGED);
        if (tmp.empty()) {
            CV_Error(Error::StsError,
                "Could not load image '" + filename + "'. "
                "Run with: example_v4d_imshow_reimplementation <image>");
        }
        tmp.copyTo(image_);
        state_.filenameCopy_ = filename;
    }

    void setup() override {
        set(GlobalState::Keys::TIME_TRACKER, V(false));
        set(GlobalState::Keys::SHOW_FRAME_TIME, V(false));

        plain([](const UMat& src, UMat& bgra, UMat& rgba) {
            if (src.channels() == 1) {
                cvtColor(src, rgba, COLOR_GRAY2RGBA);
            } else if (src.channels() == 3) {
                cvtColor(src, rgba, COLOR_BGR2RGBA);
            } else if (src.channels() == 4) {
                cvtColor(src, rgba, COLOR_BGRA2RGBA);
            } else {
                CV_Error(Error::StsError, "Unsupported image format");
            }
            // Keep an RGBA copy for the on-screen upload (nanovg's
            // createImageRGBA expects channel order R,G,B,A) and a BGRA
            // copy for the status-bar / deep-zoom pixel readout.
            cvtColor(rgba, bgra, COLOR_RGBA2BGRA);
        }, R(image_), RW(bgra_), RW(rgba_));

        nvg([](UMat& rgba, const UMat& image, const UMat& bgra, State& state, const cv::Size& sz) {
            (void)bgra;
            using namespace cv::v4d::nvg;
            CV_Assert(!rgba.empty());
            state.imageWidth_  = rgba.cols;
            state.imageHeight_ = rgba.rows;
            // Preserve the *original* channel count (1, 3 or 4) for the
            // status bar and deep-zoom overlay, not the 4 we always have
            // in rgba_/bgra_.
            state.channels_    = image.channels();
            state.imageHandle_ = createImageRGBA(rgba.cols, rgba.rows,
                                                 NVG_IMAGE_NEAREST,
                                                 rgba.getMat(cv::ACCESS_READ).data);
            CV_Assert(state.imageHandle_ > 0);
            state.zoom_ = 1.0f;
            // Centered, 1:1 by default (matches QT imshow on first open).
            state.pan_.x = (sz.width  - rgba.cols) / 2.0f;
            state.pan_.y = (sz.height - rgba.rows) / 2.0f;
        }, RW(rgba_), R(image_), R(bgra_), RWS(state_), size_);
    }

    void infer() override {
        set(V4D::Keys::CLEAR_COLOR, V(cv::Scalar(30, 30, 30, 255)));
        clear();

        // -- Input handling --------------------------------------------------
        plain([](const Mouse::List& scrollEvents,
                 const Mouse::List& dragEvents,
                 const Mouse::List& pressLeftEvents,
                 const Mouse::List& releaseLeftEvents,
                 const Mouse::List& pressRightEvents,
                 const Mouse::List& pressMiddleEvents,
                 const Mouse::List& moveEvents,
                 const Mouse::List& hoverEnterEvents,
                 const Mouse::List& hoverExitEvents,
                 const cv::Size& sz,
                 State& state) {

            // Scroll wheel: zoom around the cursor.
            for (auto se : scrollEvents) {
                float zoomFactor = (se->data().y > 0) ? 1.1f : 1.0f / 1.1f;
                float worldX = (se->position().x - state.pan_.x) / state.zoom_;
                float worldY = (se->position().y - state.pan_.y) / state.zoom_;
                state.zoom_ *= zoomFactor;
                state.zoom_ = std::clamp(state.zoom_, 0.01f, 1000.0f);
                state.pan_.x = se->position().x - worldX * state.zoom_;
                state.pan_.y = se->position().y - worldY * state.zoom_;
            }
            // Left-drag: pan.
            for (auto de : dragEvents) {
                if (state.isDragging_) {
                    state.pan_.x += de->data().x;
                    state.pan_.y += de->data().y;
                }
            }
            for (auto& _ : pressLeftEvents)   state.isDragging_ = true;
            for (auto& _ : releaseLeftEvents) state.isDragging_ = false;

            // Right-click: reset zoom (1:1, centered).
            for (auto& _ : pressRightEvents) {
                state.zoom_ = 1.0f;
                state.pan_.x = (sz.width  - state.imageWidth_)  / 2.0f;
                state.pan_.y = (sz.height - state.imageHeight_) / 2.0f;
            }
            // Middle-click: jump to deep zoom at the cursor position
            // (mirrors QT's "Zoom to region" behavior).
            for (auto me : pressMiddleEvents) {
                float target = State::kDeepZoomThreshold;
                float factor = (target / state.zoom_) - 1.0f;
                if (factor != 0.0f) {
                    float cx = me->position().x;
                    float cy = me->position().y;
                    float worldX = (cx - state.pan_.x) / state.zoom_;
                    float worldY = (cy - state.pan_.y) / state.zoom_;
                    state.zoom_ *= (1.0f + factor);
                    state.zoom_ = std::clamp(state.zoom_, 0.01f, 1000.0f);
                    state.pan_.x = cx - worldX * state.zoom_;
                    state.pan_.y = cy - worldY * state.zoom_;
                }
            }

            // Cursor position (for the status bar / pixel readout).
            if (!moveEvents.empty()) {
                state.mousePos_   = moveEvents.back()->position();
                state.mouseInside_ = true;
            }
            for (auto& _ : hoverEnterEvents) state.mouseInside_ = true;
            for (auto& _ : hoverExitEvents) {
                state.mouseInside_ = false;
                state.mousePos_   = {-1.0f, -1.0f};
            }
        }, scroll_, drag_, pressLeft_, releaseLeft_, pressRight_, pressMiddle_,
           move_, hoverEnter_, hoverExit_, size_, RWS(state_));

        // -- Render the canvas ----------------------------------------------
        nvg([](const UMat& bgra, const State& state, const cv::Size& sz) {
            using namespace cv::v4d::nvg;
            // ----- Image -----
            save();
            translate(state.pan_.x, state.pan_.y);
            scale(state.zoom_, state.zoom_);

            beginPath();
            rect(0.0f, 0.0f,
                 static_cast<float>(state.imageWidth_),
                 static_cast<float>(state.imageHeight_));
            fillPaint(imagePattern(0.0f, 0.0f,
                                   static_cast<float>(state.imageWidth_),
                                   static_cast<float>(state.imageHeight_),
                                   0.0f, state.imageHandle_, 1.0f));
            fill();

            // Grid lines for moderate zoom (8x .. 30x). At >= 30x the
            // dedicated deep-zoom block below draws its own grid on top of
            // the RGB labels, matching the QT imshow behavior.
            if (state.zoom_ >= 8.0f && state.zoom_ < State::kDeepZoomThreshold) {
                float gridAlpha = std::min(1.0f, (state.zoom_ - 8.0f) / 8.0f);
                strokeColor(cv::Scalar(128, 128, 128,
                                       static_cast<int>(gridAlpha * 255)));
                strokeWidth(1.0f / state.zoom_);
                beginPath();
                for (int x = 0; x <= state.imageWidth_; ++x) {
                    moveTo(static_cast<float>(x), 0.0f);
                    lineTo(static_cast<float>(x),
                           static_cast<float>(state.imageHeight_));
                }
                stroke();
                beginPath();
                for (int y = 0; y <= state.imageHeight_; ++y) {
                    moveTo(0.0f, static_cast<float>(y));
                    lineTo(static_cast<float>(state.imageWidth_),
                           static_cast<float>(y));
                }
                stroke();
            }

            restore();

            // ----- Deep-zoom per-pixel RGB / grayscale overlay -----
            // Faithfully reimplements OpenCV's QT imshow drawImgRegion().
            // Activates only at >= 30x (threshold_zoom_img_region).
            // Drawn in screen space (outside the image transform) so the
            // text font size is in real screen pixels and the on-screen
            // coordinates are unambiguous.
            if (state.zoom_ >= State::kDeepZoomThreshold &&
                state.channels_ >= 1 && state.channels_ <= 4) {

                static cv::Mat pixels;
                static int pw = 0, ph = 0, pc = 0;
                if (pw != state.imageWidth_ || ph != state.imageHeight_ ||
                    pc != bgra.channels() || pixels.empty()) {
                    bgra.getMat(cv::ACCESS_READ).copyTo(pixels);
                    pw = state.imageWidth_;
                    ph = state.imageHeight_;
                    pc = bgra.channels();
                }
                CV_Assert(!pixels.empty());

                // On-screen pixel size in screen units.
                float pixelW = state.zoom_;
                float pixelH = state.zoom_;

                // Visible image-coordinate range (with 1 extra row/col on
                // the top/left to show partial pixels at the edges, matching
                // QPainter's behavior).
                int imgX0 = std::max(-1, static_cast<int>(
                    std::floor(-state.pan_.x / pixelW) - 1));
                int imgY0 = std::max(-1, static_cast<int>(
                    std::floor(-state.pan_.y / pixelH) - 1));
                int imgX1 = std::min(state.imageWidth_,
                    static_cast<int>(
                        std::ceil((sz.width  - state.pan_.x) / pixelW) + 1));
                int imgY1 = std::min(state.imageHeight_,
                    static_cast<int>(
                        std::ceil((sz.height - state.pan_.y) / pixelH) + 1));

                // Font pixel size in screen units: 10 + (pixel_height - 30)/5.
                float fs = 10.0f + (pixelH - State::kDeepZoomThreshold) / 5.0f;
                fs = std::clamp(fs, 6.0f, 48.0f);
                fontSize(fs);
                fontFace("sans-bold");
                textAlign(NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                if (state.channels_ == 3 || state.channels_ == 4) {
                    // BGR(A): three rows of colored text per pixel.
                    for (int imgY = imgY0; imgY < imgY1; ++imgY) {
                        for (int imgX = imgX0; imgX < imgX1; ++imgX) {
                            if (imgX < 0 || imgY < 0 ||
                                imgX >= state.imageWidth_ || imgY >= state.imageHeight_)
                                continue;
                            const uchar* p = pixels.ptr(imgY, imgX);
                            int b = p[0], g = p[1], r = p[2];
                            char buf[8];
                            float px = state.pan_.x + (imgX + 0.5f) * pixelW;
                            float pyR = state.pan_.y + (imgY + 1.0f/6.0f) * pixelH;
                            float pyG = state.pan_.y + (imgY + 0.5f)      * pixelH;
                            float pyB = state.pan_.y + (imgY + 5.0f/6.0f) * pixelH;
                            std::snprintf(buf, sizeof(buf), "%d", r);
                            fillColor(cv::Scalar(255, 0,   0,   255));
                            text(px, pyR, buf, buf + std::strlen(buf));
                            std::snprintf(buf, sizeof(buf), "%d", g);
                            fillColor(cv::Scalar(0,   255, 0,   255));
                            text(px, pyG, buf, buf + std::strlen(buf));
                            std::snprintf(buf, sizeof(buf), "%d", b);
                            fillColor(cv::Scalar(255, 255, 255, 255));
                            text(px, pyB, buf, buf + std::strlen(buf));
                        }
                    }
                } else if (state.channels_ == 1) {
                    // Grayscale: single value with a brightness-shifted color
                    // so it stays readable on light and dark pixels alike.
                    for (int imgY = imgY0; imgY < imgY1; ++imgY) {
                        for (int imgX = imgX0; imgX < imgX1; ++imgX) {
                            if (imgX < 0 || imgY < 0 ||
                                imgX >= state.imageWidth_ || imgY >= state.imageHeight_)
                                continue;
                            const uchar* p = pixels.ptr(imgY, imgX);
                            int v = p[0];
                            int tv = (v > 127) ? (v - 127) : (127 + v);
                            char buf[8];
                            float px = state.pan_.x + (imgX + 0.5f) * pixelW;
                            float py = state.pan_.y + (imgY + 0.5f) * pixelH;
                            std::snprintf(buf, sizeof(buf), "%d", v);
                            fillColor(cv::Scalar(tv, tv, tv, 255));
                            text(px, py, buf, buf + std::strlen(buf));
                        }
                    }
                }

                // Grid lines drawn AFTER text (matches QPainter ordering),
                // in screen units.
                strokeColor(cv::Scalar(0, 0, 0, 180));
                strokeWidth(1.0f);
                beginPath();
                for (int imgX = imgX0; imgX <= imgX1; ++imgX) {
                    float sx = state.pan_.x + imgX * pixelW;
                    moveTo(sx, state.pan_.y);
                    lineTo(sx, state.pan_.y + state.imageHeight_ * pixelH);
                }
                stroke();
                beginPath();
                for (int imgY = imgY0; imgY <= imgY1; ++imgY) {
                    float sy = state.pan_.y + imgY * pixelH;
                    moveTo(state.pan_.x, sy);
                    lineTo(state.pan_.x + state.imageWidth_ * pixelW, sy);
                }
                stroke();
            }

            // ----- Status bar at the bottom of the viewport -----
            if (state.showStatusBar_) {
                std::ostringstream oss;
                oss << state.filenameCopy_.c_str();
                if (state.mouseInside_) {
                    float invZ = 1.0f / state.zoom_;
                    int ix = static_cast<int>(std::floor((state.mousePos_.x - state.pan_.x) * invZ));
                    int iy = static_cast<int>(std::floor((state.mousePos_.y - state.pan_.y) * invZ));
                    if (ix >= 0 && iy >= 0 &&
                        ix < state.imageWidth_ && iy < state.imageHeight_) {
                        const cv::Mat& m = bgra.getMat(cv::ACCESS_READ);
                        const uchar* p = m.ptr(iy, ix);
                        oss << "   |   (x=" << ix << ", y=" << iy << ")";
                        if (state.channels_ == 1) {
                            oss << "   L:" << static_cast<int>(p[0]);
                        } else {
                            oss << "   R:" << static_cast<int>(p[2])
                                << " G:" << static_cast<int>(p[1])
                                << " B:" << static_cast<int>(p[0]);
                            if (state.channels_ == 4)
                                oss << " A:" << static_cast<int>(p[3]);
                        }
                    } else {
                        oss << "   |   (x=-, y=-)";
                    }
                } else {
                    oss << "   |   (x=-, y=-)";
                }
                oss << "   |   " << state.imageWidth_ << "x" << state.imageHeight_
                    << "   |   zoom: " << static_cast<int>(state.zoom_ * 100.0f) << "%";

                float barH = 28.0f;
                float yTop = static_cast<float>(sz.height) - barH;
                beginPath();
                rect(0.0f, yTop, static_cast<float>(sz.width), barH);
                fillColor(cv::Scalar(20, 20, 30, 230));
                fill();

                beginPath();
                rect(0.0f, yTop, static_cast<float>(sz.width), 1.0f);
                fillColor(cv::Scalar(255, 255, 255, 120));
                fill();

                fontSize(15.0f);
                fontFace("sans-bold");
                fillColor(cv::Scalar(230, 230, 230, 255));
                textAlign(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                std::string txt = oss.str();
                text(10.0f, yTop + barH * 0.5f, txt.c_str(), txt.c_str() + txt.size());
            }
        }, R(bgra_), R(state_), size_);
    }

    // ImGui menu bar, dialogs and keyboard shortcuts.
    void gui() override {
        imgui([this](State& state, const cv::Size& sz) {
            using namespace ImGui;

            ImFont* font = GetFont();
            font->Scale = 1.5;
            PushFont(font);
            // ---------- Keyboard shortcuts ----------
            // Pan with Ctrl+Arrows (~5% of the viewport, mirrors QT imshow).
            auto panFrac = [&](float dx, float dy) {
                state.pan_.x += dx * sz.width;
                state.pan_.y += dy * sz.height;
            };
            if (IsKeyDown(ImGuiKey_LeftCtrl) || IsKeyDown(ImGuiKey_RightCtrl)) {
                if (IsKeyPressed(ImGuiKey_LeftArrow))  panFrac( 0.05f, 0.0f);
                if (IsKeyPressed(ImGuiKey_RightArrow)) panFrac(-0.05f, 0.0f);
                if (IsKeyPressed(ImGuiKey_UpArrow))    panFrac(0.0f,  0.05f);
                if (IsKeyPressed(ImGuiKey_DownArrow))  panFrac(0.0f, -0.05f);
                if (IsKeyPressed(ImGuiKey_Equal) || IsKeyPressed(ImGuiKey_KeypadAdd))
                    zoomAround(state, sz, 1.5f);
                if (IsKeyPressed(ImGuiKey_Minus) || IsKeyPressed(ImGuiKey_KeypadSubtract))
                    zoomAround(state, sz, 1.0f / 1.5f);
                if (IsKeyPressed(ImGuiKey_0) || IsKeyPressed(ImGuiKey_Keypad0))
                    resetZoom(state, sz);
                if (IsKeyPressed(ImGuiKey_P)) resetZoom(state, sz);
                if (IsKeyPressed(ImGuiKey_X)) zoomRegion(state, sz);
                bool saveViewShortcut = (IsKeyDown(ImGuiKey_LeftShift) ||
                                         IsKeyDown(ImGuiKey_RightShift)) &&
                                        IsKeyPressed(ImGuiKey_S);
                if (saveViewShortcut) {
                    std::snprintf(state.saveBuf_, sizeof(state.saveBuf_),
                                  "%s", filename_.c_str());
                    state.showSaveViewDialog_ = true;
                } else if (IsKeyPressed(ImGuiKey_S)) {
                    std::snprintf(state.saveBuf_, sizeof(state.saveBuf_),
                                  "%s", filename_.c_str());
                    state.showSaveDialog_ = true;
                }
                if (IsKeyPressed(ImGuiKey_C)) {
                    // Copy the original image (not the viewport) to clipboard
                    // via xclip on Linux. Best-effort, no GUI feedback.
                    cv::Mat src = image_.getMat(cv::ACCESS_READ);
                    if (!src.empty()) {
                        std::string tmp = "/tmp/v4d_imshow_clipboard.png";
                        if (imwrite(tmp, src)) {
                            std::string cmd = "xclip -selection clipboard -t image/png < "
                                              + tmp + " >/dev/null 2>&1 &";
                            std::system(cmd.c_str());
                            state.lastSaveOk_ = true;
                            state.lastSaveMsg_ = "Copied image to clipboard.";
                        }
                    }
                }
            }

            // ESC closes dialogs / overlays.
            if (IsKeyPressed(ImGuiKey_Escape)) {
                if (state.showProperties_)         state.showProperties_         = false;
                else if (state.showSaveDialog_)    state.showSaveDialog_         = false;
                else if (state.showSaveViewDialog_)state.showSaveViewDialog_     = false;
                else if (state.showHelp_)          state.showHelp_               = false;
            }

            // ---------- Main menu bar ----------
            if (BeginMainMenuBar()) {
                if (BeginMenu("File")) {
                    if (MenuItem("Save image as...", "Ctrl+S")) {
                        std::snprintf(state.saveBuf_, sizeof(state.saveBuf_),
                                      "%s", filename_.c_str());
                        state.showSaveDialog_ = true;
                    }
                    if (MenuItem("Save view as...", "Ctrl+Shift+S")) {
                        std::snprintf(state.saveBuf_, sizeof(state.saveBuf_),
                                      "%s", filename_.c_str());
                        state.showSaveViewDialog_ = true;
                    }
                    Separator();
                    if (MenuItem("Quit", "Alt+F4")) {
                        cv::v4d::request_finish();
                    }
                    EndMenu();
                }
                if (BeginMenu("View")) {
                    if (MenuItem("Zoom x1", "Ctrl+P or 0")) {
                        resetZoom(state, sz);
                    }
                    if (MenuItem("Zoom x30 (deep zoom)", "Ctrl+X")) {
                        zoomRegion(state, sz);
                    }
                    Separator();
                    if (MenuItem("Zoom in",  "Ctrl++"))  zoomAround(state, sz, 1.5f);
                    if (MenuItem("Zoom out", "Ctrl+-"))  zoomAround(state, sz, 1.0f / 1.5f);
                    if (MenuItem("Fit to window", "Ctrl+F")) fitToWindow(state, sz);
                    Separator();
                    Checkbox("Status bar",        &state.showStatusBar_);
                    Checkbox("Show help overlay", &state.showHelp_);
                    Separator();
                    MenuItem("Properties...", nullptr, &state.showProperties_);
                    EndMenu();
                }
                if (BeginMenu("Navigate")) {
                    if (MenuItem("Pan left",  "Ctrl+Left"))  panFrac( 0.05f, 0.0f);
                    if (MenuItem("Pan right", "Ctrl+Right")) panFrac(-0.05f, 0.0f);
                    if (MenuItem("Pan up",    "Ctrl+Up"))    panFrac(0.0f,  0.05f);
                    if (MenuItem("Pan down",  "Ctrl+Down"))  panFrac(0.0f, -0.05f);
                    EndMenu();
                }
                if (BeginMenu("Help")) {
                    if (MenuItem("Show controls")) state.showHelp_ = !state.showHelp_;
                    EndMenu();
                }
                EndMainMenuBar();
            }

            // ---------- Save dialog (Save image as...) ----------
            if (state.showSaveDialog_) {
                SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
                Begin("Save image as...", &state.showSaveDialog_, ImGuiWindowFlags_AlwaysAutoResize);
                Text("Save the original image (without zoom/pan overlays).");
                InputText("Path", state.saveBuf_, sizeof(state.saveBuf_));
                const char* fmts[] = { ".png", ".jpg", ".bmp" };
                Combo("Format", &state.saveFormat_, fmts, IM_ARRAYSIZE(fmts));
                if (!state.lastSaveMsg_.empty()) {
                    if (state.lastSaveOk_) TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                                       "%s", state.lastSaveMsg_.c_str());
                    else                    TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                                       "%s", state.lastSaveMsg_.c_str());
                }
                if (Button("Save") && state.saveBuf_[0] != '\0') {
                    std::string path(state.saveBuf_);
                    // Append the chosen extension if the user did not.
                    std::string ext = fmts[std::clamp(state.saveFormat_, 0, 2)];
                    if (path.size() < ext.size() ||
                        path.compare(path.size() - ext.size(), ext.size(), ext) != 0) {
                        path += ext;
                    }
                    cv::Mat src = image_.getMat(cv::ACCESS_READ);
                    std::vector<int> params;
                    if (ext == ".jpg") {
                        params.push_back(IMWRITE_JPEG_QUALITY);
                        params.push_back(95);
                    }
                    state.lastSaveOk_ = imwrite(path, src, params);
                    state.lastSaveMsg_ = state.lastSaveOk_
                        ? ("Saved to " + path)
                        : ("Failed to save to " + path);
                }
                SameLine();
                if (Button("Cancel")) {
                    state.showSaveDialog_ = false;
                    state.lastSaveMsg_.clear();
                }
                End();
            }

            // ---------- Save view dialog (saves the rendered viewport) ----------
            if (state.showSaveViewDialog_) {
                SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
                Begin("Save view as...", &state.showSaveViewDialog_, ImGuiWindowFlags_AlwaysAutoResize);
                Text("Save the currently rendered viewport as an image.");
                InputText("Path", state.saveBuf_, sizeof(state.saveBuf_));
                const char* fmts[] = { ".png", ".jpg", ".bmp" };
                Combo("Format", &state.saveFormat_, fmts, IM_ARRAYSIZE(fmts));
                if (!state.lastSaveMsg_.empty()) {
                    if (state.lastSaveOk_) TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                                       "%s", state.lastSaveMsg_.c_str());
                    else                    TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                                       "%s", state.lastSaveMsg_.c_str());
                }
                if (Button("Save") && state.saveBuf_[0] != '\0') {
                    std::string path(state.saveBuf_);
                    std::string ext = fmts[std::clamp(state.saveFormat_, 0, 2)];
                    if (path.size() < ext.size() ||
                        path.compare(path.size() - ext.size(), ext.size(), ext) != 0) {
                        path += ext;
                    }
                    // Snapshot the displayed BGRA image (which already
                    // includes the deep-zoom overlay).
                    cv::Mat src = bgra_.getMat(cv::ACCESS_READ);
                    std::vector<int> params;
                    if (ext == ".jpg") {
                        params.push_back(IMWRITE_JPEG_QUALITY);
                        params.push_back(95);
                    }
                    state.lastSaveOk_ = imwrite(path, src, params);
                    state.lastSaveMsg_ = state.lastSaveOk_
                        ? ("Saved to " + path)
                        : ("Failed to save to " + path);
                }
                SameLine();
                if (Button("Cancel")) {
                    state.showSaveViewDialog_ = false;
                    state.lastSaveMsg_.clear();
                }
                End();
            }

            // ---------- Properties dialog ----------
            if (state.showProperties_) {
                Begin("Image properties", &state.showProperties_);
                Text("File:     %s", filename_.c_str());
                Text("Size:     %d x %d", state.imageWidth_, state.imageHeight_);
                Text("Channels: %d", state.channels_);
                const char* depth = "unknown";
                if      (state.channels_ == 1) depth = "8U (grayscale)";
                else if (state.channels_ == 3) depth = "8U (BGR)";
                else if (state.channels_ == 4) depth = "8U (BGRA)";
                Text("Depth:    %s", depth);
                Separator();
                Text("Zoom:     %.2f %%", state.zoom_ * 100.0f);
                Text("Pan:      (%.1f, %.1f)", state.pan_.x, state.pan_.y);
                Separator();
                Text("Mouse controls:");
                BulletText("Scroll: zoom in/out (around cursor)");
                BulletText("Left-drag: pan");
                BulletText("Right-click: reset zoom");
                BulletText("Middle-click: zoom to region (deep zoom)");
                Text("Keyboard:");
                BulletText("Ctrl+Arrows: pan by 5%% of viewport");
                BulletText("Ctrl+'+' / Ctrl+'-': zoom in / out");
                BulletText("Ctrl+0 / Ctrl+P: reset zoom");
                BulletText("Ctrl+F: fit to window");
                BulletText("Ctrl+X: deep zoom (x%.0f)", State::kDeepZoomThreshold);
                BulletText("Ctrl+S: save image, Ctrl+Shift+S: save view");
                BulletText("Ctrl+C: copy image to clipboard (xclip)");
                BulletText("Esc: close dialogs");
                End();
            }

            // ---------- Help overlay ----------
            // Sits below the built-in FPS display at the top-left.
            if (state.showHelp_) {
                SetNextWindowPos(ImVec2(10, 40), ImGuiCond_Once);
                SetNextWindowBgAlpha(0.55f);
                Begin("Controls (Esc to hide)", &state.showHelp_,
                      ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoTitleBar);
                Text("V4D imshow Reimplementation");
                Separator();
                BulletText("Scroll wheel:       zoom (around cursor)");
                BulletText("Left drag:          pan");
                BulletText("Right click:        reset zoom (1:1)");
                BulletText("Middle click:       deep zoom (x%.0f)", State::kDeepZoomThreshold);
                Separator();
                BulletText("Ctrl+Arrows:        pan by 5%% of viewport");
                BulletText("Ctrl+'+'/'-':       zoom in / out");
                BulletText("Ctrl+0 / Ctrl+P:    reset zoom");
                BulletText("Ctrl+F:             fit to window");
                BulletText("Ctrl+X:             deep zoom");
                BulletText("Ctrl+S:             save image as...");
                BulletText("Ctrl+Shift+S:       save view as...");
                BulletText("Ctrl+C:             copy to clipboard");
                BulletText("Esc:                close dialogs");
                Separator();
                Text("Deep zoom (%.0fx and above) overlays", State::kDeepZoomThreshold);
                Text("R / G / B values inside each pixel.");
                End();
            }
            PopFont();
        }, RWS(state_), size_);
    }

    void teardown() override {
        nvg([](State& state) {
            using namespace cv::v4d::nvg;
            if (state.imageHandle_ > 0) {
                deleteImage(state.imageHandle_);
                state.imageHandle_ = -1;
            }
        }, RW(state_));
    }

private:
    // Helpers used inside the imgui() lambda.
    static void zoomAround(State& state, const cv::Size& sz, float factor) {
        float cx = sz.width  * 0.5f;
        float cy = sz.height * 0.5f;
        float worldX = (cx - state.pan_.x) / state.zoom_;
        float worldY = (cy - state.pan_.y) / state.zoom_;
        state.zoom_ *= factor;
        state.zoom_ = std::clamp(state.zoom_, 0.01f, 1000.0f);
        state.pan_.x = cx - worldX * state.zoom_;
        state.pan_.y = cy - worldY * state.zoom_;
    }
    static void resetZoom(State& state, const cv::Size& sz) {
        state.zoom_ = 1.0f;
        state.pan_.x = (sz.width  - state.imageWidth_)  / 2.0f;
        state.pan_.y = (sz.height - state.imageHeight_) / 2.0f;
    }
    static void zoomRegion(State& state, const cv::Size& sz) {
        float target = State::kDeepZoomThreshold;
        float factor = (target / state.zoom_) - 1.0f;
        if (factor != 0.0f) zoomAround(state, sz, 1.0f + factor);
    }
    static void fitToWindow(State& state, const cv::Size& sz) {
        if (state.imageWidth_ <= 0 || state.imageHeight_ <= 0) return;
        float zx = static_cast<float>(sz.width)  / state.imageWidth_;
        float zy = static_cast<float>(sz.height) / state.imageHeight_;
        state.zoom_ = std::min(zx, zy);
        state.pan_.x = (sz.width  - state.imageWidth_  * state.zoom_) * 0.5f;
        state.pan_.y = (sz.height - state.imageHeight_ * state.zoom_) * 0.5f;
    }
};

int main(int argc, char** argv) {
    cv::Rect viewport(0, 0, 960, 960);
    std::string filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = samples::findFile("lena.jpg");
    }
    cv::Ptr<V4D> runtime = V4D::init(viewport, "V4D imshow Reimplementation",
                                     AllocateFlags::NANOVG | AllocateFlags::IMGUI, ConfigFlags::DISPLAY_MODE);
    V4DPlan::run<ImshowReimplementation>(2, std::move(filename));
    return 0;
}

ImshowReimplementation::State ImshowReimplementation::state_;
