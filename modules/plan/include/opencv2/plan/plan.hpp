// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#ifndef OPENCV_PLAN_PLAN_HPP_
#define OPENCV_PLAN_PLAN_HPP_

#include "flags.hpp"
#include "util.hpp"
#include "detail/context.hpp"
#include "detail/transaction.hpp"

#include <shared_mutex>
#include <future>
#include <set>
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <barrier>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <iomanip>
#include <sstream>

#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utility.hpp>

#include "threadsafeanymap.hpp"

using namespace std::chrono_literals;
using namespace cv::utils::logging;

namespace cv {
namespace plan {

using namespace cv::plan::detail;

namespace detail {

template <typename T> using static_not = std::integral_constant<bool, !T::value>;

template<typename T> std::string int_to_hex( T i )
{
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0') << std::setw(sizeof(T) * 2)
         << std::hex << i;
  return stream.str();
}

template<typename Tlamba> std::string lambda_ptr_hex(Tlamba&& l) {
    return int_to_hex((size_t)Lambda::ptr(l));
}

static std::size_t map_index(const std::thread::id id) {
    static std::size_t nextindex = 0;
    static std::mutex my_mutex;
    static std::unordered_map<std::thread::id, std::size_t> ids;
    std::lock_guard<std::mutex> lock(my_mutex);
    auto iter = ids.find(id);
    if(iter == ids.end())
        return ids[id] = nextindex++;
    return iter->second;
}

template<bool read, typename Tfn, typename ... Args>
static auto make_function_ptr(Tfn&& fn, Args ... args) {
	using fun_t = typename edgefun_t<read, typename std::remove_reference<Tfn>::type, Args...>::type;
	return cv::makePtr<fun_t>(std::forward<Tfn>(fn));
}

}

class CV_EXPORTS Plan;

/*!
 * Abstract runtime interface that the plan module depends on.
 * V4D implements this to provide graphics contexts and property management.
 */
class CV_EXPORTS PlanRuntime {
public:
	virtual ~PlanRuntime() {}

	/*!
	 * Thread-local registry of the runtime instance associated with the calling
	 * thread. Registered by the runtime on initialization (e.g. V4D::init) and
	 * picked up by Plan's default constructor. Defined in the plan module so
	 * all modules share the same instance.
	 *
	 * Returns an owning cv::Ptr so that consumers share ownership of the
	 * runtime instead of wrapping an already-owned raw pointer.
	 */
	CV_EXPORTS static cv::Ptr<PlanRuntime>& current();

	// Context accessors - implemented by V4D
	virtual cv::Ptr<detail::PlainContext> plainCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> glCtx(int32_t idx = 0) = 0;
	virtual cv::Ptr<detail::PlanContext> fbCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> nvgCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> bgfxCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> extCtx(int32_t idx = 0) = 0;
	virtual cv::Ptr<detail::PlanContext> sourceCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> sinkCtx() = 0;
	virtual cv::Ptr<detail::PlanContext> imguiCtx() = 0;

	virtual bool hasPlainCtx() = 0;
	virtual bool hasGlCtx(uint32_t idx = 0) = 0;
	virtual bool hasFbCtx() = 0;
	virtual bool hasNvgCtx() = 0;
	virtual bool hasBgfxCtx() = 0;
	virtual bool hasExtCtx(uint32_t idx = 0) = 0;
	virtual bool hasSourceCtx() = 0;
	virtual bool hasSinkCtx() = 0;
	virtual bool hasImguiCtx() = 0;

	// Debug flags
	virtual uint32_t debugFlags() const = 0;

	// Viewport access
	virtual cv::Rect getViewport() const = 0;

	// Lifecycle hooks used by Plan::run (see §1.5 of the Plan-DSL reference)

	/*!
	 * Called inside each spawned worker thread before its plan is built.
	 * The runtime associates the calling thread with worker workerIdx here
	 * (e.g. create a per-thread runtime instance, propagate IO, set the
	 * thread name/priority).
	 */
	virtual void initWorkerThread(int32_t workerIdx) {
		CV_UNUSED(workerIdx);
	}

	/*!
	 * Called on the main thread right before the plan's gui() method.
	 */
	virtual void willGui(const cv::Ptr<Plan>& plan) {
		CV_UNUSED(plan);
	}

	/*!
	 * Execute the frame loop. Must invoke frameFn once per frame/iteration
	 * until the runtime decides to finish (e.g. keep_running() turns false).
	 */
	virtual void runFrameLoop(std::function<void()> frameFn) = 0;

	/*!
	 * Called after the frame loop finished, on every participating thread
	 * (release per-thread IO resources here).
	 */
	virtual void releaseIo() {}
};

class CV_EXPORTS Plan {
	friend class SharedVariables;
protected:
    struct BranchState {
		string branchID_;
    	bool isEnabled_ = true;
    	bool isOnce_ = false;
    	bool isSingle_ = false;
    	bool condition_ = false;
    	bool isLocked_ = false;
    };

protected:
    cv::Ptr<PlanRuntime> runtime_;
    std::string parent_;
    cv::UMat captureFrame_;
    cv::UMat writerFrame_;
    size_t parentOffset_ = 0;
    size_t parentActualTypeSize_ = 0;
    size_t actualTypeSize_ = 0;
    cv::Ptr<Plan> self_;
    std::vector<std::tuple<std::string,bool,size_t>> accesses_;
    std::map<std::string, cv::Ptr<Transaction>> transactions_;
    std::vector<cv::Ptr<Node>> currentNodes_;
    std::vector<cv::Ptr<Node>> allNodes_;
    std::deque<BranchState> branchStateStack_;
    std::deque<std::pair<string, BranchType::Enum>> branchStack_;

	template<typename Tedge>
    void emit_access(const string& context, Tedge tp) {
    	accesses_.push_back(std::make_tuple(context, Tedge::read_t::value, tp.id()));
    }

    template<typename Tfn, typename ...Args>
    void add_transaction(std::function<cv::Ptr<PlanContext>()> ctxCb, string txID, Tfn fn, Args ...args) {
		auto tx = make_transaction(fn, args...);
		tx->setContextCallback(ctxCb);
		tx->setBranchType(BranchType::NONE);
		transactions_.insert({txID, tx});
    }

