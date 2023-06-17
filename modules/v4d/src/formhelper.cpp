// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

#include "opencv2/v4d/formhelper.hpp"

namespace cv {
namespace v4d {

FormHelper::FormHelper(nanogui::Screen* screen) :
        nanogui::FormHelper(screen) {
    assert(screen != nullptr);
}

FormHelper::~FormHelper() {
}

Dialog* FormHelper::makeDialog(int x, int y, const string& title) {
    auto* win = new cv::v4d::Dialog(m_screen, x, y, title);
    this->set_window(win);
    return win;
}

nanogui::Label* FormHelper::makeGroup(const string& label) {
    return add_group(label);
}

nanogui::detail::FormWidget<bool>* FormHelper::makeFormVariable(const string& name, bool& v,
        const string& tooltip, bool visible, bool enabled) {
    auto var = add_variable(name, v);
    var->set_enabled(enabled);
    var->set_visible(visible);
    if (!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

nanogui::detail::FormWidget<nanogui::Color>* FormHelper::makeColorPicker(const string& label, nanogui::Color& color,
        const string& tooltip, std::function<void(const nanogui::Color)> fn, bool visible,
        bool enabled) {
    auto* colorPicker = add_variable(label, color);
    colorPicker->set_enabled(enabled);
    colorPicker->set_visible(visible);
    if (!tooltip.empty())
        colorPicker->set_tooltip(tooltip);
    if (fn)
        colorPicker->set_final_callback(fn);

    return colorPicker;
}

nanogui::Button* FormHelper::makeButton(const string& caption, std::function<void()> fn) {
    return add_button(caption, fn);
}

} /* namespace viz */
} /* namespace cv */
