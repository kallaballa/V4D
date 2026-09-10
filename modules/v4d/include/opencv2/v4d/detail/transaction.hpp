#ifndef MODULES_V4D_SRC_BACKEND_HPP_
#define MODULES_V4D_SRC_BACKEND_HPP_

#include "context.hpp"
#include "../util.hpp"
#include <tuple>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <type_traits>
#include <opencv2/core/types.hpp>

namespace cv {
namespace v4d {

class Plan;

namespace detail {

enum Operators {
	CONSTRUCT_,
	ASSIGN_,
	ADD_,
	SUB_,
	MUL_,
	DIV_,
	MOD_,
	INCL_,
	INCR_,
	DECL_,
	DECR_,
	AND_,
	OR_,
	EQ_,
	NEQ_,
	LT_,
	GT_,
	LE_,
	GE_,
	NOT_,
	XOR_,
	BAND_,
	BOR_,
	SHL_,
	SHR_,
	IF_,
	IDX_,
};

template<Operators Top, typename ... Edges>
struct check_op {
	static_assert(sizeof...(Edges) > 0);
	static constexpr Operators value = Top;
};


template<Operators Top, typename Tfirst, typename ... Args>
static auto make_operator_func(Tfirst, Args ...) {
	constexpr size_t numOperands = sizeof...(Args) + 1;
	constexpr bool unary = numOperands == 1;
	constexpr bool binary = numOperands == 2;
	constexpr bool ternary = numOperands == 3;
	constexpr bool nary = numOperands >= 2;

	if constexpr(Top == Operators::CONSTRUCT_) {
		constexpr bool isSmart = (!Tfirst::func_t::value && !Tfirst::byvalue_t::value && Tfirst::issmart_t::value);
		constexpr bool isPointer = Tfirst::ispointer_t::value;
		return [isSmart,isPointer](typename Tfirst::ref_t d, typename Args::ref_t ... values)  {
			if constexpr(isSmart) {
				d = typename Tfirst::value_type_t(new typename Tfirst::element_type_t(values...));
			} else if constexpr(isPointer) {
				d = new typename Tfirst::value_type_t(values...);
			} else {
				d = typename Tfirst::value_type_t(values...);
			}
			return d;
		};
	} else if constexpr(Top == Operators::ASSIGN_) {
		static_assert(binary, "Invalid number of arguments to ASSIGN");
		return [](typename Tfirst::ref_t l, typename Args::ref_t ... r)  -> decltype(l = (r + ...)) {
			auto tup = std::forward_as_tuple(r...);
			return l = std::get<0>(tup);
		};
	}  else if constexpr(Top == Operators::ADD_) {
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values)  -> decltype(f + (values + ...)){
			return f + (values + ...);
		};
	} else if constexpr(Top == Operators::SUB_) {
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f - (values - ...)) {
			return f - (values - ...);
		};
	} else if constexpr(Top == Operators::MUL_) {
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f * (values * ...)) {
			return f * (values * ...);
		};
	} else if constexpr(Top == Operators::DIV_) {
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values)  -> decltype(f / (values / ...)) {
			return f / (values / ...);
		};
	} else if constexpr(Top == Operators::MOD_) {
		static_assert(binary, "Invalid number of arguments to MOD");
		return [](typename Tfirst::ref_t l, typename Args::ref_t ... r) -> decltype(l % (r + ...)) {
			return l % (r + ...);
		};
	} else if constexpr(Top == Operators::INCL_) {
		static_assert(unary, "Invalid number of arguments to INCL");
		return [](typename Tfirst::ref_t f)   -> decltype(++f) {
			return ++f;
		};
	} else if constexpr(Top == Operators::INCR_) {
		static_assert(unary, "Invalid number of arguments to INCR");
		return [](typename Tfirst::ref_t f)  -> decltype(f++) {
			return f++;
		};
	} else if constexpr(Top == Operators::DECL_) {
		static_assert(unary, "Invalid number of arguments to DECL");
		return [](typename Tfirst::ref_t f)  -> decltype(--f) {
			return --f;
		};
	} else if constexpr(Top == Operators::DECR_){
		static_assert(unary, "Invalid number of arguments to DECR");
		return [](typename Tfirst::ref_t f)  -> decltype(f--) {
			return f--;
		};
	} else if constexpr(Top == Operators::AND_) {
		static_assert(nary, "Invalid number of arguments to AND");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f && (values && ...)) {
			return f && (values && ...);
		};
	} else if constexpr(Top == Operators::OR_) {
		static_assert(nary, "Invalid number of arguments to OR");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f || (values || ...)) {
			return f || (values || ...);
		};
	} else if constexpr(Top == Operators::EQ_) {
		static_assert(nary, "Invalid number of arguments to EQ");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f == (values == ...)) {
			return f == (values == ...);
		};

	} else if constexpr(Top == Operators::NEQ_) {
		static_assert(nary, "Invalid number of arguments to NEQ");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f != (values != ...)) {
			return f != (values != ...);
		};
	} else if constexpr(Top == Operators::LT_) {
		static_assert(nary, "Invalid number of arguments to LT");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f < (values < ...)) {
			return f < (values < ...);
		};
	} else if constexpr(Top == Operators::GT_) {
		static_assert(nary, "Invalid number of arguments to GT");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f > (values > ...)) {
			return f > (values > ...);
		};
	} else if constexpr(Top == Operators::LE_) {
		static_assert(nary, "Invalid number of arguments to LE");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f <= (values <= ...)) {
			return f <= (values <= ...);
		};
	} else if constexpr(Top == Operators::GE_) {
		static_assert(nary, "Invalid number of arguments to GE");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f >= (values >= ...)) {
			return f >= (values >= ...);
		};
	} else if constexpr(Top == Operators::NOT_) {
		static_assert(unary, "Invalid number of arguments to NOT");
		return [](typename Tfirst::ref_t f) -> decltype(!f) {
			return !f;
		};
	} else if constexpr(Top == Operators::XOR_) {
		static_assert(nary, "Invalid number of arguments to XOR");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f ^ (values ^ ...)) {
			return f ^ (values ^ ...);
		};
	} else if constexpr(Top == Operators::BAND_) {
		static_assert(nary, "Invalid number of arguments to BAND");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f & (values & ...)) {
			return f & (values & ...);
		};
	} else if constexpr(Top == Operators::BOR_) {
		static_assert(nary, "Invalid number of arguments to BOR");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f | (values | ...)) {
			return f | (values | ...);
		};
	} else if constexpr(Top == Operators::SHL_) {
		static_assert(nary, "Invalid number of arguments to SHL");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values)  -> decltype(f << (values << ...)) {
			return f << (values << ...);
		};
	} else if constexpr(Top == Operators::SHR_) {
		static_assert(nary, "Invalid number of arguments to SHR");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f >> (values >> ...)) {
			return f >> (values >> ...);
		};
	} else if constexpr(Top == Operators::IF_) {
		static_assert(ternary, "Invalid number of arguments to IF");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) {
			auto tup = std::forward_as_tuple(values...);
			auto& tr = std::get<0>(tup);
			auto& fa = std::get<1>(tup);
			return f ? tr : fa;
		};
	} else if constexpr(Top == Operators::IDX_) {
		static_assert(binary, "Invalid number of arguments to IDX");
		return [](typename Tfirst::ref_t f, typename Args::ref_t ... values) -> decltype(f[std::get<0>(std::forward_as_tuple(values...))]) {
			return f[std::get<0>(std::forward_as_tuple(values...))];
		};
	} else {
		static_assert(true, "Internal Error. Unkown operator value");
		return [](){};
	}
}