    template<typename Tfn, typename ...Args>
    void add_transaction(BranchType::Enum btype, std::function<cv::Ptr<PlanContext>()> ctxCb, string txID, Tfn fn, Args ...args) {
		auto tx = make_transaction(fn, args...);
		tx->setContextCallback(ctxCb);
		tx->setBranchType(btype);
		transactions_.insert({txID, tx});
    }

    template<typename Tfn, typename ...Args>
    void add_transaction(cv::Ptr<PlanContext> ctx, const string& txID, Tfn fn, Args ...args) {
    	this->add_transaction([ctx](){ return ctx; }, txID, fn, args...);
    }

    template<typename Tfn, typename ...Args>
    void add_transaction(BranchType::Enum btype, cv::Ptr<PlanContext> ctx, const string& txID, Tfn fn, Args ...args) {
    	this->add_transaction(btype, [ctx](){ return ctx; }, txID, fn, args...);
    }

    template <typename ... Args, typename Tfn>
    static auto wrap_callable(Tfn fn) {
    	if constexpr(std::is_void<typename CallableTraits<Tfn>::return_type_t>::value || std::is_same<typename CallableTraits<Tfn>::return_type_t, std::false_type>::value) {
			if constexpr(CallableTraits<Tfn>::member_t::value) {
				return std::function([fn](Args... values) -> decltype(std::mem_fn(fn)(values...))  {
					return std::mem_fn(fn)(values...);
				});
			} else {
				return std::function(fn);
			}
    	} else {
			if constexpr(CallableTraits<Tfn>::member_t::value) {
				return std::function([fn](Args... values) ->  decltype(std::mem_fn(fn)(values...))  {
					return std::mem_fn(fn)(values...);
				});
			} else {
				return std::function(fn);
			}
   	   }
   }

	template<bool Tconst, typename T>
    auto makeInternalEdge(T& val) {
		if constexpr(Tconst) {
			return R(val);
		} else {
			return RW(val);
		}
    }

    template<typename T>
    void setActualTypeSize() {
    	actualTypeSize_ = sizeof(T);
    }

    template<typename T>
    void setParentActualTypeSize() {
    	parentActualTypeSize_ = sizeof(T);
    }

    void setParentOffset(size_t offset) {
    	parentOffset_ = offset;
    }

	size_t getActualTypeSize() {
    	return actualTypeSize_;
    }

	size_t getParentActualTypeSize() {
    	return parentActualTypeSize_;
    }

	size_t getParentOffset() {
    	return parentOffset_;
    }

	void findNode(const string& name, cv::Ptr<Node>& found) {
    	CV_Assert(!name.empty());
    	if(currentNodes_.empty())
    		return;

    	if(currentNodes_.back()->name_ == name)
    		found = currentNodes_.back();
    }

    void makeGraph() {
     	for(const auto& t : accesses_) {
     		const string& name = std::get<0>(t);

     		const bool& read = std::get<1>(t);
     		const size_t& dep = std::get<2>(t);
     		cv::Ptr<Node> n;
     		findNode(name, n);

     		if(!n) {
     			n = new Node();
     			n->name_ = name;
     			n->tx_ = transactions_[name];
     			CV_Assert(!n->name_.empty());
     			CV_Assert(n->tx_);
     			currentNodes_.push_back(n);
      		}


     		if(read) {
     			n->read_deps_.insert(dep);
     		} else {
     			n->write_deps_.insert(dep);
     		}
     	}
     }

    virtual void pf(const size_t& depth, const BranchState& current, const cv::Ptr<Node> n) {
    	CV_UNUSED(depth);
    	CV_UNUSED(current);
    	CV_UNUSED(n);
    }

	virtual void runGraph() {
		BranchType::Enum btype;
     	BranchState currentState;
		try {
			for (auto& n : currentNodes_) {
				btype = n->tx_->getBranchType();
				bool isBranch = n->name_.substr(0, 6) == "branch";
				bool isElse = n->name_.substr(0,6) == "[else]";
				bool isEnd = n->name_.substr(0,5) == "[end]";
				bool isElseIf = n->name_.substr(0,8) == "[elseif]";
				if(btype != BranchType::NONE) {
					CV_Assert((((isBranch != isElse) != isEnd) != isElseIf));
					if(isBranch) {
						if(!branchStateStack_.empty())
							currentState = branchStateStack_.front();
						else
							currentState = BranchState();
						currentState.branchID_ = n->name_;

						if(currentState.isEnabled_) {
							currentState.isOnce_ = ((btype == BranchType::ONCE) || (btype == BranchType::PARALLEL_ONCE));
							currentState.isSingle_ = ((btype == BranchType::ONCE) || (btype == BranchType::SINGLE));
						} else {
							currentState.isOnce_ = false;
							currentState.isSingle_ = false;
							currentState.isEnabled_ = false;
						}

						if(currentState.isEnabled_) {
							if(currentState.isOnce_) {
								if((btype == BranchType::ONCE)) {
									currentState.condition_ = GlobalState::once(n->name_) && n->tx_->performPredicate();
								} else if((btype == BranchType::PARALLEL_ONCE)) {
									currentState.condition_ = !n->tx_->ran() && n->tx_->performPredicate();
								} else {
									CV_Assert(false);
								}
							} else {
								currentState.condition_ = n->tx_->performPredicate();
							}

							currentState.isEnabled_ = currentState.isEnabled_ && currentState.condition_;

							if(currentState.isEnabled_ && currentState.isSingle_) {
								CV_Assert(btype != BranchType::PARALLEL);

								GlobalState::lockNode(currentState.branchID_);

								currentState.isLocked_ = true;
							}
						}
						branchStateStack_.push_front(currentState);
						pf(branchStateStack_.size(), currentState, n);
					} else if(isElse) {
						if(branchStateStack_.empty())
							continue;
						currentState = branchStateStack_.front();
						currentState.isEnabled_ = !currentState.condition_;
						currentState.isOnce_ = false;
						currentState.condition_ = !currentState.condition_;
						currentState.isSingle_ = false;

						if(currentState.isLocked_) {
						    GlobalState::tryUnlockNode(currentState.branchID_);
						}

						currentState.isLocked_ = false;
						pf(branchStateStack_.size(), currentState, n);
						branchStateStack_.pop_front();
						branchStateStack_.push_front(currentState);
					} else if(isEnd) {
						if(branchStateStack_.empty())
							continue;

						currentState = branchStateStack_.front();
						GlobalState::tryUnlockNode(currentState.branchID_);
						pf(branchStateStack_.size(), currentState, n);
						branchStateStack_.pop_front();
					} else {
						CV_Assert(false);
					}
				} else {
					CV_Assert(!n->tx_->isPredicate());
					currentState = !branchStateStack_.empty() ? branchStateStack_.front() : BranchState();
					if(currentState.isEnabled_) {
						auto lock = GlobalState::tryGetNodeLock(currentState.branchID_);
						auto plan = self<Plan>();
						auto ctx = n->tx_->getContextCallback()();
						auto viewport = runtime_->getViewport();

						if(lock)
						{
							std::lock_guard<std::mutex> guard(*lock.get());
							int res = ctx->execute(viewport, [plan, n]() {
								n->tx_->perform();
							});
							if(res <= 0) {
							    CV_LOG_WARNING(nullptr, "Context failed while: " + n->name_);
							}
						} else {
							int res = ctx->execute(viewport, [plan, n]() {
								n->tx_->perform();
							});
							if(res <= 0) {
								CV_LOG_WARNING(nullptr, "Context failed while: " + n->name_);
							}
						}
					}
					pf(branchStateStack_.size() +1 , currentState, n);
					currentState = BranchState();
				}
			}

			size_t lockCnt = GlobalState::countNodeLocks();
			CV_Assert(branchStateStack_.empty());
			CV_Assert(lockCnt == 0);
    	} catch(std::runtime_error& ex) {
			if(!branchStateStack_.empty() && branchStateStack_.front().isLocked_) {
				if(GlobalState::tryUnlockNode(currentState.branchID_)) {
				}
			}
			throw;
		} catch(std::exception& ex) {
			CV_UNUSED(ex);
			if(!branchStateStack_.empty() && branchStateStack_.front().isLocked_) {
				if(GlobalState::tryUnlockNode(currentState.branchID_)) {
				}
			}
			throw;
		} catch(...) {
			if(!branchStateStack_.empty() && branchStateStack_.front().isLocked_) {
				if(GlobalState::tryUnlockNode(currentState.branchID_)) {
				}
			}
			throw std::runtime_error("Unkown error.");
		}
	}

