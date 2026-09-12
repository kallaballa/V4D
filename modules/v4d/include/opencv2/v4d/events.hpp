// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#ifndef MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_EVENTS_HPP_
#define MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_EVENTS_HPP_

#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <queue>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>
#include <map>
#include <cmath>
#include <cassert>
#include <iostream>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>
#include <iterator>
#include <string>
#include <array>

#ifndef EVENT_API_EXPORT
#define EVENT_API_EXPORT
#endif

#ifndef GWE_UNUSED
#define GWE_UNUSED(x) (void)(x)
#endif

// ============================================================================
// GLFW Header-Only Event Library
// Namespace changed as requested: `gwe` (GLFW events)
// ============================================================================

namespace gwe {

// ---------- Point Traits (register your own point type by specializing point_traits) ----------
template <class P>
struct point_traits {
    static double x(const P& p) { return p.x; }
    static double y(const P& p) { return p.y; }
    static void   set(P& p, double nx, double ny) { p.x = nx; p.y = ny; }
    static P      make(double x, double y) { return P(x, y); }
    static P      sub(const P& a, const P& b) { return a - b; }
};

// Optional default point if you don't want to define your own
struct DefaultPoint {
    double x{0}, y{0};
    DefaultPoint() = default;
    DefaultPoint(double X, double Y): x(X), y(Y) {}
    friend DefaultPoint operator-(const DefaultPoint& a, const DefaultPoint& b) {
        return {a.x - b.x, a.y - b.y};
    }
};

// ---------- Callback typedefs ----------
typedef std::function<bool(GLFWwindow *, int, int, int, int)> KeyCallback;
typedef std::function<bool(GLFWwindow*, unsigned int)>        CharCallback;
typedef std::function<bool(GLFWwindow*, int, int, int)>       MouseButtonCallback;
typedef std::function<bool(GLFWwindow*, double, double)>      ScrollCallback;
typedef std::function<bool(GLFWwindow*, double, double)>      CursorPosCallback;
typedef std::function<bool(GLFWwindow*, int)>                 CursorEnterCallback;
typedef std::function<bool(GLFWwindow*, int, int)>            WindowSizeCallback;
typedef std::function<bool(GLFWwindow*, int, int)>            WindowPosCallback;
typedef std::function<bool(GLFWwindow*, int)>                 WindowFocusCallback;
typedef std::function<bool(GLFWwindow*)>                      WindowCloseCallback;

// ---------- Base Event ----------
class Event {
public:
    enum Class { KEYBOARD, MOUSE, JOYSTICK, WINDOW };
    virtual ~Event() = default;

    Class getClass() const { return class_; }

protected:
    Event(Class c) : class_(c) {}

private:
    Class class_;
};

// ---------- Keyboard ----------
class Keyboard: public Event {
public:
    using List = std::vector<std::shared_ptr<Keyboard>>;
    enum Key {
        A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
        N0,N1,N2,N3,N4,N5,N6,N7,N8,N9,
        SPACE,ENTER,BACKSPACE,TAB,ESCAPE,UP,DOWN,LEFT,RIGHT,HOME,END,PAGE_UP,PAGE_DOWN,INSERT,DELETE,
        F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,
        APOSTROPHE,COMMA,MINUS,PERIOD,SLASH,SEMICOLON,EQUAL,LEFT_BRACKET,BACKSLASH,RIGHT_BRACKET,GRAVE_ACCENT,
        WORLD_1,WORLD_2,CAPS_LOCK,SCROLL_LOCK,NUM_LOCK,PRINT_SCREEN,PAUSE,
        F13,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,F25,
        KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
        KP_DECIMAL,KP_DIVIDE,KP_MULTIPLY,KP_SUBTRACT,KP_ADD,KP_ENTER,KP_EQUAL,
        LEFT_SHIFT,LEFT_CONTROL,LEFT_ALT,LEFT_SUPER,RIGHT_SHIFT,RIGHT_CONTROL,RIGHT_ALT,RIGHT_SUPER,MENU
    };

    enum Type { PRESS, RELEASE, REPEAT, HOLD };

    Keyboard(Type type, Key key) : Event(Class::KEYBOARD), key_(key), type_(type) {}

    Key  key()  const { return key_; }
    Type type() const { return type_; }

    bool is(Key a)  const { return key() == a; }
    bool is(Type t) const { return type() == t; }
private:
    Key key_;
    Type type_;
};

// ---------- Mouse (templated on point) ----------
template<typename Tpoint>
class Mouse_: public Event {
public:
    using List = std::vector<std::shared_ptr<Mouse_<Tpoint>>>;

    enum Button { NO_BUTTON, LEFT, RIGHT, MIDDLE, N4, N5, N6, N7, N8 };

    enum Type {
        PRESS, RELEASE, MOVE, SCROLL, DRAG, HOVER_ENTER, HOVER_EXIT, DOUBLE_CLICK
    };

    Mouse_(Type type, Tpoint position)
        : Event(Class::MOUSE), button_(NO_BUTTON), type_(type), position_(position) {}

    Mouse_(Type type, Tpoint position, Tpoint data)
        : Event(Class::MOUSE), button_(NO_BUTTON), type_(type), position_(position), data_(data) {}

    Mouse_(Type type, Button button, Tpoint position)
        : Event(Class::MOUSE), button_(button), type_(type), position_(position) {}

    Mouse_(Type type, Button button, Tpoint position, Tpoint data)
        : Event(Class::MOUSE), button_(button), type_(type), position_(position), data_(data) {}

    Button button()   const { return button_; }
    Type   type()     const { return type_; }
    Tpoint position() const { return position_; }
    Tpoint data()     const { return data_; }

