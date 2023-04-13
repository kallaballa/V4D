#include <opencv2/viz2d/viz2d.hpp>

using namespace cv;
using namespace cv::viz;

constexpr int WIDTH = 1280;
constexpr int HEIGHT = 720;

int main() {
	//Creates a Viz2D object for on screen rendering
	Ptr<Viz2D> v2d = Viz2D::make(Size(WIDTH, HEIGHT), "Show image");
	//Read an image as UMat
	UMat image = imread(samples::findFile("lena.jpg")).getUMat(ACCESS_READ);
	UMat resized;
	//Resize the image to framebuffer size
	resize(image, resized, v2d->getFrameBufferSize());
	v2d->fb([&](const UMat& framebuffer) {
		//Color convert the resized UMat. The framebuffer has alpha.
		cvtColor(resized, framebuffer, COLOR_RGB2BGRA);
	});
	//Display the framebuffer in the native window in an endless loop
	v2d->run(v2d->display);
}

