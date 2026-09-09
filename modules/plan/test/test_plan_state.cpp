// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "test_precomp.hpp"
#include "MockPlanRuntime.hpp"
#include "TestedPlan.hpp"

namespace opencv_test { namespace {

using namespace cv::plan;
using namespace cv::plan::test;

struct TestEvent {
    enum Type { PRESS, RELEASE };
    using List = std::vector<std::shared_ptr<TestEvent>>;
};
static size_t countEvents(const TestEvent::List& lst) { return lst.size(); }

// ---------- Fixture ----------

class PlanStateTest : public testing::Test {
protected:
    void SetUp() override {
        PlanRuntime::current() = cv::makePtr<test::MockPlanRuntime>(1);
        GlobalState::init_keys();
        LocalState::init_keys();
    }

    void TearDown() override {
        PlanRuntime::current().reset();
    }
};

// ---------- GlobalState get / set ----------

TEST_F(PlanStateTest, global_state_set_and_get_u64) {
    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 42);
    EXPECT_EQ(GlobalState::get<uint64_t>(GlobalState::Keys::FRAME_CNT), 42u);
}

TEST_F(PlanStateTest, global_state_set_and_get_double) {
    GlobalState::set<double>(GlobalState::Keys::FPS, 29.97);
    EXPECT_DOUBLE_EQ(GlobalState::get<double>(GlobalState::Keys::FPS), 29.97);
}

TEST_F(PlanStateTest, global_state_set_and_get_bool) {
    GlobalState::set<bool>(GlobalState::Keys::LOCKING, true);
    EXPECT_TRUE(GlobalState::get<bool>(GlobalState::Keys::LOCKING));
}

TEST_F(PlanStateTest, global_state_set_and_get_size_t) {
    GlobalState::set<size_t>(GlobalState::Keys::RUN_CNT, 99);
    EXPECT_EQ(GlobalState::get<size_t>(GlobalState::Keys::RUN_CNT), 99u);
}

// ---------- GlobalState apply ----------

TEST_F(PlanStateTest, global_state_apply_u64) {
    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 10);
    uint64_t result = GlobalState::apply<uint64_t>(GlobalState::Keys::FRAME_CNT,
        [](uint64_t& v) { v *= 2; return v; });
    EXPECT_EQ(result, 20u);
    EXPECT_EQ(GlobalState::get<uint64_t>(GlobalState::Keys::FRAME_CNT), 20u);
}

TEST_F(PlanStateTest, global_state_apply_double) {
    GlobalState::set<double>(GlobalState::Keys::FPS, 30.0);
    double result = GlobalState::apply<double>(GlobalState::Keys::FPS,
        [](double& v) { v += 0.5; return v; });
    EXPECT_DOUBLE_EQ(result, 30.5);
    EXPECT_DOUBLE_EQ(GlobalState::get<double>(GlobalState::Keys::FPS), 30.5);
}

// ---------- LocalState get / set ----------

TEST_F(PlanStateTest, local_state_set_and_get) {
    LocalState::set<size_t>(LocalState::Keys::WORKER_INDEX, 7);
    EXPECT_EQ(LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX), 7u);
}

TEST_F(PlanStateTest, local_state_is_thread_local) {
    LocalState::set<size_t>(LocalState::Keys::WORKER_INDEX, 0);
    EXPECT_EQ(LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX), 0u);
    LocalState::set<size_t>(LocalState::Keys::WORKER_INDEX, 5);
    EXPECT_EQ(LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX), 5u);
}

// ---------- Property edge reads global state ----------

TEST_F(PlanStateTest, property_edge_reads_global_state) {
    struct PropPlan : TestedPlan {
        Property<uint64_t> frameProp_ = P<uint64_t>(GlobalState::Keys::FRAME_CNT);
        uint64_t captured_ = 0;
        void infer() override {
            assign(RW(captured_), frameProp_);
        }
    };
    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 123);
    auto plan = Plan::make<PropPlan>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<PropPlan*>(plan.get())->captured_, 123u);
}