    bool is(Button a) const { return button() == a; }
    bool is(Type t)   const { return type() == t; }
private:
    Button button_;
    Type type_;
    Tpoint position_;
    Tpoint data_;
};

// ---------- Joystick ----------
class Joystick: public Event {
public:
    using List = std::vector<std::shared_ptr<Joystick>>;
    enum Type { PRESS, RELEASE, REPEAT, MOVE };

    enum Button {
        NO_BUTTON, A, B, X, Y, LB, RB, BACK, START, GUIDE, LEFT_THUMB, RIGHT_THUMB, DPAD_UP, DPAD_RIGHT, DPAD_DOWN, DPAD_LEFT
    };

    enum Axis {
        NO_AXIS, LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y, LEFT_TRIGGER, RIGHT_TRIGGER
    };

    Joystick(Type type, int joystick, Button button)
        : Event(Class::JOYSTICK), type_(type), joystick_(joystick), button_(button), state_(type == PRESS),
          axis_(NO_AXIS), init_(0), value_(0), delta_(0) {}

    Joystick(int joystick, Axis axis, float init, float value, float delta)
        : Event(Class::JOYSTICK), type_(MOVE), joystick_(joystick), button_(NO_BUTTON), state_(false),
          axis_(axis), init_(init), value_(value), delta_(delta) {}

    bool  is(Button a) const { return button() == a; }
    bool  is(Axis a)   const { return axis() == a; }
    bool  is(Type t)   const { return type() == t; }

    Type   type()     const { return type_; }
    int    joystick() const { return joystick_; }
    Button button()   const { return button_; }
    bool   state()    const { return state_; }
    Axis   axis()     const { return axis_; }
    float  init()     const { return init_; }
    float  value()    const { return value_; }
    float  delta()    const { return delta_; }
    float  abs()      const { return value_ - init_; }

    bool active() const {
        if(is(Axis::LEFT_TRIGGER) || is(Axis::RIGHT_TRIGGER)) {
            return value() > -1;
        } else {
            return std::fabs(abs()) > 0.1f;
        }
    }

private:
    Type  type_;
    int   joystick_;
    Button button_;
    bool  state_;
    Axis  axis_;
    float init_;
    float value_;
    float delta_;
};

// ---------- Window (templated on point) ----------
template<typename Tpoint>
class Window_: public Event {
public:
    using List = std::vector<std::shared_ptr<Window_<Tpoint>>>;
    enum Type { RESIZE, MOVE, FOCUS, UNFOCUS, CLOSE };

    Window_(Type type, Tpoint data) : Event(Class::WINDOW), type_(type), data_(data) {}
    Window_(Type type)               : Event(Class::WINDOW), type_(type) {}

