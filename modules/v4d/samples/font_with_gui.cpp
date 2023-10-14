#include <opencv2/v4d/v4d.hpp>

using namespace cv;
using namespace cv::v4d;

//The font size
static float size = 40.0f;
//The text hue
static std::vector<float> color = {1.0f, 0.0f, 0.0f};

int main() {
    Ptr<V4D> window = V4D::make(960, 960, "Font Rendering with GUI");

	//Setup the GUI
	window->imgui([&](ImGuiContext* ctx) {
	    using namespace ImGui;
	    SetCurrentContext(ctx);
	    Begin("Settings");
	    SliderFloat("Font Size", &size, 1.0f, 100.0f);
		ColorPicker3("Text Color", color.data());
		End();
	});

	class FontWithGuiPlan: public Plan {
		//The text
		string hw_ = "hello world";
	public:
		void infere(Ptr<V4D> win) override {
			//Render the text at the center of the screen using parameters from the GUI.
			win->nvg([](const Size& sz, const string& str, const float& s, const std::vector<float>& c) {
				using namespace cv::v4d::nvg;
				clear();
				fontSize(s);
				fontFace("sans-bold");
				fillColor(Scalar(c[2] * 255, c[1] * 255, c[0] * 255, 255));
				textAlign(NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				text(sz.width / 2.0, sz.height / 2.0, str.c_str(), str.c_str() + str.size());
			}, win->fbSize(), hw_, size, color);
		}
	};

	window->run<FontWithGuiPlan>(0);
}

