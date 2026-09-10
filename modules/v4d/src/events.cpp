#include "opencv2/core/cvdef.h"
#define EVENT_API_EXPORT CV_EXPORTS
#include "../include/opencv2/v4d/events.hpp"

namespace cv {
namespace v4d {
namespace event {
namespace detail {
	std::pair<int, int> Holder::window_size;
	GLFWwindow* Holder::main_window = nullptr;
	KeyCallback Holder::keyboardCallback;
	MouseButtonCallback Holder::mouseButtonCallback;
	ScrollCallback Holder::scrollCallback;
	CursorPosCallback Holder::cursorPosCallback;
	WindowSizeCallback Holder::windowSizeCallback;
	WindowPosCallback Holder::windowPosCallback;
	WindowFocusCallback Holder::windowFocusCallback;
	WindowCloseCallback Holder::windowCloseCallback;
	std::vector<EventQueue*> Holder::queue_vector;
;
}
}
}
}