    Type   type() const { return type_; }
    Tpoint data() const { return data_; }

private:
    Type   type_;
    Tpoint data_;
};


// ---------- detail ----------
namespace detail {
inline double js_repeat_delay = 0.40;      // seconds to first repeat
inline double js_repeat_rate  = 1.0 / 60;  // seconds between repeats

// Per-axis next repeat time: jid -> per-axis timestamps (init to -1.0)
inline static std::map<int, std::array<double, GLFW_GAMEPAD_AXIS_LAST + 1>> axis_next_repeat_at;

// Global locks & mappings
inline static std::mutex queue_access_mtx;
inline static std::map<std::thread::id, size_t> thread_id_map;

// Configurable queue capacity (applies to new queues; existing queues can be updated)
inline static size_t default_queue_capacity = 1000;

// Double-click detection params
inline static double dblclick_time_sec  = 0.30;  // time threshold
inline static double dblclick_dist2_px2 = 25.0;  // squared pixel distance (5px)^2

// Per-button last click time/pos (use GLFW enums: 0..GLFW_MOUSE_BUTTON_LAST)
inline static std::array<double, GLFW_MOUSE_BUTTON_LAST + 1> last_click_time = {0};
inline static std::array<double, GLFW_MOUSE_BUTTON_LAST + 1> last_click_x    = {0};
inline static std::array<double, GLFW_MOUSE_BUTTON_LAST + 1> last_click_y    = {0};

// Joystick axis state, per joystick id -> 6 axes
constexpr float AXIS_NO_VALUE = std::numeric_limits<float>::max();
inline static std::map<int, std::array<float, GLFW_GAMEPAD_AXIS_LAST + 1>> prev_axis_values;
inline static std::map<int, std::array<float, GLFW_GAMEPAD_AXIS_LAST + 1>> init_axis_values;

class EventQueue;

// Holder of global GLFW state and user callbacks
class EVENT_API_EXPORT Holder {
public:
    static std::pair<int, int>   window_size;
    static GLFWwindow*           main_window;
    static KeyCallback           keyboardCallback;
    static CharCallback          charCallback;
    static MouseButtonCallback   mouseButtonCallback;
    static ScrollCallback        scrollCallback;
    static CursorPosCallback     cursorPosCallback;
    static CursorEnterCallback   cursorEnterCallback;
    static WindowSizeCallback    windowSizeCallback;
    static WindowPosCallback     windowPosCallback;
    static WindowFocusCallback   windowFocusCallback;
    static WindowCloseCallback   windowCloseCallback;
    static std::vector<EventQueue*> queue_vector;
};

// Inline definitions (header-only ODR-safe)
inline std::pair<int, int> Holder::window_size = {0, 0};
inline GLFWwindow*         Holder::main_window = nullptr;
inline KeyCallback         Holder::keyboardCallback = {};
inline CharCallback        Holder::charCallback = {};
inline MouseButtonCallback Holder::mouseButtonCallback = {};
inline ScrollCallback      Holder::scrollCallback = {};
inline CursorPosCallback   Holder::cursorPosCallback = {};
inline CursorEnterCallback Holder::cursorEnterCallback = {};
inline WindowSizeCallback  Holder::windowSizeCallback = {};
inline WindowPosCallback   Holder::windowPosCallback = {};
inline WindowFocusCallback Holder::windowFocusCallback = {};
inline WindowCloseCallback Holder::windowCloseCallback = {};

inline double monotonic_seconds() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

inline Keyboard::Key v4d_key(int glfw_key) {
    switch (glfw_key) {
    case GLFW_KEY_A: return Keyboard::A;
    case GLFW_KEY_B: return Keyboard::B;
    case GLFW_KEY_C: return Keyboard::C;
    case GLFW_KEY_D: return Keyboard::D;
    case GLFW_KEY_E: return Keyboard::E;
    case GLFW_KEY_F: return Keyboard::F;
    case GLFW_KEY_G: return Keyboard::G;
    case GLFW_KEY_H: return Keyboard::H;
    case GLFW_KEY_I: return Keyboard::I;
    case GLFW_KEY_J: return Keyboard::J;
    case GLFW_KEY_K: return Keyboard::K;
    case GLFW_KEY_L: return Keyboard::L;
    case GLFW_KEY_M: return Keyboard::M;
    case GLFW_KEY_N: return Keyboard::N;
    case GLFW_KEY_O: return Keyboard::O;
    case GLFW_KEY_P: return Keyboard::P;
    case GLFW_KEY_Q: return Keyboard::Q;
    case GLFW_KEY_R: return Keyboard::R;
    case GLFW_KEY_S: return Keyboard::S;
    case GLFW_KEY_T: return Keyboard::T;
    case GLFW_KEY_U: return Keyboard::U;
    case GLFW_KEY_V: return Keyboard::V;
    case GLFW_KEY_W: return Keyboard::W;
    case GLFW_KEY_X: return Keyboard::X;
    case GLFW_KEY_Y: return Keyboard::Y;
    case GLFW_KEY_Z: return Keyboard::Z;
    case GLFW_KEY_0: return Keyboard::N0;
    case GLFW_KEY_1: return Keyboard::N1;
    case GLFW_KEY_2: return Keyboard::N2;
    case GLFW_KEY_3: return Keyboard::N3;
    case GLFW_KEY_4: return Keyboard::N4;
    case GLFW_KEY_5: return Keyboard::N5;
    case GLFW_KEY_6: return Keyboard::N6;
    case GLFW_KEY_7: return Keyboard::N7;
    case GLFW_KEY_8: return Keyboard::N8;
    case GLFW_KEY_9: return Keyboard::N9;
    case GLFW_KEY_SPACE: return Keyboard::SPACE;
    case GLFW_KEY_ENTER: return Keyboard::ENTER;
    case GLFW_KEY_BACKSPACE: return Keyboard::BACKSPACE;
    case GLFW_KEY_TAB: return Keyboard::TAB;
    case GLFW_KEY_ESCAPE: return Keyboard::ESCAPE;
    case GLFW_KEY_UP: return Keyboard::UP;
    case GLFW_KEY_DOWN: return Keyboard::DOWN;
    case GLFW_KEY_LEFT: return Keyboard::LEFT;
    case GLFW_KEY_RIGHT: return Keyboard::RIGHT;
    case GLFW_KEY_HOME: return Keyboard::HOME;
    case GLFW_KEY_END: return Keyboard::END;
    case GLFW_KEY_PAGE_UP: return Keyboard::PAGE_UP;
    case GLFW_KEY_PAGE_DOWN: return Keyboard::PAGE_DOWN;
    case GLFW_KEY_INSERT: return Keyboard::INSERT;
    case GLFW_KEY_DELETE: return Keyboard::DELETE;
    case GLFW_KEY_F1: return Keyboard::F1;
    case GLFW_KEY_F2: return Keyboard::F2;
    case GLFW_KEY_F3: return Keyboard::F3;
    case GLFW_KEY_F4: return Keyboard::F4;
    case GLFW_KEY_F5: return Keyboard::F5;
    case GLFW_KEY_F6: return Keyboard::F6;
    case GLFW_KEY_F7: return Keyboard::F7;
    case GLFW_KEY_F8: return Keyboard::F8;
    case GLFW_KEY_F9: return Keyboard::F9;
    case GLFW_KEY_F10: return Keyboard::F10;
    case GLFW_KEY_F11: return Keyboard::F11;
    case GLFW_KEY_F12: return Keyboard::F12;
    case GLFW_KEY_APOSTROPHE: return Keyboard::APOSTROPHE;
    case GLFW_KEY_COMMA: return Keyboard::COMMA;
    case GLFW_KEY_MINUS: return Keyboard::MINUS;
    case GLFW_KEY_PERIOD: return Keyboard::PERIOD;
    case GLFW_KEY_SLASH: return Keyboard::SLASH;
    case GLFW_KEY_SEMICOLON: return Keyboard::SEMICOLON;
    case GLFW_KEY_EQUAL: return Keyboard::EQUAL;
    case GLFW_KEY_LEFT_BRACKET: return Keyboard::LEFT_BRACKET;
    case GLFW_KEY_BACKSLASH: return Keyboard::BACKSLASH;
    case GLFW_KEY_RIGHT_BRACKET: return Keyboard::RIGHT_BRACKET;
    case GLFW_KEY_GRAVE_ACCENT: return Keyboard::GRAVE_ACCENT;
    case GLFW_KEY_WORLD_1: return Keyboard::WORLD_1;
    case GLFW_KEY_WORLD_2: return Keyboard::WORLD_2;
    case GLFW_KEY_CAPS_LOCK: return Keyboard::CAPS_LOCK;
    case GLFW_KEY_SCROLL_LOCK: return Keyboard::SCROLL_LOCK;
    case GLFW_KEY_NUM_LOCK: return Keyboard::NUM_LOCK;
    case GLFW_KEY_PRINT_SCREEN: return Keyboard::PRINT_SCREEN;
    case GLFW_KEY_PAUSE: return Keyboard::PAUSE;
    case GLFW_KEY_KP_0: return Keyboard::KP_0;
    case GLFW_KEY_KP_1: return Keyboard::KP_1;
    case GLFW_KEY_KP_2: return Keyboard::KP_2;
    case GLFW_KEY_KP_3: return Keyboard::KP_3;
    case GLFW_KEY_KP_4: return Keyboard::KP_4;
    case GLFW_KEY_KP_5: return Keyboard::KP_5;
    case GLFW_KEY_KP_6: return Keyboard::KP_6;
    case GLFW_KEY_KP_7: return Keyboard::KP_7;
    case GLFW_KEY_KP_8: return Keyboard::KP_8;
    case GLFW_KEY_KP_9: return Keyboard::KP_9;
    case GLFW_KEY_KP_DECIMAL: return Keyboard::KP_DECIMAL;
    case GLFW_KEY_KP_DIVIDE: return Keyboard::KP_DIVIDE;
    case GLFW_KEY_KP_MULTIPLY: return Keyboard::KP_MULTIPLY;
    case GLFW_KEY_KP_SUBTRACT: return Keyboard::KP_SUBTRACT;
    case GLFW_KEY_KP_ADD: return Keyboard::KP_ADD;
    case GLFW_KEY_KP_ENTER: return Keyboard::KP_ENTER;
    case GLFW_KEY_KP_EQUAL: return Keyboard::KP_EQUAL;
    case GLFW_KEY_LEFT_SHIFT: return Keyboard::LEFT_SHIFT;
    case GLFW_KEY_LEFT_CONTROL: return Keyboard::LEFT_CONTROL;
    case GLFW_KEY_LEFT_ALT: return Keyboard::LEFT_ALT;
    case GLFW_KEY_LEFT_SUPER: return Keyboard::LEFT_SUPER;
    case GLFW_KEY_RIGHT_SHIFT: return Keyboard::RIGHT_SHIFT;
    case GLFW_KEY_RIGHT_CONTROL: return Keyboard::RIGHT_CONTROL;
    case GLFW_KEY_RIGHT_ALT: return Keyboard::RIGHT_ALT;
    case GLFW_KEY_RIGHT_SUPER: return Keyboard::RIGHT_SUPER;
    case GLFW_KEY_MENU: return Keyboard::MENU;
    default:
        throw std::runtime_error("Invalid key: " + std::to_string(glfw_key));
    }
}

template<typename Tpoint>
inline typename Mouse_<Tpoint>::Button v4d_mouse_button(int glfw_button) {
    switch (glfw_button) {
    case GLFW_MOUSE_BUTTON_LEFT:   return Mouse_<Tpoint>::LEFT;
    case GLFW_MOUSE_BUTTON_RIGHT:  return Mouse_<Tpoint>::RIGHT;
    case GLFW_MOUSE_BUTTON_MIDDLE: return Mouse_<Tpoint>::MIDDLE;
    case GLFW_MOUSE_BUTTON_4:      return Mouse_<Tpoint>::N4;
    case GLFW_MOUSE_BUTTON_5:      return Mouse_<Tpoint>::N5;
    case GLFW_MOUSE_BUTTON_6:      return Mouse_<Tpoint>::N6;
    case GLFW_MOUSE_BUTTON_7:      return Mouse_<Tpoint>::N7;
    case GLFW_MOUSE_BUTTON_8:      return Mouse_<Tpoint>::N8;
    default:
        throw std::runtime_error("Invalid mouse button: " + std::to_string(glfw_button));
    }
}

inline Joystick::Button v4d_joystick_button(int glfw_button) {
    switch (glfw_button) {
    case GLFW_GAMEPAD_BUTTON_A:             return Joystick::A;
    case GLFW_GAMEPAD_BUTTON_B:             return Joystick::B;
    case GLFW_GAMEPAD_BUTTON_X:             return Joystick::X;
    case GLFW_GAMEPAD_BUTTON_Y:             return Joystick::Y;
    case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:   return Joystick::LB;
    case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER:  return Joystick::RB;
    case GLFW_GAMEPAD_BUTTON_BACK:          return Joystick::BACK;
    case GLFW_GAMEPAD_BUTTON_START:         return Joystick::START;
    case GLFW_GAMEPAD_BUTTON_GUIDE:         return Joystick::GUIDE;
    case GLFW_GAMEPAD_BUTTON_LEFT_THUMB:    return Joystick::LEFT_THUMB;
    case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB:   return Joystick::RIGHT_THUMB;
    case GLFW_GAMEPAD_BUTTON_DPAD_UP:       return Joystick::DPAD_UP;
    case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT:    return Joystick::DPAD_RIGHT;
    case GLFW_GAMEPAD_BUTTON_DPAD_DOWN:     return Joystick::DPAD_DOWN;
    case GLFW_GAMEPAD_BUTTON_DPAD_LEFT:     return Joystick::DPAD_LEFT;
    default:
        throw std::runtime_error("Invalid joystick button: " + std::to_string(glfw_button));
    }
}

inline Joystick::Axis v4d_joystick_axis(int glfw_axis) {
    switch (glfw_axis) {
    case GLFW_GAMEPAD_AXIS_LEFT_X:       return Joystick::LEFT_X;
    case GLFW_GAMEPAD_AXIS_LEFT_Y:       return Joystick::LEFT_Y;
    case GLFW_GAMEPAD_AXIS_RIGHT_X:      return Joystick::RIGHT_X;
    case GLFW_GAMEPAD_AXIS_RIGHT_Y:      return Joystick::RIGHT_Y;
    case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER: return Joystick::LEFT_TRIGGER;
    case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER:return Joystick::RIGHT_TRIGGER;
    default:
        throw std::runtime_error("Invalid joystick axis: " + std::to_string(glfw_axis));
    }
}

inline Keyboard::Type v4d_keyboard_event_type(int glfw_state) {
    switch (glfw_state) {
    case GLFW_PRESS:   return Keyboard::PRESS;
    case GLFW_RELEASE: return Keyboard::RELEASE;
    case GLFW_REPEAT:  return Keyboard::REPEAT;
    default:
        throw std::runtime_error("Invalid keyboard state: " + std::to_string(glfw_state));
    }
}

template<typename Tpoint>
inline typename Mouse_<Tpoint>::Type v4d_mouse_event_type(int glfw_state) {
    switch (glfw_state) {
    case GLFW_PRESS:   return Mouse_<Tpoint>::PRESS;
    case GLFW_RELEASE: return Mouse_<Tpoint>::RELEASE;
    default:
        throw std::runtime_error("Invalid mouse action: " + std::to_string(glfw_state));
    }
}

inline Joystick::Type v4d_joystick_event_type(int glfw_state) {
    switch (glfw_state) {
    case GLFW_PRESS:   return Joystick::PRESS;
    case GLFW_RELEASE: return Joystick::RELEASE;
    default:
        throw std::runtime_error("Invalid joystick state: " + std::to_string(glfw_state));
    }
}

// ---------- EventQueue ----------
class EventQueue: public std::deque<std::shared_ptr<Event>> {
    std::mutex mutex_;
    size_t capacity_;
    using parent_t = std::deque<std::shared_ptr<Event>>;
public:
    EventQueue() : capacity_(default_queue_capacity) { assert(capacity_ > 0); }