	void clearGraph() {
		std::copy(currentNodes_.begin(), currentNodes_.end(), std::back_inserter(allNodes_));
		accesses_.clear();
		branchStateStack_.clear();
		branchStack_.clear();
		transactions_.clear();
		currentNodes_.clear();
	}

	template<typename Tinstance>
    cv::Ptr<Tinstance> self() {
		if(!self_)
			self_ = this;
		return self_.dynamicCast<Tinstance>();
	}

    template<typename Tplan, typename Tparent, typename ... Args>
	static cv::Ptr<Tplan> makeSubPlan(Tparent* parent, Args&& ... args) {
    	Tplan* plan = new Tplan(std::forward<Args>(args)...);
    	plan->setParentID(parent->space());
    	plan->setParentOffset(reinterpret_cast<size_t>(parent));
    	plan->template setParentActualTypeSize<Tparent>();
    	plan->template setActualTypeSize<Tplan>();
		return plan->template self<Tplan>();
    }

    template<typename Tfn, typename ... Args>
    const string make_id(string id, const string& name, Tfn fn, Args ... args) {
    	std::stringstream ss;
    	if(!id.empty())
    		id = "::" + id;

    	if constexpr(std::is_pointer<Tfn>::value) {
    		ss << name << id << " [" << detail::int_to_hex(reinterpret_cast<size_t>(fn)) << "] ";
    	} else {
    		ss << name << id << " [" << detail::lambda_ptr_hex(std::forward<Tfn>(fn)) << "] ";
    	}

    	((ss << demangle(typeid(typename std::remove_reference_t<decltype(args)>::ref_t).name()) << "(" << int_to_hex(args.id()) << ") "), ...);
    	ss << "- " <<  map_index(std::this_thread::get_id());
    	while(transactions_.find(ss.str()) != transactions_.end()) {
    				ss << '+';
    	}
    	return ss.str();
    }
public:

    template<typename T>
	struct Property : detail::Edge<const T, false, true, true> {
		using parent_t = detail::Edge<const T, false, true, true>;
		Property(cv::Ptr<Plan> plan, const T& val) : parent_t(parent_t::make(plan, val)) {
			GlobalState::shared_vars().makeSharedVar(val);
		}
	};

	template<typename TeventClass, typename Tfn = std::function<std::vector<std::shared_ptr<TeventClass>>()>, typename Tparent = Edge<Tfn, false, true, false>>
	struct Event : Tparent {
		Event(cv::Ptr<Plan> plan) : Tparent(Tparent::make(plan, wrap_callable<>([]() {
			return typename TeventClass::List();
		}))) {
			static_assert(Tparent::func_t::value, "Internal error: Function not recognized!");
		}

		Event(cv::Ptr<Plan> plan, const typename TeventClass::Type t) : Tparent(Tparent::make(plan, wrap_callable<>([t]() {
			return typename TeventClass::List();
		}))) {
			static_assert(Tparent::func_t::value, "Internal error: Function not recognized!");
		}

		template<typename Ttrigger>
		Event(cv::Ptr<Plan> plan, const typename TeventClass::Type t, const Ttrigger tr) : Tparent(Tparent::make(plan, wrap_callable<>([t, tr]() {
			return typename TeventClass::List();
		}))) {
			static_assert(Tparent::func_t::value, "Internal error: Function not recognized!");
		}
	};

	//predefined branch predicates
	constexpr static auto always_ = []() { return true; };
	constexpr static auto isTrue_ = [](const bool& b) { return b; };
	constexpr static auto isFalse_ = [](const bool& b) { return !b; };
	constexpr static auto and_ = [](const bool& a, const bool& b) { return a && b; };
	constexpr static auto or_ = [](const bool& a, const bool& b) { return a || b; };

	Plan() {
		if(PlanRuntime::current())
			runtime_ = PlanRuntime::current();
	}

	virtual ~Plan() { self_ = nullptr; };
	virtual void gui() { };
	virtual void setup() { };
	virtual void infer() = 0;
	virtual void teardown() { };

	virtual std::string space() {
		if(!parent_.empty()) {
			return parent_ + "-" + name();
		} else
			return name();
	}

	virtual std::string name() {
		return detail::demangle(typeid(*this).name());
	}

	virtual void setParentID(const string& parent) {
		parent_  = parent;
	}

	virtual std::string getParentID() {
		return parent_;
	}

