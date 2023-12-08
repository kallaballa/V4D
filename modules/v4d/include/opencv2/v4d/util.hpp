// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#ifndef SRC_OPENCV_V4D_UTIL_HPP_
#define SRC_OPENCV_V4D_UTIL_HPP_

#include "source.hpp"
#include "sink.hpp"
#include <filesystem>
#include <string>
#include <iostream>
#ifdef __GNUG__
#include <cstdlib>
#include <memory>
#include <cxxabi.h>
#endif
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <unistd.h>
#include <mutex>
#include <functional>
#include <iostream>
#include <cmath>
#include <thread>

namespace cv {
namespace v4d {
namespace detail {

using std::cout;
using std::endl;

inline uint64_t get_epoch_nanos() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

static thread_local std::mutex mtx_;

class CV_EXPORTS ThreadLocal {
public:
	CV_EXPORTS static std::mutex& mutex() {
    	return mtx_;
    }
};

class CV_EXPORTS Global {
	static std::mutex global_mtx_;

	static std::mutex locking_mtx_;
	static bool locking_;

	static std::set<string> once_;

	static std::mutex frame_cnt_mtx_;
	static uint64_t frame_cnt_;

	static std::mutex start_time_mtx_;
	static uint64_t start_time_;

	static std::mutex fps_mtx_;
	static double fps_;

	static std::mutex thread_id_mtx_;
	static const std::thread::id default_thread_id_;
	static std::thread::id main_thread_id_;
	static thread_local bool is_main_;

	static uint64_t run_cnt_;
	static bool first_run_;

	static size_t workers_ready_;
    static size_t workers_started_;
    static size_t next_worker_idx_;
	static std::mutex sharedMtx_;

	static std::map<size_t, std::mutex*> shared_;
	static std::map<std::thread::id, size_t> thread_worker_id_;
	typedef typename std::map<size_t, std::mutex*>::iterator Iterator;
public:
	template <typename T>
	class Scope {
	private:
		const T& t_;
public:

		Scope(const T& t) : t_(t) {
			lock(t_);
		}

		~Scope() {
			unlock(t_);
		}
	};

	CV_EXPORTS static std::mutex& mutex() {
    	return global_mtx_;
    }

	CV_EXPORTS static bool once(string name) {
	    static std::mutex mtx;
		std::lock_guard<std::mutex> lock(mtx);
		string stem = name.substr(0, name.find_last_of("-"));
		if(once_.empty()) {
			once_.insert(stem);
			return true;
		}

		auto it = once_.find(stem);
		if(it != once_.end()) {
			return false;
		} else {
			once_.insert(stem);
			return true;
		}
	}

	CV_EXPORTS static const bool& isLocking() {
		std::lock_guard<std::mutex> lock(locking_mtx_);
    	return locking_;
    }

	CV_EXPORTS static void setLocking(bool l) {
	    std::lock_guard<std::mutex> lock(locking_mtx_);
    	locking_ = l;
    }

	CV_EXPORTS static uint64_t next_frame_cnt() {
	    std::lock_guard<std::mutex> lock(frame_cnt_mtx_);
    	return frame_cnt_++;
    }

	CV_EXPORTS static uint64_t frame_cnt() {
	    std::lock_guard<std::mutex> lock(frame_cnt_mtx_);
    	return frame_cnt_;
    }

	CV_EXPORTS static void mul_frame_cnt(const double& factor) {
	    std::lock_guard<std::mutex> lock(frame_cnt_mtx_);
    	frame_cnt_ *= factor;
    }

	CV_EXPORTS static void add_to_start_time(const size_t& st) {
		std::lock_guard<std::mutex> lock(start_time_mtx_);
		start_time_ += st;
    }

	CV_EXPORTS static uint64_t start_time() {
		std::lock_guard<std::mutex> lock(start_time_mtx_);
        return start_time_;
    }

	CV_EXPORTS static double fps() {
		std::lock_guard<std::mutex> lock(fps_mtx_);
    	return fps_;
    }

	CV_EXPORTS static void set_fps(const double& f) {
		std::lock_guard<std::mutex> lock(fps_mtx_);
    	fps_ = f;
    }

	CV_EXPORTS static void set_main_id(const std::thread::id& id) {
		std::lock_guard<std::mutex> lock(thread_id_mtx_);
		main_thread_id_ = id;
    }

	CV_EXPORTS static bool is_main() {
		std::lock_guard<std::mutex> lock(start_time_mtx_);
		return (main_thread_id_ == default_thread_id_ || main_thread_id_ == std::this_thread::get_id());
	}

	CV_EXPORTS static bool is_first_run() {
		static std::mutex mtx;
		std::lock_guard<std::mutex> lock(mtx);
    	bool f = first_run_;
    	first_run_ = false;
		return f;
    }

	CV_EXPORTS static uint64_t next_run_cnt() {
	    static std::mutex mtx;
	    std::lock_guard<std::mutex> lock(mtx);
    	return run_cnt_++;
    }