    void set_capacity(size_t c) {
        assert(c > 0);
        std::unique_lock<std::mutex> lock(mutex_);
        capacity_ = c;
        while (parent_t::size() > capacity_) parent_t::pop_front();
    }

    size_t capacity() const { return capacity_; }

    void push(const std::shared_ptr<Event>& event) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (parent_t::size() == capacity_) {
            parent_t::pop_front();
        }
        parent_t::push_back(event);
    }

    template <typename Tevent>
    bool consume() {
        return consume<Tevent>([](const std::shared_ptr<Tevent>&){ return true; });
    }

    template <typename Tevent, typename Tfn>
    bool consume(Tfn fn) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool found = false;
        std::deque<std::shared_ptr<Event>> remainder;
        for (size_t i = 0; i < parent_t::size(); ++i) {
            std::shared_ptr<Event> event = parent_t::operator[](i);
            std::shared_ptr<Tevent> candidate = std::dynamic_pointer_cast<Tevent>(event);
            if (candidate && fn(candidate)) {
                found = true;
            } else {
                remainder.push_back(event);
            }
        }
        clear_();
        if (!remainder.empty()) parent_t::operator=(std::move(remainder));
        return found;
    }

    template <typename Tevent>
    std::vector<std::shared_ptr<Tevent>> fetch() {
        return fetch<Tevent>([](const std::shared_ptr<Tevent>&){ return true; });
    }