struct Operation {
	template<Operators TopEnum, typename Ttuple, size_t ... idx>
	static auto op(Ttuple operands, std::index_sequence<idx...>){
		static_assert(std::tuple_size<Ttuple>::value > 0);
		return std::get<0>(operands).plan()->template OP<TopEnum>(std::get<idx>(operands)...);
	}

	template<Operators TopEnum, typename Ttuple>
	static auto op(Ttuple operands){
		return op<TopEnum>(operands, std::make_index_sequence<std::tuple_size<Ttuple>::value>());
	}

};
class EdgeBase {};

template<typename T, bool Tcopy, bool Tread, bool Tshared = false, typename Tbase = std::false_type, bool TbyValue = false>
class Edge : public EdgeBase {
	template <typename, typename = void>
	struct has_deref_t : std::false_type {};

	template <typename Tval>
	struct has_deref_t<Tval, std::void_t<decltype(&Tval::operator*)>> : std::is_same<std::true_type, std::true_type>
	{};

	template <typename, typename = void>
	struct has_arrow_t : std::false_type {};

	template <typename Tval>
	struct has_arrow_t<Tval, std::void_t<decltype(&Tval::operator->)>> : std::is_same<std::true_type, std::true_type>
	{};

	template <typename, typename = void>
	struct has_get_t : std::false_type {};

