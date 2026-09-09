// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "test_precomp.hpp"
#include "MockPlanRuntime.hpp"
#include "TestedPlan.hpp"

namespace opencv_test { namespace {

using namespace cv::plan;
using namespace cv::plan::test;

static int myAbs(int v) { return v < 0 ? -v : v; }

// ---------- Fixture ----------

class PlanRuntimeTest : public testing::Test {
protected:
    void SetUp() override {
        PlanRuntime::current() = cv::makePtr<test::MockPlanRuntime>(3);
        GlobalState::init_keys();
        LocalState::init_keys();
    }

    void TearDown() override {
        PlanRuntime::current().reset();
    }
};

// ---------- Mock runtime drives frame loop ----------

TEST_F(PlanRuntimeTest, counter_increments_each_frame) {
    struct CounterPlan : TestedPlan {
        int counter_ = 0;
        void infer() override {
            assign(RW(counter_), R(counter_) + V(1));
        }
    };
    auto plan = Plan::make<CounterPlan>();
    plan->infer();
    plan->makeGraph();

    plan->runGraph();
    EXPECT_EQ(static_cast<CounterPlan*>(plan.get())->counter_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<CounterPlan*>(plan.get())->counter_, 2);
    plan->runGraph();
    EXPECT_EQ(static_cast<CounterPlan*>(plan.get())->counter_, 3);
}

TEST_F(PlanRuntimeTest, assignment_from_computed_value) {
    struct ComputePlan : TestedPlan {
        int a_ = 10, b_ = 20, result_ = 0;
        void infer() override {
            assign(RW(result_), R(a_) + R(b_));
        }
    };
    auto plan = Plan::make<ComputePlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<ComputePlan*>(plan.get())->result_, 30);
}

TEST_F(PlanRuntimeTest, mutation_between_frames_is_observed) {
    struct MutationPlan : TestedPlan {
        int value_ = 0;
        void infer() override {
            assign(RW(value_), R(value_) + V(5));
        }
    };
    auto plan = Plan::make<MutationPlan>();
    plan->infer(); plan->makeGraph();

    plan->runGraph();
    EXPECT_EQ(static_cast<MutationPlan*>(plan.get())->value_, 5);

    static_cast<MutationPlan*>(plan.get())->value_ = 100;
    plan->runGraph();
    EXPECT_EQ(static_cast<MutationPlan*>(plan.get())->value_, 105);
}

TEST_F(PlanRuntimeTest, function_call_returns_value) {
    struct CallPlan : TestedPlan {
        int x_ = -7, abs_ = 0;
        void infer() override {
            auto q = F(&myAbs, R(x_));
            assign(RW(abs_), q);
        }
    };
    auto plan = Plan::make<CallPlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<CallPlan*>(plan.get())->abs_, 7);
}

TEST_F(PlanRuntimeTest, plain_node_executes_each_frame) {
    struct PlainNodePlan : TestedPlan {
        int tick_ = 0;
        void infer() override {
            plain([this]() { ++tick_; });
        }
    };
    auto plan = Plan::make<PlainNodePlan>();
    plan->infer(); plan->makeGraph();

    EXPECT_EQ(static_cast<PlainNodePlan*>(plan.get())->tick_, 0);
    plan->runGraph();
    EXPECT_EQ(static_cast<PlainNodePlan*>(plan.get())->tick_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<PlainNodePlan*>(plan.get())->tick_, 2);
    plan->runGraph();
    EXPECT_EQ(static_cast<PlainNodePlan*>(plan.get())->tick_, 3);
}

// ---------- RunFrameLoop drives frameFn N times ----------

TEST_F(PlanRuntimeTest, runFrameLoop_invokes_frame_fn) {
    auto rt = cv::makePtr<test::MockPlanRuntime>(5);
    int count = 0;
    rt->runFrameLoop([&]() { ++count; });
    EXPECT_EQ(count, 5);
}

// ---------- willGui hook ----------

TEST_F(PlanRuntimeTest, willGui_hook_is_called) {
    struct GuiTestPlan : TestedPlan {
        void infer() override { plain([](){}); }
    };
    auto rt = cv::makePtr<test::MockPlanRuntime>(1);
    EXPECT_FALSE(rt->guiCalled());
    auto plan = Plan::make<GuiTestPlan>();
    rt->willGui(plan);
    EXPECT_TRUE(rt->guiCalled());
}

// ---------- Teardown runs after frame loop ----------

TEST_F(PlanRuntimeTest, teardown_runs_after_loop) {
    struct TeardownPlan : TestedPlan {
        int teardownVal_ = 0;
        void infer() override { plain([](){}); }
        void teardown() override { assign(RW(teardownVal_), V(77)); }
    };
    auto plan = Plan::make<TeardownPlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    plan->clearGraph();

    plan->teardown();
    plan->makeGraph();
    plan->runGraph();
    plan->clearGraph();

    EXPECT_EQ(static_cast<TeardownPlan*>(plan.get())->teardownVal_, 77);
}

// ---------- Setup initializes state before frame loop ----------

TEST_F(PlanRuntimeTest, setup_initializes_before_infer) {
    struct SetupPlan : TestedPlan {
        int init_ = 0;
        int counter_ = 0;
        void setup() override { assign(RW(init_), V(10)); }
        void infer() override { assign(RW(counter_), R(counter_) + V(1)); }
    };
    auto plan = Plan::make<SetupPlan>();

    plan->setup();
    plan->makeGraph();
    plan->runGraph();
    plan->clearGraph();

    EXPECT_EQ(static_cast<SetupPlan*>(plan.get())->init_, 10);

    plan->infer();
    plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<SetupPlan*>(plan.get())->counter_, 1);
}

}} // namespace