    template <typename Tevent, typename Tfn>
    std::vector<std::shared_ptr<Tevent>> fetch(Tfn fn) {
        std::unique_lock<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<Tevent>> result;
        std::deque<std::shared_ptr<Event>> remainder;
        for (size_t i = 0; i < parent_t::size(); ++i) {
            std::shared_ptr<Event> event = parent_t::operator[](i);
            std::shared_ptr<Tevent> candidate = std::dynamic_pointer_cast<Tevent>(event);
            if (candidate && fn(candidate)) {
                result.push_back(candidate);
            } else {
                remainder.push_back(event);
            }
        }
        clear_();
        if (!remainder.empty()) parent_t::operator=(std::move(remainder));
        return result;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(mutex_);
        return parent_t::empty();
    }
private:
    void clear_() {
        parent_t empty_queue;
        std::swap(*this, empty_queue);
    }
};

// Per-thread queue access
static EventQueue& queue() {
    std::lock_guard<std::mutex> guard(queue_access_mtx);
    auto tid = std::this_thread::get_id();
    auto it_ins = thread_id_map.emplace(tid, thread_id_map.size());
    size_t index = it_ins.first->second;

    std::vector<EventQueue*>& qs = Holder::queue_vector;
    while (qs.size() <= index) {
        qs.push_back(new EventQueue());
    }
    return *qs[index];
}

static void push(const std::shared_ptr<Event>& event) {
    std::lock_guard<std::mutex> guard(queue_access_mtx);
    auto& qs = Holder::queue_vector;
    for (size_t i = 0; i < qs.size(); ++i) {
        qs[i]->push(event);
    }
}

// Per-joystick button snapshot
inline std::map<int, GLFWgamepadstate> last_states;

