// Scratch probe: validate DSL patterns against real headers (syntax only).
// Disabled — this file has a duplicate main() and is not a proper gtest.
#if 0
#include "opencv2/plan/plan.hpp"
#include "MockPlanRuntime.hpp"

#include <thread>
#include <vector>
#include <memory>

namespace cv { namespace plan { namespace test {
class ProbePlan : public TestedPlan {
public:
};

#if 0
some pseudo
#endif
}}} // namespace

using namespace cv::plan;
using cv::plan::test::ProbePlan;

static int gFrameCounter = 0;
static void freeIncrement()            { ++gFrameCounter; }
static int  freeSquare(int v)          { return v * v; }
static void freeWriteVal(int v, int& out) { out = v; }
static bool freePredicate(int v)        { return v > 0; }

struct TestEvent {
    enum Type { PRESS, RELEASE };
    using List = std::vector<std::shared_ptr<TestEvent>>;
};
static size_t countEvents(const TestEvent::List& lst) { return lst.size(); }

int main() {
    cv::plan::PlanRuntime::current() = cv::makePtr<cv::plan::test::MockPlanRuntime>(3);
    cv::plan::GlobalState::init_keys();
    cv::plan::LocalState::init_keys();

    struct OpPlan : TestedPlan {
        int a_ = 4, b_ = 6, c_ = 2;
        int dst_ = 0;
        bool flag_ = true;
        bool flag2_ = false;
        unsigned ua_ = 6;
        std::vector<int> vec_;
        cv::Ptr<int> iptr_;
        int derefed_ = 0;
        double fps_ = 29.0;
        cv::Size size_{0,0};
        cv::plan::Property<uint64_t> frameProp_ = P<uint64_t>(GlobalState::Keys::FRAME_CNT);
        uint64_t capturedProp_ = 0;
        cv::plan::Event<TestEvent> ev_ = E<TestEvent>(TestEvent::PRESS);
        size_t evCount_ = 0;
        int tick_ = 0;
        int branchRun_ = 0;
        int singleRun_ = 0;
        int onceRun_ = 0;
        int parOnceRun_ = 0;
        int pinRun0_ = 0;
        int pinRun1_ = 0;

        void infer() override {
            assign(RW(dst_), R(a_) + R(b_));                      // assign with computed
            RW(dst_) = R(a_);                                     // operator= form (2 operands)
            auto s = ADD(R(a_), R(b_));                           // named n-ary
            assign(RW(dst_), s);
            auto g = OP<Operators::ADD_>(R(a_), R(b_));           // generic
            assign(RW(dst_), g);
            op<Operators::ADD_>(R(a_), R(b_));                    // statement form (discard)
            auto s3 = R(a_) + _(R(b_), R(c_));                    // _() n-ary with symbol
            assign(RW(dst_), s3);
            auto s4 = MUL(R(a_), R(b_));                          // named mul
            assign(RW(dst_), s4);
            assign(RW(dst_), R(a_) / R(b_));                      // div
            assign(RW(dst_), R(b_) % R(c_));                      // mod
            assign(RW(dst_), -R(b_));                             // unary minus
            INCL(RW(a_));                                         // pre-inc
            INCR(RW(a_));                                         // post-inc
            DECL(RW(a_));                                         // pre-dec
            DECR(RW(a_));                                         // post-dec
            op<Operators::INCL_>(RW(a_));                         // statement inc
            assign(RW(flag_), R(a_) > R(b_));                     // compare
            assign(RW(flag_), R(a_) < R(b_) && R(b_) > V(0));     // logical
            assign(RW(flag_), R(flag_) || V(false));              // or
            assign(RW(flag_), !R(flag_));                         // not
            assign(RW(ua_), R(ua_) & V(2u));                      // band
            assign(RW(ua_), R(ua_) | V(1u));                      // bor
            assign(RW(ua_), R(ua_) ^ V(3u));                      // xor
            assign(RW(ua_), R(ua_) << V(1));                      // shl
            assign(RW(ua_), R(ua_) >> V(1));                      // shr
            assign(RW(dst_), IF(R(flag_), R(a_), R(b_)));          // ternary select

            vec_.push_back(42); iptr_ = new int(-7);
            assign(RW(dst_), IDX(vec_, V(0)));                    // container index
            DEREF(RW(derefed_), R(iptr_));                        // deref through smart ptr
            NEG(RW(dst_), R(a_));                                 // destination-first neg
            construct(RW(size_), V(640), V(480));                 // construct

            auto q1 = F(&freeSquare, R(a_));                      // free fn value
            assign(RW(dst_), q1);
            F(&freeIncrement);                                    // free fn void (statement)
            F(&freeWriteVal, R(a_), RW(dst_));                    // free fn mixed req? takes int& out via value int? check
            auto w = F(&cv::Size::width, R(size_));               // member data access
            assign(RW(dst_), w);
            auto l = F([](int v) { return v + 1; }, R(a_));       // lambda value
            assign(RW(dst_), l);

            plain([this]() { ++tick_; });                         // plain lambda no args
            plain(freeIncrement);                                 // plain free fn no args
            plain([](int v) { (void)v; }, R(a_));                 // plain lambda with R arg (const int&)
            plain([](const int& v) { (void)v; }, R(a_));
            plain([](int& v) { ++v; }, RW(dst_));                 // plain lambda with RW arg (int&)

            auto fr = F(&countEvents, ev_);                       // event edge via F
            assign(RW(evCount_), fr);

            assign(RW(capturedProp_), frameProp_);                // property live read
            set(GlobalState::Keys::FPS, R(fps_));                 // property write node

            branch(R(a_) > V(0));                                 // edge predicate
                plain([this]() { ++branchRun_; });
            endBranch();

            branch([](int v){ return v > 0; }, R(a_));            // lambda predicate with arg
                plain([this]() { ++branchRun_; });
            endBranch();

            branch(cv::plan::Plan::always_);                      // predefined predicate
                plain([this]() { ++branchRun_; });
            endBranch();

            branch(BranchType::SINGLE, R(a_) >= V(0));            // SINGLE with edge pred
                plain([this]() { ++singleRun_; });
            endBranch();

            branch(BranchType::ONCE, [](){ return true; });       // ONCE with fresh lambda
                plain([this]() { ++onceRun_; });
            endBranch();

            branch(BranchType::PARALLEL_ONCE, [](){ return true; }); // PARALLEL_ONCE
                plain([this]() { ++parOnceRun_; });
            endBranch();

            LocalState::set<size_t>(LocalState::Keys::WORKER_INDEX, 0);
            branch(0, [](int v){ return v >= 0; }, R(a_));        // worker-pinned
                plain([this]() { ++pinRun0_; });
            endBranch();

            branch(BranchType::PARALLEL, 1, [](int v){ return v >= 0; }, R(a_)); // type+worker pinned
                plain([this]() { ++pinRun1_; });
            endBranch();

            assign(RW(dst_), R(a_) + V(1));                       // constant operand
        }
    };

    cv::Ptr<OpPlan> p = Plan::make<OpPlan>();
    p->infer();
    p->makeGraph();
    p->runGraph();
    return 0;
}
#endif