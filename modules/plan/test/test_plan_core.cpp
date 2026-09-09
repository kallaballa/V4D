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
static int mySquare(int v) { return v * v; }
static void myVoidFn() {}
static int gFrameCounter = 0;
static void myIncrement() { ++gFrameCounter; }

struct TestEvent {
    enum Type { PRESS, RELEASE };
    using List = std::vector<std::shared_ptr<TestEvent>>;
};
static size_t countEvents(const TestEvent::List& lst) { return lst.size(); }

// ---------- Test fixture ----------

class PlanCoreTest : public testing::Test {
protected:
    void SetUp() override {
        PlanRuntime::current() = cv::makePtr<test::MockPlanRuntime>(3);
        GlobalState::init_keys();
        LocalState::init_keys();
        gFrameCounter = 0;
    }

    void TearDown() override {
        PlanRuntime::current().reset();
    }
};

// ---------- Construction / runtime ----------

TEST_F(PlanCoreTest, plan_construction_acquires_runtime) {
    struct SimplePlan : TestedPlan {
        void infer() override { plain([](){}); }
    };
    auto plan = Plan::make<SimplePlan>();
    ASSERT_NE(plan, nullptr);
    ASSERT_NE(plan->runtime(), nullptr);
}

TEST_F(PlanCoreTest, infer_populates_nodes) {
    struct NP : TestedPlan {
        int v_ = 0;
        void infer() override { assign(RW(v_), V(1)); }
    };
    auto plan = Plan::make<NP>();
    plan->infer();
    plan->makeGraph();
    EXPECT_FALSE(plan->currentNodes().empty());
}

TEST_F(PlanCoreTest, makeGraph_and_runGraph) {
    struct RP : TestedPlan {
        int v_ = 0;
        void infer() override { assign(RW(v_), V(1)); }
    };
    auto plan = Plan::make<RP>();
    plan->infer();
    plan->makeGraph();
    plan->runGraph();
    SUCCEED();
}

// ---------- Counter / frame loop ----------