// Joystick polling
static void poll_joystick_events() {
    const double now = monotonic_seconds();

    for (int jid = 0; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!(glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid))) continue;

        GLFWgamepadstate state{};
        if (!glfwGetGamepadState(jid, &state)) continue;

        // init per-joystick snapshots
        GLFWgamepadstate& last = last_states[jid]; // zero-inited on first use

        // per-button next repeat time (init to -1)
        static std::map<int, std::array<double, GLFW_GAMEPAD_BUTTON_LAST + 1>> next_repeat_at;
        auto [repIt, repNew] = next_repeat_at.try_emplace(jid);
        if (repNew) repIt->second.fill(-1.0);
        auto& nextRep = repIt->second;

        // Buttons: edges + repeats
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
            const bool down_now  = state.buttons[b] == GLFW_PRESS;
            const bool down_prev = last.buttons[b]  == GLFW_PRESS;

            if (down_now != down_prev) {
                auto type = v4d_joystick_event_type(state.buttons[b]); // PRESS/RELEASE
                push(std::make_shared<Joystick>(type, jid, v4d_joystick_button(b)));
                nextRep[b] = down_now ? now + js_repeat_delay : -1.0;
            } else if (down_now) {
                if (nextRep[b] >= 0.0 && now >= nextRep[b]) {
                    push(std::make_shared<Joystick>(Joystick::REPEAT, jid, v4d_joystick_button(b)));
                    nextRep[b] += js_repeat_rate;
                }
            }
        }
        last = state;

        // Axes: edges + repeats while held past deadzone
        auto [initIt, newInit] = init_axis_values.try_emplace(jid);
        auto [prevIt, newPrev] = prev_axis_values.try_emplace(jid);
        auto [axisRepIt, newAxisRep] = axis_next_repeat_at.try_emplace(jid);

        if (newInit)    initIt->second.fill(AXIS_NO_VALUE);
        if (newPrev)    prevIt->second.fill(AXIS_NO_VALUE);
        if (newAxisRep) axisRepIt->second.fill(-1.0);

        auto& init     = initIt->second;
        auto& prev     = prevIt->second;
        auto& nextRepA = axisRepIt->second;

        constexpr float stick_deadzone = 0.10f;  // absolute position deadzone
        constexpr float change_eps     = 0.002f; // per-frame change to emit another event

        for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
            if (init[axis] == AXIS_NO_VALUE) init[axis] = state.axes[axis];

            float val   = state.axes[axis];
            float prevv = (prev[axis] == AXIS_NO_VALUE) ? val : prev[axis];
            float delta = val - prevv;
            prev[axis]  = val;

            Joystick::Axis a = v4d_joystick_axis(axis);

            const bool out_of_deadzone =
                (a == Joystick::LEFT_TRIGGER || a == Joystick::RIGHT_TRIGGER)
                    ? std::fabs(val - init[axis]) > 0.02f   // triggers rest near -1
                    : std::fabs(val) > stick_deadzone;      // sticks rest near 0

            // If not active, stop repeats.
            if (!out_of_deadzone) { nextRepA[axis] = -1.0; continue; }

            const bool moved = std::fabs(delta) > change_eps;

            if (moved) {
                // Normal motion event
                detail::push(std::make_shared<Joystick>(jid, a, init[axis], val, delta));
                // Arm repeats to start after a hold
                nextRepA[axis] = now + js_repeat_delay;
            } else {
                // Held steady past deadzone — emit periodic repeats
                if (nextRepA[axis] < 0.0) {
                    nextRepA[axis] = now + js_repeat_delay; // just became active
                } else if (now >= nextRepA[axis]) {
                    detail::push(std::make_shared<Joystick>(jid, a, init[axis], val, 0.0f)); // repeat 'MOVE'
                    nextRepA[axis] += js_repeat_rate;
                }
            }
        }
    }
}

} // namespace detail

// ---------- Public configuration knobs ----------
inline void set_queue_capacity(std::size_t cap) {
    std::lock_guard<std::mutex> guard(detail::queue_access_mtx);
    assert(cap > 0);
    detail::default_queue_capacity = cap;
    for (auto* q : detail::Holder::queue_vector) {
        if (q) q->set_capacity(cap);
    }
}

inline void set_double_click_time(double seconds) {
    detail::dblclick_time_sec = seconds;
}
inline void set_double_click_distance(double pixels) {
    detail::dblclick_dist2_px2 = pixels * pixels;
}

inline void set_js_repeat_delay(double seconds) {
    detail::js_repeat_delay = seconds;
}
inline void set_js_repeat_rate(double rate) {
    detail::js_repeat_rate = rate;
}