	template <typename Tval>
	struct has_get_t<Tval, std::void_t<decltype(&Tval::get)>> : std::is_same<std::true_type, std::true_type>
	{};

public:
	using copy_t = std::integral_constant<bool, Tcopy>;
	using read_t = std::integral_constant<bool, Tread>;
	using value_type_t = typename std::disjunction<
 			values_equal<read_t::value, true, const typename std::remove_pointer<typename std::remove_extent<T>::type>::type>,
			default_type<typename std::remove_pointer<typename std::remove_extent<T>::type>::type>
 			>::type;
	using shared_t = std::integral_constant<bool, Tshared>;
	using deref_t = values_equal<std::is_same<Tbase, std::false_type>::value, false, std::true_type>;
	using byvalue_t = std::integral_constant<bool, TbyValue>;
	using lockie_t = values_equal<!copy_t::value && shared_t::value, true, std::true_type>;
	using func_t = values_equal<
					!std::is_same<typename return_t<value_type_t>::type, std::false_type>::value, true, std::true_type>;

 	using base_maybe_const_t = typename std::remove_reference<typename std::remove_pointer<typename std::remove_extent<value_type_t>::type>::type>::type;
	using base_t = typename std::remove_const<base_maybe_const_t>::type;
	using func_ret_t = typename return_t<base_t>::type;
	using element_type_t = typename std::disjunction<
								values_equal<std::is_same<typename element_t<base_t>::type, std::false_type>::value, false, typename element_t<base_t>::type>,
								default_type<T>
						>::type;

	using ispointer_t = values_equal<std::is_pointer<T>::value || std::is_array<T>::value, true, std::true_type>;

	using issmart_t = typename std::conjunction<
			has_deref_t<value_type_t>,
			has_arrow_t<value_type_t>,
			has_get_t<value_type_t>,
			values_equal<ispointer_t::value, false, std::true_type>,
			values_equal<func_t::value, false, std::true_type>
			>::type;

	using iselem_pointer_t = values_equal<std::is_pointer<element_type_t>::value || std::is_array<element_type_t>::value, true, std::true_type>;
private:
 	using iswriteable_func_t = typename std::conjunction<
			values_equal<std::is_reference<func_ret_t>::value, true, std::true_type>,
 			values_equal<std::is_const<func_ret_t>::value, false, std::true_type>
			>::type;
// 	static_assert(!read_t::value || !iswriteable_func_t::value, "You are trying to write to the return value of a function which is not writable and/or not a reference");
// 	static_assert((!ispointer_t::value) || !copy_t::value, "You are trying to explicitly copy the value of a (smart) pointer.");
//	static_assert((!temp_t::value) || (!copy_t::value && read_t::value), "Internal error: Trying to form a copy or write edge to a temporary.");
	static_assert(shared_t::value || !(copy_t::value && !read_t::value), "Internal error: Trying to form  copy-write edge on a non-shared variable.");
	static_assert(!lockie_t::value || !copy_t::value, "Internal error: Trying to form a copy edge on a to be locked variable.");


	using internal_base_t = typename std::disjunction<
			values_equal<func_t::value, true, func_ret_t>,
			values_equal<issmart_t::value, true, base_maybe_const_t>,
 			values_equal<ispointer_t::value, true, base_t>,
			default_type<value_type_t>
			>::type;

 	using internal_base_ptr_t = typename std::disjunction<
			default_type<internal_base_t*>
 			>::type;

 	using internal_copy_ptr_t = typename std::disjunction<
			values_equal<func_t::value, true, func_ret_t*>,
 			values_equal<issmart_t::value || deref_t::value, true, element_type_t*>,
			default_type<base_t*>
 			>::type;

 	using holder_t = typename std::disjunction<
 			values_equal<issmart_t::value, true, cv::Ptr<base_t>>,
 			values_equal<func_t::value, true, base_t>,
			default_type<nullptr_t>
			>::type;

 	cv::Ptr<Plan> plan_;
 	internal_base_ptr_t ptr_ = nullptr;
 	internal_copy_ptr_t copyPtr_ = nullptr;
	holder_t holder_ = nullptr;