TEST_F(PlanCoreTest, counter_increments_per_frame) {
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

// ---------- Lifecycle ----------

TEST_F(PlanCoreTest, setup_teardown_build_and_run) {
    struct SetupTeardownPlan : TestedPlan {
        int setupVal_ = 0;
        int teardownVal_ = 0;
        void setup() override { assign(RW(setupVal_), V(42)); }
        void infer() override { plain([](){}); }
        void teardown() override { assign(RW(teardownVal_), V(99)); }
    };
    auto plan = Plan::make<SetupTeardownPlan>();
    plan->setup();
    plan->makeGraph();
    plan->runGraph();
    plan->clearGraph();
    EXPECT_EQ(static_cast<SetupTeardownPlan*>(plan.get())->setupVal_, 42);

    plan->teardown();
    plan->makeGraph();
    plan->runGraph();
    plan->clearGraph();
    EXPECT_EQ(static_cast<SetupTeardownPlan*>(plan.get())->teardownVal_, 99);
}

TEST_F(PlanCoreTest, gui_does_not_throw) {
    struct GuiPlan : TestedPlan {
        void gui() override { plain([](){}); }
        void infer() override { plain([](){}); }
    };
    auto plan = Plan::make<GuiPlan>();
    plan->gui();
    plan->infer();
    plan->makeGraph();
    plan->runGraph();
}

// ---------- Arithmetic: symbol form ----------

TEST_F(PlanCoreTest, arithmetic_symbol_add) {
    struct P : TestedPlan {
        int a_ = 3, b_ = 4, dst_ = 0;
        void infer() override { assign(RW(dst_), R(a_) + R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 7);
}

TEST_F(PlanCoreTest, arithmetic_symbol_sub) {
    struct P : TestedPlan {
        int a_ = 10, b_ = 3, dst_ = 0;
        void infer() override { assign(RW(dst_), R(a_) - R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 7);
}

TEST_F(PlanCoreTest, arithmetic_symbol_mul) {
    struct P : TestedPlan {
        int a_ = 3, b_ = 4, dst_ = 0;
        void infer() override { assign(RW(dst_), R(a_) * R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 12);
}

TEST_F(PlanCoreTest, arithmetic_symbol_div) {
    struct P : TestedPlan {
        int a_ = 12, b_ = 4, dst_ = 0;
        void infer() override { assign(RW(dst_), R(a_) / R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 3);
}

TEST_F(PlanCoreTest, arithmetic_symbol_mod) {
    struct P : TestedPlan {
        int a_ = 7, b_ = 3, dst_ = 0;
        void infer() override { assign(RW(dst_), R(a_) % R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 1);
}

// ---------- Arithmetic: named / generic / statement / _() forms ----------

TEST_F(PlanCoreTest, arithmetic_named_add) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 6, dst_ = 0;
        void infer() override {
            auto s = ADD(R(a_), R(b_));
            assign(RW(dst_), s);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 11);
}

TEST_F(PlanCoreTest, arithmetic_generic_add) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 6, dst_ = 0;
        void infer() override {
            auto g = OP<Operators::ADD_>(R(a_), R(b_));
            assign(RW(dst_), g);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 11);
}

TEST_F(PlanCoreTest, arithmetic_statement_form) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 6, dst_ = 0;
        void infer() override {
            op<Operators::ADD_>(R(a_), R(b_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    SUCCEED();
}

TEST_F(PlanCoreTest, arithmetic_nary_with_underscore) {
    struct P : TestedPlan {
        int a_ = 1, b_ = 2, c_ = 3, dst_ = 0;
        void infer() override {
            auto s = R(a_) + _(R(b_), R(c_));
            assign(RW(dst_), s);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 7);
}

// ---------- Unary minus ----------

TEST_F(PlanCoreTest, unary_minus) {
    struct P : TestedPlan {
        int a_ = -5, dst_ = 0;
        void infer() override { assign(RW(dst_), -R(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 5);
}

// ---------- INCL / INCR / DECL / DECR ----------

TEST_F(PlanCoreTest, pre_increment) {
    struct P : TestedPlan {
        int a_ = 5;
        void infer() override { INCL(RW(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->a_, 6);
}

TEST_F(PlanCoreTest, post_increment) {
    struct P : TestedPlan {
        int a_ = 5;
        void infer() override { INCR(RW(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->a_, 6);
}

TEST_F(PlanCoreTest, pre_decrement) {
    struct P : TestedPlan {
        int a_ = 5;
        void infer() override { DECL(RW(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->a_, 4);
}

TEST_F(PlanCoreTest, post_decrement) {
    struct P : TestedPlan {
        int a_ = 5;
        void infer() override { DECR(RW(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->a_, 4);
}

// ---------- Comparison ----------

TEST_F(PlanCoreTest, comparison_gt) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 3;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) > R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

TEST_F(PlanCoreTest, comparison_lt) {
    struct P : TestedPlan {
        int a_ = 3, b_ = 5;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) < R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

TEST_F(PlanCoreTest, comparison_eq) {
    struct P : TestedPlan {
        int a_ = 7, b_ = 7;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) == R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

TEST_F(PlanCoreTest, comparison_neq) {
    struct P : TestedPlan {
        int a_ = 7, b_ = 8;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) != R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

TEST_F(PlanCoreTest, comparison_le) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 5;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) <= R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

TEST_F(PlanCoreTest, comparison_ge) {
    struct P : TestedPlan {
        int a_ = 5, b_ = 5;
        bool flag_ = false;
        void infer() override { assign(RW(flag_), R(a_) >= R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->flag_);
}

// ---------- Logical ----------

TEST_F(PlanCoreTest, logical_and) {
    struct P : TestedPlan {
        bool a_ = true, b_ = true, dst_ = false;
        void infer() override { assign(RW(dst_), R(a_) && R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->dst_);
}

TEST_F(PlanCoreTest, logical_or) {
    struct P : TestedPlan {
        bool a_ = false, b_ = true, dst_ = false;
        void infer() override { assign(RW(dst_), R(a_) || R(b_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_TRUE(static_cast<P*>(plan.get())->dst_);
}

TEST_F(PlanCoreTest, logical_not) {
    struct P : TestedPlan {
        bool a_ = true, dst_ = false;
        void infer() override { assign(RW(dst_), !R(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_FALSE(static_cast<P*>(plan.get())->dst_);
}

// ---------- Bitwise ----------

TEST_F(PlanCoreTest, bitwise_band) {
    struct P : TestedPlan {
        unsigned ua_ = 6, dst_ = 0;
        void infer() override { assign(RW(dst_), R(ua_) & V(2u)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 2u);
}

TEST_F(PlanCoreTest, bitwise_bor) {
    struct P : TestedPlan {
        unsigned ua_ = 4, dst_ = 0;
        void infer() override { assign(RW(dst_), R(ua_) | V(1u)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 5u);
}

TEST_F(PlanCoreTest, bitwise_xor) {
    struct P : TestedPlan {
        unsigned ua_ = 5, dst_ = 0;
        void infer() override { assign(RW(dst_), R(ua_) ^ V(3u)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 6u);
}

TEST_F(PlanCoreTest, bitwise_shl) {
    struct P : TestedPlan {
        unsigned ua_ = 3, dst_ = 0;
        void infer() override { assign(RW(dst_), R(ua_) << V(1)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 6u);
}

TEST_F(PlanCoreTest, bitwise_shr) {
    struct P : TestedPlan {
        unsigned ua_ = 6, dst_ = 0;
        void infer() override { assign(RW(dst_), R(ua_) >> V(1)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 3u);
}

// ---------- IF (ternary select) ----------

TEST_F(PlanCoreTest, if_select) {
    struct P : TestedPlan {
        bool flag_ = true;
        int a_ = 10, b_ = 20, dst_ = 0;
        void infer() override { assign(RW(dst_), IF(R(flag_), R(a_), R(b_))); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 10);
}

// ---------- IDX (container index) ----------

TEST_F(PlanCoreTest, idx_container) {
    struct P : TestedPlan {
        std::vector<int> vec_;
        int dst_ = 0;
        void infer() override {
            assign(RW(dst_), IDX(R(vec_), V(0)));
        }
    };
    auto plan = Plan::make<P>();
    static_cast<P*>(plan.get())->vec_.push_back(42);
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 42);
}

// ---------- DEREF (smart pointer) ----------

TEST_F(PlanCoreTest, deref_smart_ptr) {
    struct DerefPlan : TestedPlan {
        cv::Ptr<int> iptr_;
        int derefed_ = 0;
        void infer() override {
            DEREF(RW(derefed_), RW(iptr_));
        }
    };
    auto plan = Plan::make<DerefPlan>();
    static_cast<DerefPlan*>(plan.get())->iptr_ = cv::makePtr<int>(-7);
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<DerefPlan*>(plan.get())->derefed_, -7);
}

// ---------- NEG (destination-first) ----------

TEST_F(PlanCoreTest, neg_dst_first) {
    struct P : TestedPlan {
        int a_ = 5, dst_ = 0;
        void infer() override { NEG(RW(dst_), R(a_)); }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, -5);
}

// ---------- CONSTRUCT ----------

TEST_F(PlanCoreTest, construct_size) {
    struct P : TestedPlan {
        cv::Size size_;
        void infer() override {
            construct(RW(size_), V(640), V(480));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    auto& s = static_cast<P*>(plan.get())->size_;
    EXPECT_EQ(s.width, 640);
    EXPECT_EQ(s.height, 480);
}

// ---------- F calls ----------

TEST_F(PlanCoreTest, f_free_fn_value) {
    struct P : TestedPlan {
        int a_ = -7, dst_ = 0;
        void infer() override {
            auto q = F(&myAbs, R(a_));
            assign(RW(dst_), q);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 7);
}

TEST_F(PlanCoreTest, f_lambda_value) {
    struct P : TestedPlan {
        int a_ = 5, dst_ = 0;
        void infer() override {
            auto l = F([](int v) -> int { return v + 1; }, R(a_));
            assign(RW(dst_), l);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 6);
}

TEST_F(PlanCoreTest, f_member_data) {
    struct P : TestedPlan {
        cv::Size size_{640, 480};
        int dst_ = 0;
        void infer() override {
            auto w = F(&cv::Size::width, R(size_));
            assign(RW(dst_), w);
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 640);
}

// ---------- Plain nodes ----------

TEST_F(PlanCoreTest, plain_lambda_no_args) {
    struct P : TestedPlan {
        int tick_ = 0;
        void infer() override {
            plain([this]() { ++tick_; });
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->tick_, 0);
    plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->tick_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->tick_, 2);
    plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->tick_, 3);
}

TEST_F(PlanCoreTest, plain_lambda_with_r_arg) {
    struct P : TestedPlan {
        int a_ = 42;
        int captured_ = 0;
        void infer() override {
            plain([](const int& v) { /* just read */ }, R(a_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    SUCCEED();
}

TEST_F(PlanCoreTest, plain_lambda_with_rw_arg) {
    struct P : TestedPlan {
        int dst_ = 0;
        void infer() override {
            plain([](int& v) { v = 99; }, RW(dst_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 99);
}

// ---------- Chaining ----------

TEST_F(PlanCoreTest, chaining_assignments) {
    struct P : TestedPlan {
        int a_ = 1, b_ = 2, dst_ = 0;
        void infer() override {
            assign(RW(dst_), R(a_));
            assign(RW(dst_), R(b_));
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->dst_, 2);
}

// ---------- Branches ----------

TEST_F(PlanCoreTest, branch_edge_predicate) {
    struct P : TestedPlan {
        int a_ = 5;
        int run_ = 0;
        void infer() override {
            branch(R(a_) > V(0));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_lambda_no_args) {
    struct P : TestedPlan {
        int run_ = 0;
        void infer() override {
            branch([]() -> bool { return true; });
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_lambda_with_args) {
    struct P : TestedPlan {
        int a_ = 5;
        int run_ = 0;
        void infer() override {
            branch([](int v) -> bool { return v > 0; }, R(a_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_predefined_always) {
    struct P : TestedPlan {
        int run_ = 0;
        void infer() override {
            branch(cv::plan::Plan::always_);
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_predefined_isTrue) {
    struct P : TestedPlan {
        bool flag_ = true;
        int run_ = 0;
        void infer() override {
            branch(cv::plan::Plan::isTrue_, R(flag_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_predefined_isFalse) {
    struct P : TestedPlan {
        bool flag_ = false;
        int run_ = 0;
        void infer() override {
            branch(cv::plan::Plan::isFalse_, R(flag_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_predefined_and) {
    struct P : TestedPlan {
        bool a_ = true, b_ = true;
        int run_ = 0;
        void infer() override {
            branch(cv::plan::Plan::and_, R(a_), R(b_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_predefined_or) {
    struct P : TestedPlan {
        bool a_ = false, b_ = true;
        int run_ = 0;
        void infer() override {
            branch(cv::plan::Plan::or_, R(a_), R(b_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 1);
}

TEST_F(PlanCoreTest, branch_false_predicate_skips_body) {
    struct P : TestedPlan {
        int run_ = 0;
        void infer() override {
            branch(R(run_) > V(0));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->run_, 0);
}

// ---------- elseBranch ----------

TEST_F(PlanCoreTest, else_branch) {
    struct P : TestedPlan {
        int a_ = 0;
        int ifRun_ = 0, elseRun_ = 0;
        void infer() override {
            branch(R(a_) > V(0));
                plain([this]() { ++ifRun_; });
            elseBranch();
                plain([this]() { ++elseRun_; });
            endBranch();
        }
    };
    auto plan = Plan::make<P>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<P*>(plan.get())->ifRun_, 0);
    EXPECT_EQ(static_cast<P*>(plan.get())->elseRun_, 1);
}

// ---------- SINGLE branch ----------

TEST_F(PlanCoreTest, single_branch) {
    struct SinglePlan : TestedPlan {
        int a_ = 1;
        int run_ = 0;
        void infer() override {
            branch(BranchType::SINGLE, R(a_) >= V(0));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<SinglePlan>();
    plan->infer(); plan->makeGraph();
    plan->runGraph();
    EXPECT_EQ(static_cast<SinglePlan*>(plan.get())->run_, 1);
    plan->runGraph();
    EXPECT_EQ(static_cast<SinglePlan*>(plan.get())->run_, 2);
}

// ---------- ONCE branch ----------

TEST_F(PlanCoreTest, once_branch) {
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

// ---------- PARALLEL_ONCE branch ----------

TEST_F(PlanCoreTest, parallel_once_branch) {
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

// ---------- Worker-pinned branch ----------

TEST_F(PlanCoreTest, worker_pinned_branch) {
    struct PinPlan : TestedPlan {
        int a_ = 1;
        int run_ = 0;
        void infer() override {
            LocalState::set<size_t>(LocalState::Keys::WORKER_INDEX, 0);
            branch(0, [](int v) -> bool { return v >= 0; }, R(a_));
                plain([this]() { ++run_; });
            endBranch();
        }
    };
    auto plan = Plan::make<PinPlan>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<PinPlan*>(plan.get())->run_, 1);
}

// ---------- Sub-plan ----------

TEST_F(PlanCoreTest, sub_plan_subInfer) {
    struct Sub : TestedPlan {
        int val_ = 0;
        void infer() override { assign(RW(val_), V(42)); }
    };
    struct Parent : TestedPlan {
        cv::Ptr<Sub> sub_;
        int result_ = 0;
        Parent() { sub_ = _sub<Sub>(this); }
        void infer() override { subInfer(sub_); }
    };
    auto plan = Plan::make<Parent>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    auto* parent = static_cast<Parent*>(plan.get());
    auto* sub = parent->sub_.get();
    EXPECT_EQ(sub->val_, 42);
}

// ---------- Property / Event ----------

TEST_F(PlanCoreTest, property_reads_global_state) {
    struct PropertyPlan : TestedPlan {
        Property<uint64_t> frameProp_ = P<uint64_t>(GlobalState::Keys::FRAME_CNT);
        uint64_t captured_ = 0;
        void infer() override {
            assign(RW(captured_), frameProp_);
        }
    };
    GlobalState::set<uint64_t>(GlobalState::Keys::FRAME_CNT, 123);
    auto plan = Plan::make<PropertyPlan>();
    plan->infer(); plan->makeGraph(); plan->runGraph();
    EXPECT_EQ(static_cast<PropertyPlan*>(plan.get())->captured_, 123u);
}

TEST_F(PlanCoreTest, set_key_writes_global_state) {
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

TEST_F(PlanCoreTest, event_edge) {
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