// ---------- init (register callbacks) ----------
template<typename Tpoint>
inline void init(
    KeyCallback         keyboardCallback     = KeyCallback(),
    MouseButtonCallback mouseButtonCallback  = MouseButtonCallback(),
    ScrollCallback      scrollCallback       = ScrollCallback(),
    WindowSizeCallback  windowSizeCallback   = WindowSizeCallback(),
    WindowPosCallback   windowPosCallback    = WindowPosCallback(),
    WindowFocusCallback windowFocusCallback  = WindowFocusCallback(),
    WindowCloseCallback windowCloseCallback  = WindowCloseCallback(),
    CursorPosCallback   cursorPosCallback    = CursorPosCallback(),
    CursorEnterCallback cursorEnterCallback  = CursorEnterCallback(),
    CharCallback        charCallback         = CharCallback()
) {
    GLFWwindow* win = glfwGetCurrentContext();
    assert(win);
    detail::Holder::main_window         = win;
    detail::Holder::keyboardCallback    = keyboardCallback;
    detail::Holder::charCallback        = charCallback;
    detail::Holder::mouseButtonCallback = mouseButtonCallback;
    detail::Holder::scrollCallback      = scrollCallback;
    detail::Holder::cursorPosCallback   = cursorPosCallback;
    detail::Holder::cursorEnterCallback = cursorEnterCallback;
    detail::Holder::windowSizeCallback  = windowSizeCallback;
    detail::Holder::windowPosCallback   = windowPosCallback;
    detail::Holder::windowFocusCallback = windowFocusCallback;
    detail::Holder::windowCloseCallback = windowCloseCallback;

    glfwGetWindowSize(win, &detail::Holder::window_size.first, &detail::Holder::window_size.second);

    // Keyboard
    glfwSetKeyCallback(win,
        [](GLFWwindow *window, int key, int scancode, int action, int mods) {
            if (!detail::Holder::keyboardCallback || !detail::Holder::keyboardCallback(window, key, scancode, action, mods)) {
                if (key != GLFW_KEY_UNKNOWN) {
                    Keyboard::Key k = detail::v4d_key(key);
                    Keyboard::Type type = detail::v4d_keyboard_event_type(action);
                    auto event = std::make_shared<Keyboard>(type, k);
                    detail::push(event);
                }
            }
        });

    // Character input (needed by ImGui text fields and text-aware apps)
    glfwSetCharCallback(win,
        [](GLFWwindow *window, unsigned int codepoint) {
            if (!detail::Holder::charCallback ||
                !detail::Holder::charCallback(window, codepoint)) {
                // No native text event; reserved for future use.
            }
        });

    // Mouse button (+ double click)
    glfwSetMouseButtonCallback(win,
        [](GLFWwindow *window, int button, int action, int mods) {
            if (!detail::Holder::mouseButtonCallback || !detail::Holder::mouseButtonCallback(window, button, action, mods)) {
                if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST) return;
                auto mouseButton = detail::v4d_mouse_button<Tpoint>(button);
                auto type = detail::v4d_mouse_event_type<Tpoint>(action);

                double x, y;
                glfwGetCursorPos(window, &x, &y);
                Tpoint position = point_traits<Tpoint>::make(x, y);

                // Emit PRESS/RELEASE event
                auto event = std::make_shared<Mouse_<Tpoint>>(type, mouseButton, position);
                detail::push(event);

                // Double-click detection on PRESS
                if (type == Mouse_<Tpoint>::PRESS) {
                    const double now = detail::monotonic_seconds();
                    const double px = point_traits<Tpoint>::x(position);
                    const double py = point_traits<Tpoint>::y(position);
                    const double dt = now - detail::last_click_time[button];
                    const double dx = px - detail::last_click_x[button];
                    const double dy = py - detail::last_click_y[button];
                    const double d2 = dx*dx + dy*dy;
                    if (dt <= detail::dblclick_time_sec && d2 <= detail::dblclick_dist2_px2) {
                        auto dbl = std::make_shared<Mouse_<Tpoint>>(Mouse_<Tpoint>::DOUBLE_CLICK, mouseButton, position);
                        detail::push(dbl);
                    }
                    detail::last_click_time[button] = now;
                    detail::last_click_x[button] = px;
                    detail::last_click_y[button] = py;
                }
            }
        });

    // Scroll
    glfwSetScrollCallback(win,
        [](GLFWwindow *window, double xoffset, double yoffset) {
            if (!detail::Holder::scrollCallback || !detail::Holder::scrollCallback(window, xoffset, yoffset)) {
                Tpoint offset = point_traits<Tpoint>::make(xoffset, yoffset);
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                Tpoint position = point_traits<Tpoint>::make(x, y);
                auto event = std::make_shared<Mouse_<Tpoint>>(Mouse_<Tpoint>::SCROLL, position, offset);
                detail::push(event);
            }
        });

    // Cursor move / drag
    glfwSetCursorPosCallback(win,
        [](GLFWwindow *window, double xpos, double ypos) {
            static Tpoint prevMousePos = point_traits<Tpoint>::make(0, 0);

            if (!detail::Holder::cursorPosCallback || !detail::Holder::cursorPosCallback(window, xpos, ypos)) {
                Tpoint position = point_traits<Tpoint>::make(xpos, ypos);
                bool pressed = false;
                for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
                    if (glfwGetMouseButton(window, button) == GLFW_PRESS) {
                        pressed = true;
                        break;
                    }
                }
                if (pressed) {
                    Tpoint delta = point_traits<Tpoint>::sub(position, prevMousePos);
                    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
                        if (glfwGetMouseButton(window, button) == GLFW_PRESS) {
                            auto v4d_button = detail::v4d_mouse_button<Tpoint>(button);
                            auto event = std::make_shared<Mouse_<Tpoint>>(Mouse_<Tpoint>::DRAG, v4d_button, position, delta);
                            detail::push(event);
                        }
                    }
                    prevMousePos = position;
                } else {
                    auto event = std::make_shared<Mouse_<Tpoint>>(Mouse_<Tpoint>::MOVE, position);
                    detail::push(event);
                    prevMousePos = position;
                }
            }
        });

    // Cursor enter/leave (hover)
    glfwSetCursorEnterCallback(win,
        [](GLFWwindow* window, int entered) {
            if (!detail::Holder::cursorEnterCallback || !detail::Holder::cursorEnterCallback(window, entered)) {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                Tpoint position = point_traits<Tpoint>::make(x, y);
                auto type = entered ? Mouse_<Tpoint>::HOVER_ENTER : Mouse_<Tpoint>::HOVER_EXIT;
                auto event = std::make_shared<Mouse_<Tpoint>>(type, position);
                detail::push(event);
            }
        });

    // Window size (no Y-flip; reports raw width/height)
    glfwSetWindowSizeCallback(win,
        [](GLFWwindow *window, int width, int height) {
            detail::Holder::window_size = {width, height};
            if (!detail::Holder::windowSizeCallback || !detail::Holder::windowSizeCallback(window, width, height)) {
                Tpoint sz = point_traits<Tpoint>::make(width, height);
                auto event = std::make_shared<Window_<Tpoint>>(Window_<Tpoint>::RESIZE, sz);
                detail::push(event);
            }
        });

    // Window pos (RAW OS COORDINATES — NO Y-FLIP, per your decision)
    glfwSetWindowPosCallback(win,
        [](GLFWwindow *window, int xpos, int ypos) {
            if (!detail::Holder::windowPosCallback || !detail::Holder::windowPosCallback(window, xpos, ypos)) {
                Tpoint position = point_traits<Tpoint>::make(xpos, ypos);
                auto event = std::make_shared<Window_<Tpoint>>(Window_<Tpoint>::MOVE, position);
                detail::push(event);
            }
        });

    // Focus
    glfwSetWindowFocusCallback(win,
        [](GLFWwindow *window, int focused) {
            if (!detail::Holder::windowFocusCallback || !detail::Holder::windowFocusCallback(window, focused)) {
                typename Window_<Tpoint>::Type type = focused ? Window_<Tpoint>::FOCUS : Window_<Tpoint>::UNFOCUS;
                auto event = std::make_shared<Window_<Tpoint>>(type);
                detail::push(event);
            }
        });

    // Close
    glfwSetWindowCloseCallback(win,
        [](GLFWwindow *window) {
            if (!detail::Holder::windowCloseCallback || !detail::Holder::windowCloseCallback(window)) {
                auto event = std::make_shared<Window_<Tpoint>>(Window_<Tpoint>::CLOSE);
                detail::push(event);
            }
        });
}