	CV_EXPORTS static void set_workers_started(const size_t& ws) {
	    static std::mutex mtx;
	    std::lock_guard<std::mutex> lock(mtx);
		workers_started_ = ws;
	}

	CV_EXPORTS static size_t workers_started() {
	    static std::mutex mtx;
	    std::lock_guard<std::mutex> lock(mtx);
		return workers_started_;
	}

	CV_EXPORTS static size_t next_worker_ready() {
	    static std::mutex mtx;
	    std::lock_guard<std::mutex> lock(mtx);
		return ++workers_ready_;
	}

	CV_EXPORTS static size_t next_worker_idx() {
	    static std::mutex mtx;
	    std::lock_guard<std::mutex> lock(mtx);
	    thread_worker_id_[std::this_thread::get_id()] = next_worker_idx_;
		return next_worker_idx_++;
	}

	CV_EXPORTS static size_t worker_id() {
		CV_Assert(!is_main());
		return thread_worker_id_[std::this_thread::get_id()];
	}

	template<typename T>
	static bool isShared(const T& shared) {
		std::lock_guard<std::mutex> guard(sharedMtx_);
		std::cerr << "shared:" << reinterpret_cast<size_t>(&shared) << std::endl;
		return shared_.find(reinterpret_cast<size_t>(&shared)) != shared_.end();
	}

	template<typename T>
	static void registerShared(const T& shared) {
		std::lock_guard<std::mutex> guard(sharedMtx_);
		std::cerr << "register:" << reinterpret_cast<size_t>(&shared) << std::endl;
		shared_.insert(std::make_pair(reinterpret_cast<size_t>(&shared), new std::mutex()));
	}

	template<typename T>
	static void lock(const T& shared) {
		Iterator it, end;
		std::mutex* mtx = nullptr;
		{
			std::lock_guard<std::mutex> guard(sharedMtx_);
			it = shared_.find(reinterpret_cast<size_t>(&shared));
			end = shared_.end();
			if(it != end) {
				mtx = (*it).second;
			}
		}

		if(mtx != nullptr) {
			mtx->lock();
			return;
		}
		CV_Assert(!"You are trying to lock a non-shared variable");
	}

	template<typename T>
	static void unlock(const T& shared) {
		Iterator it, end;
		std::mutex* mtx = nullptr;
		{
			std::lock_guard<std::mutex> guard(sharedMtx_);
			it = shared_.find(reinterpret_cast<size_t>(&shared));
			end = shared_.end();
			if(it != end) {
				mtx = (*it).second;
			}
		}

		if(mtx != nullptr) {
			mtx->unlock();
			return;
		}

		CV_Assert(!"You are trying to unlock a non-shared variable");
	}

	template<typename T>
	static T safe_copy(const T& shared) {
		std::lock_guard<std::mutex> guard(sharedMtx_);
		auto it = shared_.find(reinterpret_cast<size_t>(&shared));

		if(it != shared_.end()) {
			std::lock_guard<std::mutex> guard(*(*it).second);
			return shared;
		} else {
			CV_Assert(!"You are unnecessarily safe copying a variable");
			//unreachable
			return shared;
		}
	}

	static cv::UMat safe_copy(const cv::UMat& shared) {
		std::lock_guard<std::mutex> guard(sharedMtx_);
		cv::UMat copy;
		auto it = shared_.find(reinterpret_cast<size_t>(&shared));
		if(it != shared_.end()) {
			std::lock_guard<std::mutex> guard(*(*it).second);
			//workaround for context conflicts
			shared.getMat(cv::ACCESS_READ).copyTo(copy);
			return copy;
		} else {
			CV_Assert(!"You are unnecessarily safe copying a variable");
			//unreachable
			shared.getMat(cv::ACCESS_READ).copyTo(copy);
			return copy;
		}
	}
};

//https://stackoverflow.com/a/27885283/1884837
template<class T>
struct function_traits : function_traits<decltype(&T::operator())> {
};

// partial specialization for function type
template<class R, class... Args>
struct function_traits<R(Args...)> {
    using result_type = R;
    using argument_types = std::tuple<std::remove_reference_t<Args>...>;
	static const bool value = true;
};

// partial specialization for function pointer
template<class R, class... Args>
struct function_traits<R (*)(Args...)> {
    using result_type = R;
    using argument_types = std::tuple<std::remove_reference_t<Args>...>;
	static const bool value = true;
};

// partial specialization for std::function
template<class R, class... Args>
struct function_traits<std::function<R(Args...)>> {
    using result_type = R;
    using argument_types = std::tuple<std::remove_reference_t<Args>...>;
	static const bool value = true;
};

// partial specialization for pointer-to-member-function (i.e., operator()'s)
template<class T, class R, class... Args>
struct function_traits<R (T::*)(Args...)> {
    using result_type = R;
    using argument_types = std::tuple<std::remove_reference_t<Args>...>;
	static const bool value = true;
};

template<class T, class R, class... Args>
struct function_traits<R (T::*)(Args...) const> {
    using result_type = R;
    using argument_types = std::tuple<std::remove_reference_t<Args>...>;
	static const bool value = true;
};


//https://stackoverflow.com/questions/281818/unmangling-the-result-of-stdtype-infoname
CV_EXPORTS std::string demangle(const char* name);

template <const size_t _UniqueId, typename _Res, typename... _ArgTypes>
struct fun_ptr_helper
{
public:
    typedef std::function<_Res(_ArgTypes...)> function_type;

