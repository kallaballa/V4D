// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#ifndef OPENCV_PLAN_MOCK_RUNTIME_HPP_
#define OPENCV_PLAN_MOCK_RUNTIME_HPP_

#include "opencv2/plan/plan.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

namespace cv {
namespace plan {
namespace test {

class MockPlanRuntime : public PlanRuntime {
    cv::Ptr<detail::PlainContext> plainCtx_;
    mutable std::mutex mtx_;
    int framesLeft_;
    bool guiCalled_ = false;
    // Frame-loop-only timing: every thread shares this runtime instance, so the
    // accumulators below sum the per-thread frame times across all frame loops.
    // Only the frameFn() invocations inside runFrameLoop() are timed - spawn,
    // per-thread infer/makeGraph, barrier, and join are excluded.
    std::atomic<long long> frameNs_ {0};
    std::atomic<long long> framesRun_ {0};

public:
    explicit MockPlanRuntime(int frameCount = 3) : framesLeft_(frameCount) {}

    cv::Ptr<detail::PlainContext> plainCtx() override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!plainCtx_) plainCtx_ = cv::makePtr<detail::PlainContext>();
        return plainCtx_;
    }

    cv::Ptr<detail::PlanContext> glCtx(int) override { return nullptr; }
    cv::Ptr<detail::PlanContext> fbCtx() override { return nullptr; }
    cv::Ptr<detail::PlanContext> nvgCtx() override { return nullptr; }
    cv::Ptr<detail::PlanContext> bgfxCtx() override { return nullptr; }
    cv::Ptr<detail::PlanContext> extCtx(int) override { return nullptr; }
    cv::Ptr<detail::PlanContext> sourceCtx() override { return nullptr; }
    cv::Ptr<detail::PlanContext> sinkCtx() override { return nullptr; }
    cv::Ptr<detail::PlanContext> imguiCtx() override { return nullptr; }

    bool hasPlainCtx() override { return true; }
    bool hasGlCtx(uint32_t) override { return false; }
    bool hasFbCtx() override { return false; }
    bool hasNvgCtx() override { return false; }
    bool hasBgfxCtx() override { return false; }
    bool hasExtCtx(uint32_t) override { return false; }
    bool hasSourceCtx() override { return false; }
    bool hasSinkCtx() override { return false; }
    bool hasImguiCtx() override { return false; }

    uint32_t debugFlags() const override { return 0; }
    cv::Rect getViewport() const override { return cv::Rect(0, 0, 640, 480); }

    void initWorkerThread(int) override {}
    void willGui(const cv::Ptr<Plan>&) override { guiCalled_ = true; }

    void runFrameLoop(std::function<void()> frameFn) override {
        for (int i = 0; i < framesLeft_; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            frameFn();
            const auto t1 = std::chrono::steady_clock::now();
            frameNs_ += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++framesRun_;
        }
    }

    void releaseIo() override {}

    void resetFrames() {
        frameNs_ = 0;
        framesRun_ = 0;
    }

    long long frameNs() const { return frameNs_.load(); }
    long long framesRun() const { return framesRun_.load(); }

    bool guiCalled() const { return guiCalled_; }
};

} // namespace test
} // namespace plan
} // namespace cv

#endif
