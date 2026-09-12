// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
#include "perf_precomp.hpp"

namespace opencv_test { namespace {

using namespace cv::plan;
using namespace cv::plan::test;

// ---- shared setup: fresh MockRuntime + init key registries -----------------
static void initPlanPerf() {
    cv::plan::PlanRuntime::current().reset();
    cv::plan::GlobalState::init_keys();
    cv::plan::LocalState::init_keys();
    cv::plan::PlanRuntime::current() = cv::makePtr<cv::plan::test::MockPlanRuntime>(3);
}
// NOTE: PlanRuntime::current() is thread_local. Worker threads spawned by
// Plan::run pick up the runtime themselves (plan.hpp:1154).

// ---- 1. Steady-state graph replay: flat arithmetic chain -------------------
// N int elements pre-sized in the ctor; infer() records N sequential
// x_[i] = x_[i-1] + 1 nodes. Graph built ONCE, runGraph() replayed each cycle:
// this is the realistic per-frame cost.
// NOTE: per element two nodes are emitted — the ADD (R(x_[i-1]) + V(1)) and the
// ASSIGN — so nodeCount = 2*(N-1).
typedef perf::TestBaseWithParam<int> PlanNodeCount;

PERF_TEST_P(PlanNodeCount, graph_replay_flat, testing::Values(16, 64, 256, 1024, 4096))
{
    const int N = GetParam();
    initPlanPerf();

    struct FlatPlan : TestedPlan {
        std::vector<int> x_;
        explicit FlatPlan(size_t n) : x_(n, 1) {}
        void infer() override {
            for (size_t i = 1; i < x_.size(); ++i)
                assign(RW(x_[i]), R(x_[i - 1]) + V(1));
        }
    };
    auto plan = Plan::make<FlatPlan>(N);
    plan->infer();
    plan->makeGraph();
    ASSERT_EQ(plan->nodeCount(), (size_t)(2 * (N - 1)));   // guard: distinct nodes, no collapse

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 2. Graph (re)build cost: infer + makeGraph + clearGraph ----------------
PERF_TEST_P(PlanNodeCount, graph_build_flat, testing::Values(16, 64, 256, 1024))
{
    const int N = GetParam();
    initPlanPerf();

    struct FlatPlan : TestedPlan {
        std::vector<int> x_;
        explicit FlatPlan(size_t n) : x_(n, 1) {}
        void infer() override {
            for (size_t i = 1; i < x_.size(); ++i)
                assign(RW(x_[i]), R(x_[i - 1]) + V(1));
        }
    };
    auto plan = Plan::make<FlatPlan>(N);
    // addresses of x_ elements are stable across rebuilds, so node ids are
    // deterministic; clearGraph() wipes accesses_/transactions_ each cycle.
    TEST_CYCLE() { plan->infer(); plan->makeGraph(); plan->clearGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 3. Full lifecycle: infer+makeGraph+runGraph+clearGraph per cycle -------
PERF_TEST_P(PlanNodeCount, full_lifecycle, testing::Values(16, 64, 256))
{
    const int N = GetParam();
    initPlanPerf();

    struct FlatPlan : TestedPlan {
        std::vector<int> x_;
        explicit FlatPlan(size_t n) : x_(n, 1) {}
        void infer() override {
            for (size_t i = 1; i < x_.size(); ++i)
                assign(RW(x_[i]), R(x_[i - 1]) + V(1));
        }
    };
    auto plan = Plan::make<FlatPlan>(N);
    TEST_CYCLE() { plan->infer(); plan->makeGraph(); plan->runGraph(); plan->clearGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 4. plain() node overhead -------------------------------------------------
PERF_TEST_P(PlanNodeCount, plain_nodes, testing::Values(16, 64, 256, 1024))
{
    const int N = GetParam();
    initPlanPerf();

    struct PlainPlan : TestedPlan {
        std::vector<int> tick_;
        explicit PlainPlan(size_t n) : tick_(n, 0) {}
        void infer() override {
            for (size_t i = 0; i < tick_.size(); ++i)
                plain([this, i]() { ++tick_[i]; });
        }
    };
    auto plan = Plan::make<PlainPlan>(N);
    plan->infer();
    plan->makeGraph();
    ASSERT_EQ(plan->nodeCount(), (size_t)N);

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 5. F() call overhead (pure algebraic) -------------------------------------
PERF_TEST_P(PlanNodeCount, F_pure_calls, testing::Values(16, 64, 256, 1024))
{
    const int N = GetParam();
    initPlanPerf();

    struct FPlan : TestedPlan {
        int src_ = 1;
        std::vector<int> dst_;
        explicit FPlan(size_t n) : dst_(n, 0) {}
        void infer() override {
            auto f = F([](int v) -> int { return v + 1; }, R(src_));
            for (size_t i = 0; i < dst_.size(); ++i)
                assign(RW(dst_[i]), f);
        }
    };
    auto plan = Plan::make<FPlan>(N);
    plan->infer();
    plan->makeGraph();
    // F() emits one node for its own closure plus one ASSIGN node per element.
    ASSERT_EQ(plan->nodeCount(), (size_t)(N + 1));

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 6. F() wrapping a real OpenCV op (cvtColor), over typical frame sizes -----
typedef perf::TestBaseWithParam<cv::Size> PlanFrameSize;

PERF_TEST_P(PlanFrameSize, F_opencv_cvtColor,
            testing::Values(szVGA, szqHD, sz720p, sz1080p))
{
    const cv::Size sz = GetParam();
    initPlanPerf();

    struct FImgPlan : TestedPlan {
        cv::Mat src_, dst_;
        void infer() override {
            F([](const cv::Mat& s, cv::Mat& d) {
                cv::cvtColor(s, d, cv::COLOR_BGR2GRAY);
            }, R(src_), RW(dst_));
        }
    };
    auto plan = Plan::make<FImgPlan>();
    static_cast<FImgPlan*>(plan.get())->src_ = cv::Mat(sz, CV_8UC3, cv::Scalar::all(1));
    plan->infer();
    plan->makeGraph();

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 7. Branch overhead: N true branches (if-body runs) -------------------------
PERF_TEST_P(PlanNodeCount, branches_true, testing::Values(8, 32, 128, 512))
{
    const int N = GetParam();
    initPlanPerf();

    struct BranchPlan : TestedPlan {
        std::vector<int> flag_;      // vector<bool> is proxy-based — must NOT be used
        std::vector<int> run_;
        explicit BranchPlan(size_t n) : flag_(n, 1), run_(n, 0) {}
        void infer() override {
            for (size_t i = 0; i < flag_.size(); ++i) {
                branch(R(flag_[i]) > V(0));
                    plain([this, i]() { ++run_[i]; });
                endBranch();
            }
        }
    };
    auto plan = Plan::make<BranchPlan>(N);
    plan->infer();
    plan->makeGraph();
    // Per element: GT nary-op + branch + plain + [end] = 4 nodes.
    ASSERT_EQ(plan->nodeCount(), (size_t)(4 * N));

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 8. Branch overhead: false predicate, else-body runs -------------------------
PERF_TEST_P(PlanNodeCount, branches_else, testing::Values(8, 32, 128, 512))
{
    const int N = GetParam();
    initPlanPerf();

    struct BranchPlan : TestedPlan {
        std::vector<int> flag_;
        std::vector<int> run_;
        explicit BranchPlan(size_t n) : flag_(n, 0), run_(n, 0) {}
        void infer() override {
            for (size_t i = 0; i < flag_.size(); ++i) {
                branch(R(flag_[i]) > V(0));
                    plain([this, i]() { ++run_[i]; });
                elseBranch();
                    plain([this, i]() { ++run_[i]; });
                endBranch();
            }
        }
    };
    auto plan = Plan::make<BranchPlan>(N);
    plan->infer();
    plan->makeGraph();

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 9. Sub-plan replay: parent with K sub-plans of M nodes each --------------
PERF_TEST_P(PlanNodeCount, subplan_replay, testing::Values(2, 8, 32, 128))
{
    const int N = GetParam();          // total = N sub-plans x 16 nodes
    initPlanPerf();

    const int subNodes = 16;
    struct Sub : TestedPlan {
        std::vector<int> x_;
        explicit Sub(size_t n) : x_(n, 1) {}
        void infer() override {
            for (size_t i = 1; i < x_.size(); ++i)
                assign(RW(x_[i]), R(x_[i - 1]) + V(1));
        }
    };
    struct Parent : TestedPlan {
        std::vector<cv::Ptr<Sub>> subs_;
        explicit Parent(int k, int m) {
            for (int i = 0; i < k; ++i) subs_.push_back(_sub<Sub>(this, (size_t)m));
        }
        void infer() override {
            for (auto& s : subs_) subInfer(s);
        }
    };
    auto plan = Plan::make<Parent>(N, subNodes);
    plan->infer();
    plan->makeGraph();
    // Per sub-plan: 2 nodes per element (ADD + ASSIGN) over subNodes-1 links.
    ASSERT_EQ(plan->nodeCount(), (size_t)(N * 2 * (subNodes - 1)));

    TEST_CYCLE() { plan->runGraph(); }
    SANITY_CHECK_NOTHING();
}

// ---- 10. End-to-end 3-worker run: Plan::run<T>(3) --------------------------------
// Plan::run spawns 3 extra worker threads; EACH thread (including main) runs the
// mock frame loop with its own plan instance, so one cycle = 4 x frames runGraphs
// plus setup/infer/barrier/teardown/join. frameCount=64 amortizes spawn cost so
// dispatch dominates. Result is reported as ms/iteration (whole run).
PERF_TEST(PlanPerf, end_to_end_run_3_workers)
{
    initPlanPerf();
    cv::plan::PlanRuntime::current() = cv::makePtr<cv::plan::test::MockPlanRuntime>(64);

    struct WorkerPlan : TestedPlan {
        std::vector<int> x_;
        WorkerPlan() : x_(256, 1) {}
        void infer() override {
            for (size_t i = 1; i < x_.size(); ++i)
                assign(RW(x_[i]), R(x_[i - 1]) + V(1));
        }
        void setup() override { plain([]() {}); }
        void teardown() override { plain([]() {}); }
    };

    TEST_CYCLE() { Plan::run<WorkerPlan>(3); }
    SANITY_CHECK_NOTHING();
}

}} // namespace