    template <typename Tctx, typename Tfn, typename Tuple, size_t ... idx>
    cv::Ptr<Plan> call(Tctx ctx, const string& name, Tfn fn, Tuple&& args, std::index_sequence<idx...>) {
		const string id = make_id(this->space(), name, fn, std::get<idx>(args)...);
		emit_access(id, R(*this));
		(emit_access(id, std::get<idx>(args) ),...);
		auto wrap = wrap_callable<typename std::remove_reference<decltype(std::get<idx>(args))>::type::ref_t...>(fn);
		add_transaction(ctx,id, wrap, std::get<idx>(args)...);
		return self<Plan>();
	}

    template <typename Tedge>
    typename std::enable_if<std::is_base_of_v<EdgeBase, Tedge>, cv::Ptr<Plan>>::type
    branch(Tedge edge) {
        auto wrap = wrap_callable<typename Tedge::ref_t>([](const bool& b){ return b; });
        const string id = make_id(this->space(), "branch", wrap);
        branchStack_.push_front({id, BranchType::PARALLEL});
        emit_access(id, R(*this));
		add_transaction(BranchType::PARALLEL, runtime_->plainCtx(), id, wrap, edge);
		return self<Plan>();
    }

    template <typename Tfn>
    typename std::enable_if<!std::is_base_of_v<EdgeBase, Tfn>, cv::Ptr<Plan>>::type
    branch(Tfn fn) {
        auto wrap = wrap_callable(fn);
        const string id = make_id(this->space(), "branch", fn);
        branchStack_.push_front({id, BranchType::PARALLEL});
        emit_access(id, R(*this));
		add_transaction(BranchType::PARALLEL, runtime_->plainCtx(), id, wrap);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    typename std::enable_if<!std::is_integral_v<Tfn>, cv::Ptr<Plan>>::type
    branch(Tfn fn, Args ... args) {
        auto wrap = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "branch", fn, args...);
        branchStack_.push_front({id, BranchType::PARALLEL});
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		add_transaction(BranchType::PARALLEL, runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    typename std::enable_if<!std::is_base_of<EdgeBase, Tfn>::value, cv::Ptr<Plan>>::type
    branch(int workerIdx, Tfn fn, Args ... args) {
        auto wrapInner = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "branch-pin" + std::to_string(workerIdx), fn, args...);
        branchStack_.push_front({id, BranchType::PARALLEL});
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		std::function<bool((typename Args::ref_t...))> wrap = [this, workerIdx, wrapInner](typename Args::ref_t ... innerArgs){
			return LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX) == workerIdx && wrapInner(innerArgs...);
		};
		add_transaction(BranchType::PARALLEL, runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    cv::Ptr<Plan> branch(int workerIdx, BranchType::Enum type, Tfn fn, Args ... args) {
        auto wrapInner = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "branch-pin" + std::to_string(workerIdx), fn, args...);
        branchStack_.push_front({id, BranchType::PARALLEL});
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
        std::function<bool((typename Args::ref_t...))> wrap = [this, workerIdx, wrapInner](typename Args::ref_t ... innerArgs){
return LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX) == static_cast<size_t>(workerIdx) && wrapInner(innerArgs...);
        };
        add_transaction(type, runtime_->plainCtx(), id, wrap, args...);
        return self<Plan>();
    }

