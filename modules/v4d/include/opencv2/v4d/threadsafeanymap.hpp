#ifndef SRC_OPENCV_THREADSAFEMAP_HPP_
#define SRC_OPENCV_THREADSAFEMAP_HPP_

#include <any>
#include <vector>
#include <mutex>
#include <shared_mutex>
#ifdef __GNUG__
#include <cxxabi.h>
#endif

namespace cv {
namespace v4d {
namespace detail {
#ifdef __GNUG__

inline std::string demangle(const char* name) {
    int status = -4; // some arbitrary value to eliminate the compiler warning
    std::unique_ptr<char, void(*)(void*)> res {
        abi::__cxa_demangle(name, NULL, NULL, &status),
        std::free
    };

    return (status==0) ? res.get() : name ;
}

#else
// does nothing if not g++
constexpr int demangle(const char* name, char* res) {
	res = new char[sizeof[](name)];
	return 0;
}
#endif

template<typename T>
std::string type_name() {
	return demangle(typeid(T).name());
}
}
class Value : public std::any {
public:
	std::function<void(Value& val)> callback_;
	const bool read_;

	Value(const bool& read = false) : read_(read) {
	}

    any& operator=(const any& rhs) {
    	return std::any::operator =(rhs);
    }

    template <typename T>
    any& operator=(T&& __rhs) {
    	std::any::operator =(any(std::forward<T>(__rhs)));
    	return *this;
    }
};

template<typename K>
class AnyPropertyMap {
	static_assert(std::is_enum<K>::value);
private:
    std::vector<Value> properties_;
    template<typename V>
    constexpr void check_value_type() const {
    	using U = std::remove_cv_t<std::remove_reference_t<V>>;

    	static_assert(std::is_constructible<V, const U&>::value, "Illegal value type: Can't construct const V&");
    	static_assert(std::is_constructible<V, U&>::value, "Illegal value type: Can't construct V&");
    	static_assert(std::is_constructible<V, U>::value, "Illegal value type: Can't construct V from itself");
    	static_assert(!std::is_void<V>::value, "Illegal value type: V may not be void");
    }

    void check_write(K key) {
    	CV_Assert(properties_.size() > key);
    	if(properties_[key].read_) {
    		CV_Error(cv::Error::StsBadArg, "You are trying to set a read only property");
    	}
    }

public:
    template<bool Tread, typename V>
    void create(K key, const V& value, std::function<void(const V& val)> cb) {
    	check_value_type<V>();
    	properties_.reserve(100);
//    	CV_Assert(properties_.size() == key);
    	CV_Assert(!Tread || (Tread && !cb));
    	if constexpr(Tread) {
    		Value val(Tread);
    		val.callback_ = [](const Value& val){};
    		val = value;
    		properties_.emplace_back(val);
    	} else {
    		if(!cb)
    			cb = [](const V& v){};

    		properties_.emplace_back(Value(Tread));
    		properties_[key] = value;
    		Value& val = properties_[key];
    		val.callback_ = [cb](const Value& val){ cb(std::any_cast<V>(val)); };
    	}
    }

    template<typename V>
    void set(K key, const V& value, bool fire = true) {
    	check_value_type<V>();
    	check_write(key);
    	V* p = std::any_cast<V>(&properties_[key]);

    	if(!p)
    		CV_Error(cv::Error::StsBadArg, string("Type mistmatch for key: ") + std::to_string(int(key)) + ". Expected: " + detail::demangle(properties_[key].type().name()) + ", Got: " + detail::type_name<V>() + ".");
    	V oldVal = *p;
    	*p = value;
    	if(fire && memcmp(&oldVal, p, sizeof(V)) != 0)
    		properties_[key].callback_(properties_[key]);
    }

    template<typename V>
    constexpr const V& get(K key) const {
    	check_value_type<V>();
        return *ptr<V>(key);
    }

    template<typename V> auto apply(K key, std::function<V(V&)> func) {
    	check_value_type<V>();
    	check_write(key);
    	V ret = func(*std::any_cast<V>(&properties_[key]));
        return ret;
    }

    // A member to get a pointer to the value for a given key
    template<typename V>
    constexpr const V* ptr(K key) const {
    	check_value_type<V>();
//    	CV_Assert(properties_.size() > key);
    	const V* p = std::any_cast<V>(&properties_[key]);
//    	CV_Assert(p != nullptr);
    	return p;
    }
};

template<typename K>
class ThreadSafeAnyMap : public AnyPropertyMap<K> {
private:
	AnyPropertyMap<K> mutexes_;
    cv::Ptr<std::shared_mutex> mapMutexPtr_ = cv::makePtr<std::shared_mutex>();
    using parent_t = AnyPropertyMap<K>;
public:
    template<bool Tread, typename V>
   void create(K key, const V& value, const std::function<void(const V& val)>& cb = std::function<void(const V& val)>()) {
    	std::function<void(const cv::Ptr<std::shared_mutex>&)> emptyFn;
    	mutexes_.template create<Tread>(key, cv::makePtr<std::shared_mutex>(), emptyFn);
    	parent_t::template create<Tread>(key, value, cb);
    }

    template<typename V>
    void set(K key, const V& value, bool fire = true) {
        std::unique_lock<std::shared_mutex> map_lock(*mapMutexPtr_);
        std::unique_lock<std::shared_mutex> key_lock(*mutexes_.template get<cv::Ptr<std::shared_mutex>>(key));
        parent_t::set(key, value, fire);
    }
//
//    template<typename ... Elements>
//    void set(std::tuple<Elements&>& ... kv, bool fire = true) {
//        std::unique_lock<std::shared_mutex> map_lock(*mapMutexPtr_);
//        std::unique_lock<std::shared_mutex> key_lock(*mutexes_.template get<cv::Ptr<std::shared_mutex>>(key));
//        parent_t::set(key, value, fire);
//    }

    template<typename V>
    const V& get(K key) const {
        std::shared_lock<std::shared_mutex> map_lock(*mapMutexPtr_);
        std::shared_lock<std::shared_mutex> key_lock(*mutexes_.template get<cv::Ptr<std::shared_mutex>>(key));
        return parent_t::template get<V>(key);
    }

    template<typename V> V apply(K key, std::function<V(V&)> func) {
        std::unique_lock<std::shared_mutex> map_lock(*mapMutexPtr_);
        std::unique_lock<std::shared_mutex> key_lock(*mutexes_.template get<cv::Ptr<std::shared_mutex>>(key));
        return parent_t::template apply<V>(key, func);
    }

    // A function to get a pointer to the value for a given key
    // Note: This function is not thread-safe
    template<typename V>
    V* ptr(K key) const {
        return parent_t::template ptr<V>(key);
    }
};

}
}

#endif // SRC_OPENCV_THREADSAFEMAP_HPP_