// ---------- Consume / Fetch ----------
template<typename Tevent>
inline bool consume(){
    return detail::queue().consume<Tevent>();
}

template<typename Tevent>
inline bool consume(const typename Tevent::Type& t){
    return detail::queue().consume<Tevent>([t](std::shared_ptr<Tevent> ev){ return ev->type() == t; });
}

inline bool consume(const Keyboard::Type& t, const Keyboard::Key& k){
    return detail::queue().consume<Keyboard>([t,k](std::shared_ptr<Keyboard> ev){ return ev->type() == t && ev->key() == k; });
}

template<typename Tmouse>
inline bool consume(const typename Tmouse::Type& t, const typename Tmouse::Button& b){
    return detail::queue().consume<Tmouse>([t,b](std::shared_ptr<Tmouse> ev){ return ev->type() == t && ev->button() == b; });
}

inline bool consume(const Joystick::Type& t, const Joystick::Button& b){
    return detail::queue().consume<Joystick>([t,b](std::shared_ptr<Joystick> ev){ return ev->type() == t && ev->button() == b; });
}

inline bool consume(const Joystick::Type& t, const Joystick::Axis& a){
    return detail::queue().consume<Joystick>([t,a](std::shared_ptr<Joystick> ev){ return ev->type() == t && ev->axis() == a; });
}

template<typename Tevent>
inline bool consume(std::function<bool(const Tevent&)> fn){
    return detail::queue().consume<Tevent>([fn](std::shared_ptr<Tevent> ev){ return fn(*ev.get()); });
}

template<typename Tevent>
inline std::vector<std::shared_ptr<Tevent>> fetch(){
    return detail::queue().fetch<Tevent>();
}

template<typename Tevent>
inline std::vector<std::shared_ptr<Tevent>> fetch(const typename Tevent::Type& t){
    return detail::queue().fetch<Tevent>([t](std::shared_ptr<Tevent> ev){ return ev->type() == t; });
}

inline std::vector<std::shared_ptr<Keyboard>> fetch(const Keyboard::Type& t, const Keyboard::Key& k){
    return detail::queue().fetch<Keyboard>([t,k](std::shared_ptr<Keyboard> ev){ return ev->type() == t && ev->key() == k; });
}

template<typename Tmouse>
inline std::vector<std::shared_ptr<Tmouse>> fetch(const typename Tmouse::Type& t, const typename Tmouse::Button& b){
    return detail::queue().fetch<Tmouse>([t,b](std::shared_ptr<Tmouse> ev){ return ev->type() == t && ev->button() == b; });
}

inline std::vector<std::shared_ptr<Joystick>> fetch(const Joystick::Type& t, const Joystick::Button& b){
    return detail::queue().fetch<Joystick>([t,b](std::shared_ptr<Joystick> ev){ return ev->type() == t && ev->button() == b; });
}

inline std::vector<std::shared_ptr<Joystick>> fetch(const Joystick::Type& t, const Joystick::Axis& a){
    return detail::queue().fetch<Joystick>([t,a](std::shared_ptr<Joystick> ev){ return ev->type() == t && ev->axis() == a; });
}

template<typename Tevent>
inline std::vector<std::shared_ptr<Tevent>> fetch(std::function<bool(const Tevent&)> fn){
    return detail::queue().fetch<Tevent>([fn](std::shared_ptr<Tevent> ev){ return fn(*ev.get()); });
}

// ---------- Poll ----------
EVENT_API_EXPORT inline void poll() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    assert(detail::Holder::main_window);
    glfwPollEvents();
    detail::poll_joystick_events();
}

} // namespace gwe
#endif  // MODULES_V4D_INCLUDE_OPENCV2_V4D_DETAIL_EVENTS_HPP_