TEST_F(PlanStateTest, property_edge_is_live_per_frame) {
    struct PropPlan : TestedPlan {
        Property<uint64_t> frameProp_ = P<uint64_t>(GlobalState::Keys::FRAME_CNT);
        uint64_t captured_ = 0;
        void infer() override {
            assign(RW(captured_), frameProp_);
        }
    };
    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 10);
    auto plan = Plan::make<PropPlan>();
    plan->infer(); plan->makeGraph();

    plan->runGraph();
    EXPECT_EQ(static_cast<PropPlan*>(plan.get())->captured_, 10u);

    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 20);
    plan->runGraph();
    EXPECT_EQ(static_cast<PropPlan*>(plan.get())->captured_, 20u);
}

// ---------- set(key, edge) writes global state ----------

TEST_F(PlanStateTest, set_key_writes_global_state) {
    struct P : TestedPlan {
        double fps_ = 60.0;
        void infer() override {
            set(GlobalState::Keys::FPS, R(fps_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_DOUBLE_EQ(GlobalState::get<double>(GlobalState::Keys::FPS), 60.0);
}

// ---------- _shared / RS / RWS / CS ----------

TEST_F(PlanStateTest, shared_rs_and_rws) {
    struct P : TestedPlan {
        int shared_ = 0;
        int out_ = 0;
        P() { _shared(shared_); }
        void infer() override {
            assign(RW(out_), RS(shared_));
            RWS(shared_) = R(out_) + V(1);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    auto* p = static_cast<P*>(plan.get());
    EXPECT_EQ(p->out_, 0);
    EXPECT_EQ(p->shared_, 1);
}

TEST_F(PlanStateTest, cs_copies_under_lock) {
    struct P : TestedPlan {
        int shared_ = 42;
        int snapshot_ = 0;
        P() { _shared(shared_); }
        void infer() override {
            assign(RW(snapshot_), CS(shared_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->snapshot_, 42);
}

// ---------- _safe marks never-shared ----------

TEST_F(PlanStateTest, safe_var_throws_on_rs) {
    struct P : TestedPlan {
        int safe_ = 0;
        int out_ = 0;
        P() { _safe(safe_); }
        void infer() override {
            assign(RW(out_), RS(safe_));
        }
    };
    auto plan = Plan::make<P>();
    EXPECT_ANY_THROW(plan->infer());
}

// ---------- Auto-registration of non-member vars ----------

TEST_F(PlanStateTest, non_member_auto_registers) {
    struct P : TestedPlan {
        int out_ = 0;
        void infer() override {
            int local = 42;
            assign(RW(out_), RS(local));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->out_, 42);
}

// ---------- GlobalState::once sticky ----------

TEST_F(PlanStateTest, once_branch_runs_only_first_frame) {
    struct OncePlan : TestedPlan {
        int run_ = 0;
        void infer() override {
            branch(BranchType::ONCE, []() -> bool { return true; });
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<OncePlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<OncePlan*>(plan.get())->run_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<OncePlan*>(plan.get())->run_, 1);
}

TEST_F(PlanStateTest, parallel_once_branch_runs_only_first_frame) {
    struct ParOncePlan : TestedPlan {
        int run_ = 0;
        void infer() override {
            branch(BranchType::PARALLEL_ONCE, []() -> bool { return true; });
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<ParOncePlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<ParOncePlan*>(plan.get())->run_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<ParOncePlan*>(plan.get())->run_, 1);
}

// ---------- Node-lock helpers ----------

TEST_F(PlanStateTest, lock_tryUnlock_countNodeLocks) {
    std::string name = "test-lock-node-unique-1";
    EXPECT_TRUE(GlobalState::lockNode(name));
    EXPECT_EQ(GlobalState::countNodeLocks(), 1u);
    EXPECT_TRUE(GlobalState::tryUnlockNode(name));
    EXPECT_EQ(GlobalState::countNodeLocks(), 0u);
}

// ---------- Event edge ----------

TEST_F(PlanStateTest, event_edge_produces_empty_list) {
    struct P : TestedPlan {
        Event<TestEvent> ev_ = E<TestEvent>(TestEvent::PRESS);
        size_t evCount_ = 0;
        void infer() override {
            auto fr = F(&countEvents, ev_);
            assign(RW(evCount_), fr);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->evCount_, 0u);
}

}} // namespace