	Edge(cv::Ptr<Plan> plan) : plan_(plan) {
	}
public:
	using pass_t = typename std::disjunction<
			values_equal<func_t::value, true, base_t>,
			values_equal<byvalue_t::value, true, base_t>,
			values_equal<issmart_t::value, true, base_t&>,
			values_equal<deref_t::value, true, base_t&>,
			values_equal<ispointer_t::value, true, internal_base_ptr_t>,
			default_type<value_type_t&>
			>::type;

	using ref_t = typename std::disjunction<
			values_equal<deref_t::value && iselem_pointer_t::value, true, Tbase>,
			values_equal<deref_t::value, true, Tbase&>,
						values_equal<ispointer_t::value && deref_t::value, true, base_t>,
			values_equal<ispointer_t::value, true, internal_base_ptr_t>,
			default_type<internal_base_t&>
			>::type;

	static Edge make(cv::Ptr<Plan> plan, pass_t t) {
		Edge e(plan);
		e.set(t);
		return e;
	}

	virtual cv::Ptr<Plan> plan() const {
		return plan_;
	}

	Edge clone() const {
		return *this;
	}

	void set(pass_t t) {
		if constexpr(issmart_t::value && byvalue_t::value) {
			holder_ = new base_t(t);
		} else if constexpr(issmart_t::value) {
			holder_ = &t;
		} else if constexpr(deref_t::value || func_t::value) {
			holder_ = t;
		}


		if constexpr(func_t::value && read_t::value) {
			ptr_ = new internal_base_t();
		} else if constexpr(ispointer_t::value) {
			ptr_ = t;
		} else if constexpr(byvalue_t::value) {
			ptr_ = holder_;
		} else if constexpr(deref_t::value) {
			ptr_ = holder_.get();
		} else if constexpr(issmart_t::value) {
			ptr_ = holder_.get();
		} else {
			ptr_ = &t;
		}


		if constexpr(copy_t::value) {
			copyPtr_ = new typename std::remove_pointer<internal_copy_ptr_t>::type();
		}
	}

	internal_base_ptr_t ptr() {
		if constexpr(func_t::value) {
			if constexpr(!read_t::value) {
				ptr_ = &holder_.operator()();
			} else {
				*ptr_ = holder_.operator()();
			}
		}

		CV_Assert(ptr_ != nullptr);
		return ptr_;
	}

	size_t id() {
		if constexpr(deref_t::value) {
			return reinterpret_cast<size_t>(&holder_);
		} else {

			return reinterpret_cast<size_t>(ptr());
		}
	}


	ref_t ref() {
		if constexpr(!copy_t::value) {
			if constexpr(ispointer_t::value) {
				return ptr_;
			} else if constexpr(deref_t::value) {
				return *ptr()->get();
			} else {
				return *ptr();
			}
		} else {
			if constexpr(issmart_t::value){
				if constexpr(shared_t::value) {
					Global::instance().safe_copy(*ptr()->get(), *copyPtr_.get());
					return copyPtr_;
				} else {
					Global::instance().copy(*ptr()->get(), *copyPtr_.get());
					return copyPtr_;
				}
			} else {
				if constexpr(shared_t::value) {
					Global::instance().safe_copy(*ptr(), *copyPtr_);
					return *copyPtr_;
				} else {
					Global::instance().copy(*ptr(), *copyPtr_);
					return *copyPtr_;
				}
			}
		}
	}

    void copyBack() {
    	if constexpr(!read_t::value && (copy_t::value || iswriteable_func_t::value)) {
    		if constexpr(shared_t::value) {
    			Global::instance().safe_copy(*copyPtr_, *ptr_);
    		} else {
    			Global::instance().copy(*copyPtr_, *ptr_);
    		}
    	}
    }

    std::mutex& getMutex() {
    	static_assert(lockie_t::value, "Internal Error: Trying to get mutex from a non-lockie edge");
    	//uses the no check variant because this should never fail due to previous checks.
    	return *Global::instance().getMutexPtr(*ptr(), true);
    }

    bool tryLock() {
    	return getMutex().try_lock();
    }

    bool unlock() {
    	getMutex().unlock();
    	return true;
    }

    template<typename ... Edges>
    auto operator=(const std::tuple<Edges...>& tuple){
    	return Operation::op<ASSIGN_>(tuple);
    }

