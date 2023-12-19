#include <opencv2/v4d/v4d.hpp>

using namespace cv;
using namespace cv::v4d;

class FontWithGuiPlan: public Plan {
	static struct Params {
		float size_ = 40.0f;
		cv::Scalar_<float> color_ = {1.0f, 0.0f, 0.0f, 1.0f};
	} params_;
	//The text
	string hw_ = "hello world";
public:
	FontWithGuiPlan(const cv::Rect& vp) : Plan(vp) {
		Global::registerShared(params_);
	}

	void gui(Ptr<V4D> window) override {
		window->imgui([](Ptr<V4D> win, Params& params) {
			CV_UNUSED(win);
			using namespace ImGui;
			Begin("Settings");
			SliderFloat("Font Size", &params.size_, 1.0f, 100.0f);
			ColorPicker4("Text Color", params.color_.val);
			End();
		}, params_);
	}

	void infer(Ptr<V4D> window) override {
		//Render the text at the center of the screen using parameters from the GUI.
		window->nvg([](const Size& sz, const string& str, const Params params) {
			using namespace cv::v4d::nvg;
			clear();
			fontSize(params.size_);
			fontFace("sans-bold");
			fillColor(params.color_ * 255.0);
			textAlign(NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			text(sz.width / 2.0, sz.height / 2.0, str.c_str(), str.c_str() + str.size());
		}, R(size()), R(hw_), R_C(params_));
	}
};

FontWithGuiPlan::Params FontWithGuiPlan::params_;

int main() {
	cv::Rect viewport(0, 0, 960,960);
    Ptr<V4D> window = V4D::make(viewport.size(), "Font Rendering with GUI", AllocateFlags::NANOVG | AllocateFlags::IMGUI);
	window->run<FontWithGuiPlan>(0, viewport);
}