    static void bind(function_type&& f)
    { instance().fn_.swap(f); }

    static void bind(const function_type& f)
    { instance().fn_=f; }

    static _Res invoke(_ArgTypes... args)
    { return instance().fn_(args...); }

    typedef decltype(&fun_ptr_helper::invoke) pointer_type;
    static pointer_type ptr()
    { return &invoke; }

private:
    static fun_ptr_helper& instance()
    {
        static fun_ptr_helper inst_;
        return inst_;
    }

    fun_ptr_helper() {}

    function_type fn_;
};

template <const size_t _UniqueId, typename _Res, typename... _ArgTypes>
typename fun_ptr_helper<_UniqueId, _Res, _ArgTypes...>::pointer_type
get_fn_ptr(const std::function<_Res(_ArgTypes...)>& f)
{
    fun_ptr_helper<_UniqueId, _Res, _ArgTypes...>::bind(f);
    return fun_ptr_helper<_UniqueId, _Res, _ArgTypes...>::ptr();
}

template<typename T>
std::function<typename std::enable_if<std::is_function<T>::value, T>::type>
make_function(T *t)
{
    return {t};
}

//https://stackoverflow.com/a/33047781/1884837
class Lambda {
    template<typename T>
    static const void* fn(const void* new_fn = nullptr) {
        CV_Assert(new_fn);
    	return new_fn;
    }
	template<typename Tret, typename T>
    static Tret lambda_ptr_exec() {
        return (Tret) (*(T*)fn<T>());
    }
public:
	//FIXME race condition?
    template<typename Tret = void, typename Tfp = Tret(*)(), typename T>
    static Tfp ptr(T& t) {
        fn<T>(&t);
        return (Tfp) lambda_ptr_exec<Tret, T>;
    }
};

CV_EXPORTS size_t cnz(const cv::UMat& m);
}
using std::string;
class V4D;



CV_EXPORTS void copy_shared(const cv::UMat& src, cv::UMat& dst);


/*!
 * Convenience function to color convert from Scalar to Scalar
 * @param src The scalar to color convert
 * @param code The color converions code
 * @return The color converted scalar
 */
CV_EXPORTS cv::Scalar colorConvert(const cv::Scalar& src, cv::ColorConversionCodes code);

/*!
 * Convenience function to check for OpenGL errors. Should only be used via the macro #GL_CHECK.
 * @param file The file path of the error.
 * @param line The file line of the error.
 * @param expression The expression that failed.
 */
CV_EXPORTS void gl_check_error(const std::filesystem::path& file, unsigned int line, const char* expression);
/*!
 * Convenience macro to check for OpenGL errors.
 */
#ifndef NDEBUG
#define GL_CHECK(expr)                            \
    expr;                                        \
    cv::v4d::gl_check_error(__FILE__, __LINE__, #expr);
#else
#define GL_CHECK(expr)                            \
    expr;
#endif
CV_EXPORTS void init_shaders(unsigned int handles[3], const char* vShader, const char* fShader, const char* outputAttributeName);
CV_EXPORTS void init_fragment_shader(unsigned int handles[2], const char* fshader);
/*!
 * Returns the OpenGL vendor string
 * @return a string object with the OpenGL vendor information
 */
CV_EXPORTS std::string getGlVendor();
/*!
 * Returns the OpenGL Version information.
 * @return a string object with the OpenGL version information
 */
CV_EXPORTS std::string getGlInfo();
/*!
 * Returns the OpenCL Version information.
 * @return a string object with the OpenCL version information
 */
CV_EXPORTS std::string getClInfo();
/*!
 * Determines if Intel VAAPI is supported
 * @return true if it is supported
 */
CV_EXPORTS bool isIntelVaSupported();
/*!
 * Determines if cl_khr_gl_sharing is supported
 * @return true if it is supported
 */
CV_EXPORTS bool isClGlSharingSupported();
/*!
 * Tells the application if it's alright to keep on running.
 * Note: If you use this mechanism signal handlers are installed
 * @return true if the program should keep on running
 */
CV_EXPORTS bool keepRunning();

CV_EXPORTS void requestFinish();

void resizePreserveAspectRatio(const cv::UMat& src, cv::UMat& output, const cv::Size& dstSize, const cv::Scalar& bgcolor = {0,0,0,255});

}
}

#endif /* SRC_OPENCV_V4D_UTIL_HPP_ */