    template <typename Tedge>
    typename std::enable_if<std::is_base_of_v<EdgeBase, Tedge>, cv::Ptr<Plan>>::type
    branch(BranchType::Enum type, Tedge edge) {
        auto wrap = wrap_callable<typename Tedge::ref_t>([](const bool& b){ return b; });
        const string id = make_id(this->space(), "branch", wrap);
        branchStack_.push_front({id, type});
        emit_access(id, R(*this));
		add_transaction(type, runtime_->plainCtx(), id, wrap, edge);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    cv::Ptr<Plan> branch(BranchType::Enum type, Tfn fn, Args ... args) {
        auto wrap = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "branch-type" + std::to_string((int)type), fn, args...);
        branchStack_.push_front({id, type});
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		add_transaction(type, runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    cv::Ptr<Plan> branch(BranchType::Enum type, int workerIdx, Tfn fn, Args ... args) {
        auto wrapInner = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "branch-type-pin" + std::to_string((int)type) + "-" + std::to_string(workerIdx), fn, args...);
        branchStack_.push_front({id, type});
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		std::function<bool((typename Args::ref_t...))> wrap = [this, workerIdx, wrapInner](typename Args::ref_t ... innerArgs){
			return LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX) == workerIdx && wrapInner(innerArgs...);
		};
		add_transaction(type, runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

	cv::Ptr<Plan> endBranch() {
    	auto current = branchStack_.front();
    	branchStack_.pop_front();
        string id = "[end]" + current.first;
        emit_access(id, R(*this));
        std::function functor = [](){ return true; };
		add_transaction(current.second, runtime_->plainCtx(), id, functor);
		return self<Plan>();
    }

    cv::Ptr<Plan> elseBranch() {
    	auto current = branchStack_.front();
    	string id = "[else]" + current.first;
    	emit_access(id, R(*this));
		std::function functor = [](){ return true; };
		add_transaction(current.second, runtime_->plainCtx(), id, functor);
		return self<Plan>();
    }

    template <typename Tfn, typename Tuple, size_t ... idx>
    auto wrapGuiCall(Tfn fn, Tuple&& args, std::index_sequence<idx...>) {
        return wrap_callable<typename std::remove_reference<decltype(std::get<idx>(args))>::type::ref_t...>(fn);
    }

    template <typename ... Args>
    cv::Ptr<Plan> plain(Args... args) {
        auto fn = [](typename Args::ref_t ...){};
        auto wrap = wrap_callable<typename Args::ref_t ...>(fn);
        const string id = make_id(this->space(), "plain", wrap, args...);
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		add_transaction(runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

    template <typename Tfn, typename ... Args>
    typename std::enable_if<!std::is_base_of<EdgeBase, Tfn>::value, cv::Ptr<Plan>>::type
    plain(Tfn fn, Args... args) {
        auto wrap = wrap_callable<typename Args::ref_t ...>(fn);

        const string id = make_id(this->space(), "plain", fn, args...);
        emit_access(id, R(*this));
        (emit_access(id, args ),...);
		add_transaction(runtime_->plainCtx(), id, wrap, args...);
		return self<Plan>();
    }

    template <typename TsubPlan>
    cv::Ptr<Plan> subInfer(cv::Ptr<TsubPlan> subPlan) {
    	subPlan->infer();
    	subPlan->makeGraph();
    	std::copy(subPlan->accesses_.begin(), subPlan->accesses_.end(), std::inserter(accesses_, accesses_.end()));
    	std::copy(subPlan->transactions_.begin(), subPlan->transactions_.end(), std::inserter(transactions_, transactions_.end()));
    	subPlan->clearGraph();
    	return self<Plan>();
    }

    template <typename TsubPlan>
    cv::Ptr<Plan> subSetup(cv::Ptr<TsubPlan> subPlan) {
    	subPlan->setup();
    	subPlan->makeGraph();
    	std::copy(subPlan->accesses_.begin(), subPlan->accesses_.end(), std::inserter(accesses_, accesses_.end()));
    	std::copy(subPlan->transactions_.begin(), subPlan->transactions_.end(), std::inserter(transactions_, transactions_.end()));
    	subPlan->clearGraph();
    	return self<Plan>();
    }

    template <typename TsubPlan>
    cv::Ptr<Plan> subTeardown(cv::Ptr<TsubPlan> subPlan) {
    	subPlan->teardown();
      	subPlan->makeGraph();
    	std::copy(subPlan->accesses_.begin(), subPlan->accesses_.end(), std::inserter(accesses_, accesses_.end()));
    	std::copy(subPlan->transactions_.begin(), subPlan->transactions_.end(), std::inserter(transactions_, transactions_.end()));
    	subPlan->clearGraph();
    	return self<Plan>();
    }

    template<bool TmakeEdge = true, typename TfnOp, typename ... Args>
	auto make_op(TfnOp fn, Args ... args) {
		auto op = wrap_callable<typename Args::ref_t ...>(fn);
		using ret_t = typename CallableTraits<decltype(op)>::return_type_t;
		constexpr bool hasReturn = !std::is_same<ret_t, void>::value;
		using ret_no_ref_t = typename std::remove_reference<ret_t>::type;
		static_assert(!std::is_same<ret_no_ref_t, std::false_type>::value, "Invalid callable passed to Plan::op");

		using val_t = typename std::disjunction<
						values_equal<hasReturn, true, typename std::remove_pointer<ret_no_ref_t>::type>,
						default_type<int>
					>::type;


		if constexpr(hasReturn && TmakeEdge) {
			cv::Ptr<cv::Ptr<val_t>> retPtr = new cv::Ptr<val_t>(cv::Ptr<val_t>(), nullptr);
			std::function wrap = [op](cv::Ptr<val_t>& v, typename Args::ref_t ... values) mutable {
				if constexpr(std::is_pointer<ret_no_ref_t>::value) {
					v = cv::Ptr<val_t>(cv::Ptr<val_t>(),op(values...));
				} else if constexpr(std::is_lvalue_reference<ret_t>::value) {
					auto& ref = op(values...);
					v = cv::Ptr<val_t>(cv::Ptr<val_t>(),std::addressof(ref));
				} else {
					v = cv::Ptr<val_t>(cv::Ptr<val_t>(),new val_t(op(values...)));
				}
			};

			const string id = make_id(this->space(), "nary-op", wrap, args...);
			emit_access(id, R(*this));
			(emit_access(id, args ),...);

			auto ptrEdge = detail::Edge<cv::Ptr<cv::Ptr<val_t>>, false, false, false, cv::Ptr<val_t>, true>::make(self<Plan>(), retPtr);
			add_transaction(runtime_->plainCtx(), id, wrap, ptrEdge, args...);

			return detail::Edge<cv::Ptr<val_t>, false, false, false, val_t>::make(self<Plan>(), *retPtr.get());
		} else {
			std::function wrap = [op](typename Args::ref_t ... values) {
				op(values...);
			};

			const string id = make_id(this->space(), "nary-op", wrap, args...);
			emit_access(id, R(*this));
			(emit_access(id, args ),...);
			add_transaction(runtime_->plainCtx(), id, wrap, args...);
			return self<Plan>();
		}
	}

	template<Operators Top, typename ... Edges>
	cv::Ptr<Plan> op(Edges ... edges){
		return make_op<false>(make_operator_func<check_op<Top, Edges...>::value>(edges...), edges...);
	}

	template<typename ... Edges>
	cv::Ptr<Plan> assign(Edges ... edges){
		return make_op<false>(make_operator_func<check_op<ASSIGN_, Edges...>::value>(edges...), edges...);
	}

	template<typename ... Edges>
	cv::Ptr<Plan> construct(Edges ... edges){
		return make_op<false>(make_operator_func<check_op<CONSTRUCT_, Edges...>::value>(edges...), edges...);
	}

	template<Operators Top, typename ... Edges>
	auto OP(Edges ... edges){
		return make_op(make_operator_func<check_op<Top, Edges...>::value>(edges...), edges...);
	}

	template<typename ... Edges>
	auto operator()(Edges&& ... edges){
		return OP<Operators::CONSTRUCT_>(edges...);
	}

	template<typename ... Edges>
	auto IF(Edges&& ... edges){
		return OP<Operators::IF_>(edges...);
	}

	template<typename ... Edges>
	auto ASSIGN(Edges&& ... edges){
		return OP<Operators::ASSIGN_>(edges...);
	}

	template<typename ... Edges>
	auto ADD(Edges&& ... edges){
		return OP<Operators::ADD_>(edges...);
	}

	template<typename ... Edges>
	auto SUB(Edges&& ... edges){
		return OP<Operators::SUB_>(edges...);
	}

	template<typename ... Edges>
	auto MUL(Edges&& ... edges){
		return OP<Operators::MUL_>(edges...);
	}

	template<typename ... Edges>
	auto DIV(Edges&& ... edges){
		return OP<Operators::DIV_>(edges...);
	}

	template<typename ... Edges>
	auto MOD(Edges&& ... edges){
		return OP<Operators::MOD_>(edges...);
	}

	template<typename ... Edges>
	auto INCL(Edges&& ... edges){
		return OP<Operators::INCL_>(edges...);
	}

	template<typename ... Edges>
	auto INCR(Edges&& ... edges){
		return OP<Operators::INCR_>(edges...);
	}

	template<typename ... Edges>
	auto DECL(Edges&& ... edges){
		return OP<Operators::DECL_>(edges...);
	}

	template<typename ... Edges>
	auto DECR(Edges&& ... edges){
		return OP<Operators::DECR_>(edges...);
	}

	template<typename ... Edges>
	auto AND(Edges&& ... edges){
		return OP<Operators::AND_>(edges...);
	}

	template<typename ... Edges>
	auto OR(Edges&& ... edges){
		return OP<Operators::OR_>(edges...);
	}

	template<typename ... Edges>
	auto EQ(Edges&& ... edges){
		return OP<Operators::EQ_>(edges...);
	}

	template<typename ... Edges>
	auto NEQ(Edges&& ... edges){
		return OP<Operators::NEQ_>(edges...);
	}

	template<typename ... Edges>
	auto LT(Edges&& ... edges){
		return OP<Operators::LT_>(edges...);
	}

	template<typename ... Edges>
	auto GT(Edges&& ... edges){
		return OP<Operators::GT_>(edges...);
	}

	template<typename ... Edges>
	auto LE(Edges&& ... edges){
		return OP<Operators::LE_>(edges...);
	}

	template<typename ... Edges>
	auto GE(Edges&& ... edges){
		return OP<Operators::GE_>(edges...);
	}

	template<typename ... Edges>
	auto NOT(Edges&& ... edges){
		return OP<Operators::NOT_>(edges...);
	}

	template<typename ... Edges>
	auto XOR(Edges&& ... edges){
		return OP<Operators::XOR_>(edges...);
	}

	template<typename ... Edges>
	auto BAND(Edges&& ... edges){
		return OP<Operators::BAND_>(edges...);
	}

	template<typename ... Edges>
	auto BOR(Edges&& ... edges){
		return OP<Operators::BOR_>(edges...);
	}


	template<typename ... Edges>
	auto SHL(Edges&& ... edges){
		return OP<Operators::SHL_>(edges...);
	}

	template<typename ... Edges>
	auto SHR(Edges&& ... edges){
		return OP<Operators::SHR_>(edges...);
	}

	template<typename ... Edges>
	auto DEREF(Edges&& ... edges){
		return OP<Operators::DEREF_>(edges...);
	}

	template<typename ... Edges>
	auto IDX(Edges&& ... edges){
		return OP<Operators::IDX_>(edges...);
	}

	template<typename ... Edges>
	auto NEG(Edges&& ... edges){
		return OP<Operators::NEG_>(edges...);
	}

	template<typename Tfn, typename ... Args>
	auto F(Tfn src, Args&& ... args) {
		return make_op(src, args...);
	}

	template<typename ... Args>
	auto _(Args&& ... args) {
		return std::make_tuple(std::forward<const Args>(args)...);
	}

	template<typename TsubPlan, typename Tparent, typename ... Args>
	auto _sub(Tparent* parent, Args&& ... args) {
		return Plan::makeSubPlan<TsubPlan>(parent, std::forward<Args>(args)...);
	}

	template<typename TsubPlan, typename TparentPtr, typename ... Args>
	auto _sub(TparentPtr parent, Args&& ... args) {
		return Plan::makeSubPlan<TsubPlan>(parent.get(), std::forward<Args>(args)...);
	}

        template<typename Tvar>
        void _shared(Tvar& val) {
            GlobalState::shared_vars().makeSharedVar(val);
        }

	template<typename Tvar>
	void _safe(Tvar& val) {
		GlobalState::shared_vars().registerSafe(val);
	}

       template<typename Tedge>
       cv::Ptr<Plan> set(const GlobalState::Keys::Enum& key, const Tedge& e) {
           auto plan = self<Plan>();
           using TE = std::remove_const_t<Tedge>;
           using ref_t = decltype(std::declval<TE>().ref());
           std::function<void(ref_t)> fn = [plan, key](ref_t v){
               GlobalState::set(key, v);
           };
            const string id = make_id(this->space(), "set-fn", fn, e);
            emit_access(id, R(*this));
            emit_access(id, e);
            add_transaction(runtime()->plainCtx(), id, fn, e);
            return self<Plan>();
        }

        template<typename ... Args>
        cv::Ptr<Plan> set(std::tuple<GlobalState::Keys::Enum,Args>&& ... tuples) {
            (set(std::forward<std::tuple<GlobalState::Keys::Enum,Args>>(tuples)),...);
            return self<Plan>();
        }

        template<typename T>
	detail::Edge<T, false, true> R(const T& t) {
		return detail::Edge<T, false, true>::make(self<Plan>(), t);
	}

	template<typename T>
	detail::Edge<T, false, true, true> RS(const T& t) {
		if(!GlobalState::shared_vars().checkShared(*this, t)) {
			throw std::runtime_error("You declare a non-shared variable as shared. Maybe you forgot to declare it?.");
		}
		return detail::Edge<T, false, true, true>::make(self<Plan>(), t);
	}

	template<typename T>
	detail::Edge<T, false, false> RW(T& t) {
		return detail::Edge<T, false, false>::make(self<Plan>(), t);
	}

	template<typename T>
	detail::Edge<T, false, false, true> RWS(T& t) {
		if(!GlobalState::shared_vars().checkShared(*this, t)) {
			throw std::runtime_error("You declare a non-shared variable as shared. Maybe you forgot to declare it?.");
		}
		return detail::Edge<T, false, false, true>::make(self<Plan>(), t);
	}

	template<typename T>
	detail::Edge<T, true, true, true> CS(T& t) {
		if(GlobalState::shared_vars().checkShared(*this, t)) {
			return detail::Edge<T, true, true, true>::make(self<Plan>(), t);
		} else {
			throw std::runtime_error("You are trying to safe-copy a non-shared variable. Maybe you forgot to declare it?.");
		}
	}

	template<typename T>
	detail::Edge<cv::Ptr<T>, false, true, false, T, true> V(T t) {
		auto ptr = cv::makePtr<T>(t);
		return detail::Edge<decltype(ptr), false, true, false, T, true>::make(self<Plan>(), ptr);
	}

	template<typename Tval>
	Property<Tval> P(GlobalState::Keys::Enum key) {
		const auto& ref = GlobalState::get<Tval>(key);
		return Property<Tval>(self<Plan>(), ref);
	}

	template<typename Tval>
	Property<Tval> P(LocalState::Keys::Enum key) {
		const auto& ref = LocalState::get<Tval>(key);
		return Property<Tval>(self<Plan>(), ref);
	}

	template<typename Tclass>
	Event<Tclass> E() {
		return Event<Tclass>(self<Plan>());
	}

	template<typename Tclass>
	Event<Tclass> E(typename Tclass::Type t) {
		return Event<Tclass>(self<Plan>(), t);
	}

	template<typename Tclass, typename Ttrigger>
	Event<Tclass> E(typename Tclass::Type t, Ttrigger tr) {
		return Event<Tclass>(self<Plan>(), t, tr);
	}

	template<typename Tplan, typename ... Args>
	static cv::Ptr<Tplan> make(Args&& ... args) {
    	Tplan* plan = new Tplan(std::forward<Args>(args)...);
    	plan->template setActualTypeSize<Tplan>();
		return plan->template self<Tplan>();
	}

	/*!
	 * Run the plan (§1.5 of the Plan-DSL reference). Orchestrates the complete
	 * per-worker lifecycle of the task graph:
	 *
	 *   spawn workers (each recursively calls run<Tplan>(0, ...))
	 *   setup()  -> makeGraph() -> runGraph() -> clearGraph()
	 *   infer()  -> makeGraph()
	 *   barrier
	 *   frame loop: runtime()->runFrameLoop(plan) re-executes the graph
	 *               every frame/iteration
	 *   teardown() -> makeGraph() -> runGraph() -> clearGraph()
	 *
	 * Runtime specifics are delegated to the PlanRuntime hooks.
	 */
	template<typename Tplan, typename ... Args>
	static void run(int32_t extra_workers, Args&& ... args) {
		CV_Assert(extra_workers >= -1);
		// Contract:
		//   extra_workers == -1 -> 1 worker + display thread, cv::setNumThreads(0)
		//   extra_workers ==  0 -> 1 worker + display thread, cv::setNumThreads(-1)
		//   extra_workers ==  n -> n workers + display thread, cv::setNumThreads(0)
		const int32_t workers = (extra_workers <= 0) ? 1 : extra_workers;

		cv::Ptr<Tplan> plan;
		std::vector<std::thread*> threads;
		{
			static std::mutex runMtx;
			std::lock_guard<std::mutex> lock(runMtx);

			GlobalState::init_keys();
			LocalState::init_keys();
			cv::setNumThreads(extra_workers == 0 ? -1 : 0);


			if(GlobalState::isFirstRun()) {
				GlobalState::setMainID(std::this_thread::get_id());
				CV_LOG_INFO(nullptr, "Starting with " << workers << " workers");
			}

			plan = Plan::make<Tplan>(std::forward<Args>(args)...);

			if(!plan->runtime_ && PlanRuntime::current())
				plan->setRuntime(PlanRuntime::current());
			CV_Assert(plan->runtime_);

			if(GlobalState::isMain()) {
				GlobalState::set<size_t>(GlobalState::Keys::WORKERS_STARTED, workers);
				// Decay-copy the arguments once: every worker needs its own
				// copy, so the per-thread capture below must not move out of
				// the argument pack.
				auto argsCopy = std::make_tuple(args...);
				for (int32_t i = 0; i < workers; ++i) {
					threads.push_back(
						// Each worker constructs its own plan instance inside the
						// thread (after the runtime hook registered the thread's
						// runtime), because a plan and its transactions are bound
						// to the contexts of the runtime it was created with.
						new std::thread([rt = plan->runtime(), i, argsTuple = argsCopy]() mutable {
							GlobalState::init_keys();
							LocalState::init_keys();
							rt->initWorkerThread(i);
							if(!PlanRuntime::current())
								PlanRuntime::current() = rt;
							LocalState::set(LocalState::Keys::WORKER_INDEX, size_t(i));
							std::apply([](Args&& ... unpacked) {
								Plan::run<Tplan>(0, std::forward<decltype(unpacked)>(unpacked)...);
							}, std::move(argsTuple));
						})
					);
				}
			}
		}

		CV_Assert(plan);

		if(GlobalState::isMain()) {
			try {
				CV_LOG_DEBUG(nullptr, "Loading GUI");
				plan->runtime()->willGui(plan);
				plan->gui();
			} catch(std::exception& ex) {
				CV_Error_(cv::Error::StsError, ("Loading GUI failed: %s", ex.what()));
			}
		} else {
			static std::binary_semaphore setup_sema(1);
			try {
				CV_LOG_DEBUG(nullptr, "Setup on worker: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
				setup_sema.acquire();
				plan->setup();
				plan->makeGraph();
				plan->runGraph();
				plan->clearGraph();
				setup_sema.release();
			} catch(std::exception& ex) {
				CV_Error_(cv::Error::StsError, ("Setup failed: %s", ex.what()));
			}
			CV_LOG_DEBUG(nullptr, "Setup finished: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
		}

		if(GlobalState::isMain()) {
			CV_LOG_DEBUG(nullptr, "GUI loaded");
		} else {
			try {
				CV_LOG_DEBUG(nullptr, "Main inference on worker: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
				plan->infer();
				plan->makeGraph();
			} catch(std::exception& ex) {
				CV_Error_(cv::Error::StsError, ("Main inference failed: %s", ex.what()));
			}
			CV_LOG_DEBUG(nullptr, "Main inference finished: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
			GlobalState::apply<size_t>(GlobalState::Keys::WORKERS_READY, [](size_t& wr){ ++wr; return wr; });
		}

		static std::barrier syncPoint(std::ptrdiff_t(workers + 1));
		syncPoint.arrive_and_wait();

		try {
			plan->runtime()->runFrameLoop([plan]() {
				plan->runGraph();
			});
		} catch(std::exception& ex) {
			CV_Error_(cv::Error::StsError, ("Main runtime failed: %s", ex.what()));
		}

		if(!GlobalState::isMain()) {
			plan->clearGraph();
			CV_LOG_DEBUG(nullptr, "Starting teardown on worker: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
			try {
				plan->teardown();
				plan->makeGraph();
				plan->runGraph();
				plan->clearGraph();
			} catch(std::exception& ex) {
				CV_Error_(cv::Error::StsError, ("Pipeline teardown failed: %s", ex.what()));
			}
			plan->runtime()->releaseIo();
			CV_LOG_DEBUG(nullptr, "Teardown complete on worker: " << LocalState::get<size_t>(LocalState::Keys::WORKER_INDEX));
		} else {
			for(auto& t : threads)
				t->join();
			CV_LOG_INFO(nullptr, "All threads terminated.");
			plan->runtime()->releaseIo();
		}
	}

    cv::Ptr<PlanRuntime> runtime() const { return runtime_; }
    void setRuntime(cv::Ptr<PlanRuntime> rt) { runtime_ = rt; }
};

template<typename ... Edges>
auto operator+(const std::tuple<Edges...>& tuple){
	return Operation::op<ADD_>(tuple);
}

template<typename TedgeL, typename ... Edges>
auto operator+(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<ADD_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator+(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator+(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator+(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator+(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator+(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator+(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}


template<typename TedgeL, typename ... Edges>
auto operator*(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<MUL_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator*(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator*(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator*(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator*(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator*(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator*(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator-(const std::tuple<Edges...>& tuple){
	return Operation::op<SUB_>(tuple);
}

template<typename TedgeL, typename ... Edges>
auto operator-(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<SUB_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator-(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator-(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator-(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator-(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator-(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator-(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}
template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator-(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator*(rhs, std::make_tuple(std::forward<decltype(rhs.plan()->V(-1))>(rhs.plan()->V(-1))));
}

template<typename T>
auto operator-(const Plan::Property<T>& rhs){
	return operator*(rhs, std::make_tuple(std::forward<decltype(rhs.plan()->V(-1))>(rhs.plan()->V(-1))));
}

template<typename T>
auto operator-(const Plan::Event<T>& rhs){
	return operator*(rhs, std::make_tuple(std::forward<decltype(rhs.plan()->V(-1))>(rhs.plan()->V(-1))));
}


template<typename TedgeL, typename ... Edges>
auto operator/(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<DIV_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator/(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator/(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator/(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator/(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator/(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator/(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}
template<typename TedgeL, typename ... Edges>
auto operator%(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<MOD_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator%(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator%(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator%(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator%(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator%(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator%(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator++(const std::tuple<Edges...>& tuple){
	return Operation::op<INCL_>(tuple);
}

template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator++(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& e){
	return operator++(std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(e)));
}

template<typename T>
auto operator++(const Plan::Property<T>& rhs){
	return operator++(std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename T>
auto operator++(const Plan::Event<T>& rhs){
	return operator++(std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator++(const std::tuple<Edges...>& tuple, int){
	return Operation::op<INCR_>(tuple);
}

template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator++(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& e, int){
	return Operation::op<INCR_>(std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(e)));
}

template<typename T>
auto operator++(const Plan::Property<T>& rhs, int){
	return Operation::op<INCR_>(std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename T>
auto operator++(const Plan::Event<T>& rhs, int){
	return Operation::op<INCR_>(std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator--(const std::tuple<Edges...>& tuple){
	return Operation::op<DECL_>(tuple);
}

template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator--(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& e){
	return operator--(std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(e)));
}

template<typename T>
auto operator--(const Plan::Property<T>& rhs){
	return operator--(std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename T>
auto operator--(const Plan::Event<T>& rhs){
	return operator--(std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator--(const std::tuple<Edges...>& tuple, int){
	return Operation::op<DECR_>(tuple);
}

template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator--(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& e, int){
	return Operation::op<DECR_>(std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(e)));
}

template<typename T>
auto operator--(const Plan::Property<T>& rhs, int){
	return Operation::op<DECR_>(std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename T>
auto operator--(const Plan::Event<T>& rhs, int){
	return Operation::op<DECR_>(std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator&&(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<AND_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator&&(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator&&(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator&&(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator&&(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator&&(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator&&(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}


template<typename TedgeL, typename ... Edges>
auto operator||(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<OR_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator||(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator||(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator||(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator||(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator||(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator||(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator==(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<EQ_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator==(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator==(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator==(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator==(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator==(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator==(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator!=(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<NEQ_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator!=(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator!=(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator!=(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator!=(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator!=(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator!=(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}
template<typename TedgeL, typename ... Edges>
auto operator<(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<LT_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator<(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator<(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator<(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator<(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator>(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<GT_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator>(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator>(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator>(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator>(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}
template<typename TedgeL, typename ... Edges>
auto operator<=(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<LE_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator<=(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator<=(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<=(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator<=(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<=(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator<=(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator>=(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<GE_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator>=(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator>=(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>=(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator>=(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>=(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator>=(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename ... Edges>
auto operator!(const std::tuple<Edges...>& tuple){
	return Operation::op<NOT_>(tuple);
}

template<typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator!(const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& e){
	return operator!(std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(e)));
}

template<typename T>
auto operator!(const Plan::Property<T>& rhs){
	return operator!(std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename T>
auto operator!(const Plan::Event<T>& rhs){
	return operator!(std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator^(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<XOR_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator^(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator^(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator^(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator^(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator^(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator^(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator&(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<BAND_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator&(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator&(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator&(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator&(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator&(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator&(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator|(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<BOR_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator|(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator|(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator|(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator|(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator|(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator|(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator<<(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<SHL_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator<<(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator<<(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<<(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator<<(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator<<(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator<<(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

template<typename TedgeL, typename ... Edges>
auto operator>>(const TedgeL& lhs, const std::tuple<Edges...>& tuple){
	return Operation::op<SHR_>(std::tuple_cat(std::make_tuple(std::forward<const TedgeL>(lhs)), tuple));
}

template<typename TedgeL, typename Telement, bool Tcopy, bool Tread, bool Tshared, typename Tbase, bool TbyValue>
auto operator>>(const TedgeL& lhs, const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>& rhs){
	return operator>>(lhs, std::make_tuple(std::forward<const Edge<Telement, Tcopy, Tread, Tshared, Tbase, TbyValue>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>>(const TedgeL& lhs, const Plan::Property<T>& rhs){
	return operator>>(lhs, std::make_tuple(std::forward<const Plan::Property<T>>(rhs)));
}

template<typename TedgeL, typename T>
auto operator>>(const TedgeL& lhs, const Plan::Event<T>& rhs){
	return operator>>(lhs, std::make_tuple(std::forward<const Plan::Event<T>>(rhs)));
}

} /* namespace plan */
} /* namespace cv */

#endif /* OPENCV_PLAN_PLAN_HPP_ */
