// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>

int v4d_cube_main();
int v4d_many_cubes_main();
int v4d_video_main(int argc, char **argv);
int v4d_nanovg_main(int argc, char **argv);
int v4d_shader_main(int argc, char **argv);
int v4d_font_main();
int v4d_pedestrian_main(int argc, char **argv);
int v4d_optflow_main(int argc, char **argv);
int v4d_beauty_main(int argc, char **argv);
#define main v4d_cube_main
#include "cube-demo.cpp"
#undef main
#define main v4d_many_cubes_main
#include "many_cubes-demo.cpp"
#undef main
#define main v4d_video_main
#include "video-demo.cpp"
#undef main
#define main v4d_nanovg_main
#include "nanovg-demo.cpp"
#undef main
#define main v4d_shader_main
#include "shader-demo.cpp"
#undef main
#define main v4d_font_main
#include "font-demo.cpp"
#undef main
#define main v4d_pedestrian_main
#include "pedestrian-demo.cpp"
#undef main
#define main v4d_optflow_main
#include "optflow-demo.cpp"
#undef main
#define main v4d_beauty_main
#include "beauty-demo.cpp"
#undef main

class MontageDemoPlan : public Plan {
	const cv::Size tiling_  = cv::Size(3, 3);
	const cv::Size tileSz_ = cv::Size(640, 360);

	std::vector<Plan*> plans_ = {
		new CubeDemoPlan(tileSz_),
		new ManyCubesDemoPlan(tileSz_),
		new VideoDemoPlan(tileSz_),
		new NanoVGDemoPlan(tileSz_),
		new ShaderDemoPlan(tileSz_),
		new FontDemoPlan(tileSz_),
		new PedestrianDemoPlan(tileSz_),
		new BeautyDemoPlan(tileSz_),
		new OptflowDemoPlan(tileSz_)
	};
	struct Frames {
		std::vector<cv::UMat> results_ = std::vector<cv::UMat>(9);
		cv::UMat captured;
	} frames_;

	cv::Size_<float> scale_;
public:
	MontageDemoPlan(const cv::Size& sz) : Plan(sz) {
		CV_Assert(plans_.size() == frames_.results_.size() &&  plans_.size() == size_t(tiling_.width * tiling_.height));
		scale_ = cv::Size_<float>(float(size().width) / tileSz_.width, float(size().height) / tileSz_.height);
	}

	virtual void setup(cv::Ptr<V4D> window) override {
		for(auto* plan : plans_) {
			plan->setup(window);
		}
	}

	virtual void infer(cv::Ptr<V4D> window) override {
		window->nvgCtx()->setScale(scale_);
		window->capture();
		window->setDisableIO(true);
		window->fb([](cv::UMat& framebuffer, const cv::Size& tileSize, cv::UMat& captured){
			cv::resize(framebuffer, captured, tileSize);
		}, tileSz_, frames_.captured);


		for(size_t i = 0; i < plans_.size(); ++i) {
			auto* plan = plans_[i];
			window->fb([](cv::UMat& framebuffer, const cv::Size& tileSize, const cv::UMat& captured){
				framebuffer = cv::Scalar::all(0);
				captured.copyTo(framebuffer(cv::Rect(0, tileSize.height * 2, tileSize.width, tileSize.height)));
			}, tileSz_, frames_.captured);
			plan->infer(window);
			window->fb([](const cv::UMat& framebuffer, cv::UMat& result){
				framebuffer.copyTo(result);
			}, frames_.results_[i]);
		}

		window->fb([](cv::UMat& framebuffer, const cv::Size& tileSz, const Frames& frames){
			int w = tileSz.width;
			int h = tileSz.height;
			framebuffer = cv::Scalar::all(0);

			for(size_t x = 0; x < 3; ++x)
				for(size_t y = 0; y < 3; ++y)
					frames.results_[x * 3 + y]		(cv::Rect(0, h * 2, w, h)).copyTo(framebuffer(cv::Rect(w * x, h * y, w, h)));
		}, tileSz_, frames_);

		window->setDisableIO(false);
		window->write();
	}

	virtual void teardown(cv::Ptr<V4D> window) override {
		for(auto* plan : plans_) {
			plan->setup(window);
		}
	}
};

int main(int argc, char** argv) {
#ifndef __EMSCRIPTEN__
	if (argc != 3) {
        cerr << "Usage: montage-demo <video-file> <number of extra threads>" << endl;
        exit(1);
    }
	constexpr double FPS = 60;
	constexpr const char* OUTPUT_FILENAME = "montage-demo.mkv";
#else
	CV_UNUSED(argc);
	CV_UNUSED(argv);
#endif
    using namespace cv::v4d;
    cv::Ptr<MontageDemoPlan> plan = new MontageDemoPlan(cv::Size(1920, 1080));
    cv::Ptr<V4D> window = V4D::make(plan->size(), "Montage Demo", ALL);
#ifndef __EMSCRIPTEN__
    //Creates a source from a file or a device
    auto src = makeCaptureSource(window, argv[1]);
    window->setSource(src);
    //Creates a writer sink (which might be hardware accelerated)
    auto sink = makeWriterSink(window, OUTPUT_FILENAME, FPS, plan->size());
    window->setSink(sink);
#endif
    window->run(plan, atoi(argv[2]));

    return 0;
}