    template<typename Tedge>
    auto operator=(const Tedge& rhs){
    	return operator=(std::make_tuple(*this,rhs));
    }

    template<typename ... Edges>
    auto operator[](const std::tuple<Edges...>& tuple){
    	return Operation::op<IDX_>(tuple);
    }

    template<typename Tedge>
    auto operator[](const Tedge& rhs){
    	return operator[](std::make_tuple(*this,rhs));
    }
};
}
struct BranchType {
	enum Enum {
	NONE = 0,
	SINGLE = 1,
	PARALLEL = 2,
	ONCE = 4,
	PARALLEL_ONCE = 8
	};
};

class CV_EXPORTS Transaction {
private:
	std::function<cv::Ptr<cv::v4d::detail::V4DContext>()> ctxCallback_;
	BranchType::Enum btype_;
public:
	CV_EXPORTS Transaction();
	CV_EXPORTS virtual ~Transaction() {}
	CV_EXPORTS virtual constexpr bool isPredicate() = 0;

	CV_EXPORTS virtual bool hasLockies() = 0;
	CV_EXPORTS virtual bool hasCopyBacks()  = 0;
	CV_EXPORTS virtual void perform() = 0;
	CV_EXPORTS virtual bool performPredicate() = 0;
	CV_EXPORTS virtual bool ran() = 0;

	CV_EXPORTS bool isBranch();
	CV_EXPORTS void setBranchType(BranchType::Enum btype);
	CV_EXPORTS BranchType::Enum getBranchType();
	CV_EXPORTS void setContextCallback(std::function<cv::Ptr<cv::v4d::detail::V4DContext>()> ctx);
	CV_EXPORTS std::function<cv::Ptr<cv::v4d::detail::V4DContext>()> getContextCallback();
};

namespace detail {

template <typename T>
auto filter_lockie(T& t) {
	if constexpr(T::lockie_t::value) {
		return std::make_tuple(t);
	} else {
		return std::tuple<>();
	}
}

template<typename Tuple, size_t... _Idx>
auto filter_lockies(Tuple t, std::index_sequence<_Idx...>) {
	return std::tuple_cat(filter_lockie(std::get<_Idx>(t))...);
}

template<typename Ttuple, size_t ... Tidx>
auto make_lock_guard_ptr_tuple(Ttuple& t,  std::index_sequence<Tidx...>) {
	return std::make_tuple<cv::Ptr<std::lock_guard<decltype(std::get<Tidx>(t).getMutex())>>...>(new std::lock_guard<decltype(std::get<Tidx>(t).getMutex())>(std::get<Tidx>(t).getMutex(), std::adopt_lock)...);
}

template<bool TcountContention = false, typename Ttuple, size_t ... Tidx>
auto perform_lock_from_tuple(Ttuple& t,  std::index_sequence<Tidx...>) {
	if constexpr(TcountContention) {
		size_t cnt = 0;
		(((std::get<Tidx>(t).tryLock() && std::get<Tidx>(t).unlock()) || ++cnt), ...);

		Global::instance().apply<size_t>(Global::Keys::LOCK_CONTENTION_CNT, [cnt](size_t& v){
			v += cnt;
			return v;
		});
	}
	std::lock(std::get<Tidx>(t).getMutex()...);
}
template <bool TcountContention, typename F, typename... Ts>
class TransactionImpl : public Transaction
{
    static_assert(sizeof...(Ts) == 0 || (!(std::is_rvalue_reference_v<Ts> && ...)));
private:
    F f;
    std::tuple<Ts...> args_;
    bool ran_;
    using predicate_t = std::is_same<std::remove_cv_t<typename decltype(f)::result_type>, bool>;
	static constexpr bool hasLockies_ = (std::remove_reference_t<Ts>::lockie_t::value || ...);
	static constexpr bool hasCopyBacks_ = (
			(
					std::remove_reference_t<Ts>::copy_t::value
					&& !std::remove_reference_t<Ts>::read_t::value
			) && ...);
public:


    template <typename FwdF, typename... FwdTs,
        typename = std::enable_if_t<sizeof...(Ts) == 0 || ((std::is_convertible_v<FwdTs&&, Ts> && ...))>>
		TransactionImpl(FwdF func, FwdTs... fwdArgs)
        : f(func),
          args_{std::make_tuple(fwdArgs...)},
		  ran_(false)
    {}

    virtual ~TransactionImpl() override
	{}

