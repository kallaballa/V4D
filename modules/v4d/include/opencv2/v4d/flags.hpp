#ifndef MODULES_V4D_INCLUDE_OPENCV2_V4D_FLAGS_HPP_
#define MODULES_V4D_INCLUDE_OPENCV2_V4D_FLAGS_HPP_

#include <type_traits>

namespace cv {
namespace v4d {

struct AllocateFlags {
	enum Enum {
		NONE = 0,
		NANOVG = 1,
		IMGUI = 2,
		BGFX = 4,
		DEFAULT = NONE
	};
};

struct ConfigFlags {
	enum Enum {
		DEFAULT = 0,
		OFFSCREEN = 1,
		DISPLAY_MODE = 2,
	};
};

struct DebugFlags {
	enum Enum {
		DEFAULT = 0,
		ONSCREEN_CONTEXTS = 1,
		PRINT_CONTROL_FLOW = 2,
		DEBUG_GL_CONTEXT = 4,
		PRINT_LOCK_CONTENTION = 8,
		MONITOR_RUNTIME_PROPERTIES = 16,
		LOWER_WORKER_PRIORITY = 32,
		DONT_PAUSE_LOG = 64,
	};
};


inline AllocateFlags::Enum operator&(const AllocateFlags::Enum& lhs, const AllocateFlags::Enum& rhs) {
	return static_cast<AllocateFlags::Enum>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

inline AllocateFlags::Enum operator|(const AllocateFlags::Enum& lhs, const AllocateFlags::Enum& rhs) {
	return static_cast<AllocateFlags::Enum>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

inline ConfigFlags::Enum operator&(const ConfigFlags::Enum& lhs, const ConfigFlags::Enum& rhs) {
	return static_cast<ConfigFlags::Enum>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

inline ConfigFlags::Enum operator|(const ConfigFlags::Enum& lhs, const ConfigFlags::Enum& rhs) {
	return static_cast<ConfigFlags::Enum>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

inline DebugFlags::Enum operator&(const DebugFlags::Enum& lhs, const DebugFlags::Enum& rhs) {
	return static_cast<DebugFlags::Enum>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

inline DebugFlags::Enum operator|(const DebugFlags::Enum& lhs, const DebugFlags::Enum& rhs) {
	return static_cast<DebugFlags::Enum>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

}
}

#endif