    virtual bool ran() override {
    	return ran_;
    }

    template <typename _Fn, typename _Tuple, size_t... _Idx>
    void
    performImpl(_Fn&& __f, _Tuple&& __t, std::index_sequence<_Idx...> seq) {
    	if constexpr(hasLockies_) {
    		auto lockies = filter_lockies(__t, seq);
    		constexpr size_t lksz = std::tuple_size<decltype(lockies)>::value;
    		if constexpr(lksz > 1) {
       		  perform_lock_from_tuple<TcountContention>(lockies, std::make_index_sequence<lksz>());
    		}
    		make_lock_guard_ptr_tuple(lockies, std::make_index_sequence<lksz>());

    		std::invoke(std::forward<_Fn>(__f),
        			std::get<_Idx>(std::forward<_Tuple>(__t)).ref()...);

        	if constexpr(hasCopyBacks_) {
        		(std::get<_Idx>(std::forward<_Tuple>(__t)).copyBack(),...);
        	}
    	} else {
    		std::invoke(std::forward<_Fn>(__f),
    			std::get<_Idx>(std::forward<_Tuple>(__t)).ref()...);

    		if constexpr(hasCopyBacks_) {
        		(std::get<_Idx>(std::forward<_Tuple>(__t)).copyBack(),...);
        	}
    	}
    }

    template <typename _Fn, typename _Tuple, size_t... _Idx>
    constexpr decltype(auto)
    performImplRet(_Fn&& __f, _Tuple&& __t, std::index_sequence<_Idx...> seq) {
    	bool res = false;
    	if constexpr(hasLockies_) {
    		auto lockies = filter_lockies(__t, seq);
    		constexpr size_t lksz = std::tuple_size<decltype(lockies)>::value;
    		if constexpr(lksz > 1) {
       		  perform_lock_from_tuple<TcountContention>(lockies, std::make_index_sequence<lksz>());
    		}
    		make_lock_guard_ptr_tuple(lockies, std::make_index_sequence<lksz>());


    		res = std::invoke(std::forward<_Fn>(__f),
        			std::get<_Idx>(std::forward<_Tuple>(__t)).ref()...);

        	if constexpr(hasCopyBacks_) {
        		(std::get<_Idx>(std::forward<_Tuple>(__t)).copyBack(),...);
        	}
    	} else {
        	res = std::invoke(std::forward<_Fn>(__f),
        			std::get<_Idx>(std::forward<_Tuple>(__t)).ref()...);

    		if constexpr(hasCopyBacks_) {
        		(std::get<_Idx>(std::forward<_Tuple>(__t)).copyBack(),...);
        	}
    	}

        return res;
    }

    virtual void perform() override {
    	if constexpr(!predicate_t::value) {
			using _Indices= std::make_index_sequence<std::tuple_size<decltype(args_)>::value>;
			performImpl(std::forward<F>(f),
				   std::forward<decltype(args_)>(args_),
				   _Indices{});
			ran_ = true;
		} else {
			static_assert(true, "Internal error: Trying to execute a predicate");
		}
    }

    virtual bool performPredicate() override {
    	if constexpr(predicate_t::value) {
				using _Indices= std::make_index_sequence<std::tuple_size<decltype(args_)>::value>;
				bool res = false;
					res = performImplRet(std::forward<F>(f),
						   std::forward<decltype(args_)>(args_),
						   _Indices{});
				ran_ = true;
			return res;
    	} else {
        	static_assert(true, "Internal error: Trying to evaluate a non predicate");
    		return false;
    	}
    }

    virtual bool isPredicate() override {
    	return predicate_t::value;
    }

    virtual bool hasLockies() override {
    	return hasLockies_;
    }

    virtual bool hasCopyBacks() override {
    	return hasCopyBacks_;
    }
};
}

struct Node {
	string name_;
	std::set<long> read_deps_;
	std::set<long> write_deps_;
	cv::Ptr<Transaction> tx_  = nullptr;
	bool initialized() {
		return tx_;
	}
};

template <bool TcountContention = false, typename F, typename... Args>
cv::Ptr<Transaction> make_transaction(F f, Args... args) {
    return cv::Ptr<Transaction>(new detail::TransactionImpl<TcountContention, std::decay_t<F>, std::remove_cv_t<Args>...>
        (f, args...));
}
}
}

#endif /* MODULES_V4D_SRC_BACKEND_HPP_ */
