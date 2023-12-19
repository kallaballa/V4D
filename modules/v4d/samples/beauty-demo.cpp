// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
// Copyright Amir Hassan (kallaballa) <amir@viel-zu.org>
#include <opencv2/v4d/v4d.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/face.hpp>
#include <opencv2/stitching/detail/blenders.hpp>
#include <opencv2/tracking.hpp>

#include <vector>
#include <string>

using std::vector;
using std::string;

/*!
 * Data structure holding the points for all face landmarks
 */
struct FaceFeatures {
    cv::Rect faceRect_;
    vector<cv::Point2f> chin_;
    vector<cv::Point2f> top_nose_;
    vector<cv::Point2f> bottom_nose_;
    vector<cv::Point2f> left_eyebrow_;
    vector<cv::Point2f> right_eyebrow_;
    vector<cv::Point2f> left_eye_;
    vector<cv::Point2f> right_eye_;
    vector<cv::Point2f> outer_lips_;
    vector<cv::Point2f> inside_lips_;
    FaceFeatures() {};
    FaceFeatures(const cv::Rect &faceRect, const vector<cv::Point2f> &shape, double local_scale) {
        //calculate the face rectangle
        faceRect_ = cv::Rect(faceRect.x / local_scale, faceRect.y / local_scale, faceRect.width / local_scale, faceRect.height / local_scale);

        /** Copy all features **/
        size_t i = 0;
        // Around Chin. Ear to Ear
        for (i = 0; i <= 16; ++i)
            chin_.push_back(shape[i] / local_scale);
        // left eyebrow
        for (; i <= 21; ++i)
            left_eyebrow_.push_back(shape[i] / local_scale);
        // Right eyebrow
        for (; i <= 26; ++i)
            right_eyebrow_.push_back(shape[i] / local_scale);
        // Line on top of nose
        for (; i <= 30; ++i)
            top_nose_.push_back(shape[i] / local_scale);
        // Bottom part of the nose
        for (; i <= 35; ++i)
            bottom_nose_.push_back(shape[i] / local_scale);
        // Left eye
        for (; i <= 41; ++i)
            left_eye_.push_back(shape[i] / local_scale);
        // Right eye
        for (; i <= 47; ++i)
            right_eye_.push_back(shape[i] / local_scale);
        // Lips outer part
        for (; i <= 59; ++i)
            outer_lips_.push_back(shape[i] / local_scale);
        // Lips inside part
        for (; i <= 67; ++i)
            inside_lips_.push_back(shape[i] / local_scale);
    }

    //Concatenates all feature points
    vector<cv::Point2f> points() const {
        vector<cv::Point2f> allPoints;
        allPoints.insert(allPoints.begin(), chin_.begin(), chin_.end());
        allPoints.insert(allPoints.begin(), top_nose_.begin(), top_nose_.end());
        allPoints.insert(allPoints.begin(), bottom_nose_.begin(), bottom_nose_.end());
        allPoints.insert(allPoints.begin(), left_eyebrow_.begin(), left_eyebrow_.end());
        allPoints.insert(allPoints.begin(), right_eyebrow_.begin(), right_eyebrow_.end());
        allPoints.insert(allPoints.begin(), left_eye_.begin(), left_eye_.end());
        allPoints.insert(allPoints.begin(), right_eye_.begin(), right_eye_.end());
        allPoints.insert(allPoints.begin(), outer_lips_.begin(), outer_lips_.end());
        allPoints.insert(allPoints.begin(), inside_lips_.begin(), inside_lips_.end());

        return allPoints;
    }

    //Returns all feature points in fixed order
    vector<vector<cv::Point2f>> features() const {
        return {chin_,
            top_nose_,
            bottom_nose_,
            left_eyebrow_,
            right_eyebrow_,
            left_eye_,
            right_eye_,
            outer_lips_,
            inside_lips_};
    }

    size_t empty() const {
        return points().empty();
    }
};

using namespace cv::v4d;

class BeautyDemoPlan : public Plan {
public:
	using Plan::Plan;
private:
	cv::Size downSize_;

	static struct Params {
		//Saturation boost factor for eyes and lips
		float eyesAndLipsSaturation_ = 1.85f;
		//Saturation boost factor for skin
		float skinSaturation_ = 1.45f;
		//Contrast factor skin
		float skinContrast_ = 0.65f;
		//Show input and output side by side
		bool sideBySide_ = false;
		//Scale the video to the window size
		bool stretch_ = true;
	} params_;

	struct Cache {
	    vector<cv::UMat> channels_;
	    cv::UMat hls_;
	    cv::UMat frameOutFloat_;
	    cv::UMat bgra_;
	} cache_;

	struct Frames {
		//BGR
		cv::UMat orig_, down_, contrast_, faceOval_, eyesAndLips_, skin_;
		cv::UMat lhalf_;
		cv::UMat rhalf_;
		//GREY
		cv::UMat faceSkinMaskGrey_, eyesAndLipsMaskGrey_, backgroundMaskGrey_;
	} frames_;

	//results of face detection and facemark
	struct Face {
		vector<vector<cv::Point2f>> shapes_;
		std::vector<cv::Rect> faceRects_;
		bool found_ = false;
		FaceFeatures features_;
	} face_;

	//the frame holding the final composed image
	cv::UMat frameOut_;
	cv::Ptr<cv::face::Facemark> facemark_ = cv::face::createFacemarkLBF();
	//Blender (used to put the different face parts back together)
	cv::Ptr<cv::detail::MultiBandBlender> blender_ = new cv::detail::MultiBandBlender(true, 5);
	//Face detector
	cv::Ptr<cv::FaceDetectorYN> detector_;

	//based on the detected FaceFeatures it guesses a decent face oval and draws a mask for it.
	static void draw_face_oval_mask(const FaceFeatures &ff) {
	    using namespace cv::v4d::nvg;
	    clear();

	    cv::RotatedRect rotRect = cv::fitEllipse(ff.points());

	    beginPath();
	    fillColor(cv::Scalar(255, 255, 255, 255));
	    ellipse(rotRect.center.x, rotRect.center.y * 0.875, rotRect.size.width / 2, rotRect.size.height / 1.75);
	    rotate(rotRect.angle);
	    fill();
	}

	//Draws a mask consisting of eyes and lips areas (deduced from FaceFeatures)
	static void draw_face_eyes_and_lips_mask(const FaceFeatures &ff) {
	    using namespace cv::v4d::nvg;
	    clear();
	    vector<vector<cv::Point2f>> features = ff.features();
	    for (size_t j = 5; j < 8; ++j) {
	        beginPath();
	        fillColor(cv::Scalar(255, 255, 255, 255));
	        moveTo(features[j][0].x, features[j][0].y);
	        for (size_t k = 1; k < features[j].size(); ++k) {
	            lineTo(features[j][k].x, features[j][k].y);
	        }
	        closePath();
	        fill();
	    }

	    beginPath();
	    fillColor(cv::Scalar(0, 0, 0, 255));
	    moveTo(features[8][0].x, features[8][0].y);
	    for (size_t k = 1; k < features[8].size(); ++k) {
	        lineTo(features[8][k].x, features[8][k].y);
	    }
	    closePath();
	    fill();
	}

	//adjusts the saturation of a UMat
	static void adjust_saturation(const cv::UMat &srcBGR, cv::UMat &dstBGR, float factor, Cache& cache) {
	    cvtColor(srcBGR, cache.hls_, cv::COLOR_BGR2HLS);
	    split(cache.hls_, cache.channels_);
	    cv::multiply(cache.channels_[2], factor, cache.channels_[2]);
	    merge(cache.channels_, cache.hls_);
	    cvtColor(cache.hls_, dstBGR, cv::COLOR_HLS2BGR);
	}
public:
	BeautyDemoPlan(const cv::Rect& vp) : Plan(vp) {
		Global::registerShared(params_);
	}

	void gui(cv::Ptr<V4D> window) override {
		window->imgui([](cv::Ptr<V4D> win, Params& params){
			using namespace ImGui;
			Begin("Effect");
			Text("Display");
			Checkbox("Side by side", &params.sideBySide_);
			if(Checkbox("Stetch", &params.stretch_)) {
				win->setStretching(true);
			} else
				win->setStretching(false);

			if(Button("Fullscreen")) {
				win->setFullscreen(!win->isFullscreen());
			};

			if(Button("Offscreen")) {
				win->setVisible(!win->isVisible());
			};

			Text("Face Skin");
			SliderFloat("Saturation", &params.skinSaturation_, 0.0f, 10.0f);
			SliderFloat("Contrast", &params.skinContrast_, 0.0f, 2.0f);
			Text("Eyes and Lips");
			SliderFloat("Saturation ", &params.eyesAndLipsSaturation_, 0.0f, 10.0f);
			End();
		}, params_);
	}
	void setup(cv::Ptr<V4D> window) override {
		window->setStretching(params_.stretch_);
		window->plain([](const cv::Size& sz, cv::Size& downSize, cv::UMat& frameOut, cv::Ptr<cv::face::Facemark>& facemark, cv::Ptr<cv::FaceDetectorYN>& detector) {
	    	int w = sz.width;
	    	int h = sz.height;
	    	downSize = { std::min(w, std::max(640, int(round(w / 2.0)))), std::min(h, std::max(360, int(round(h / 2.0)))) };
	    	detector = cv::FaceDetectorYN::create("modules/v4d/assets/models/face_detection_yunet_2023mar.onnx", "", downSize, 0.9, 0.3, 5000, cv::dnn::DNN_BACKEND_OPENCV, cv::dnn::DNN_TARGET_OPENCL);
	    	facemark->loadModel("modules/v4d/assets/models/lbfmodel.yaml");
			frameOut.create(sz, CV_8UC3);
		}, R(size()), RW(downSize_), RW(frameOut_), RW(facemark_), RW(detector_));
	}
	void infer(cv::Ptr<V4D> window) override {
		window->capture();

		//Save the video frame as BGR
		window->fb([](const cv::UMat& framebuffer, const cv::Size& downSize, Frames& frames) {
			cvtColor(framebuffer, frames.orig_, cv::COLOR_BGRA2BGR);

			//Downscale the video frame for face detection
			cv::resize(frames.orig_, frames.down_, downSize);
		}, R(downSize_), RW(frames_));

		window->plain([](const cv::Size sz, cv::Ptr<cv::FaceDetectorYN>& detector, cv::Ptr<cv::face::Facemark>& facemark, const cv::UMat& down, Face& face) {
			face.shapes_.clear();
			cv::Mat faces;
			//Detect faces in the down-scaled image
			detector->detect(down, faces);
			//Only add the first face
			cv::Rect faceRect;
			if(!faces.empty())
				faceRect = cv::Rect(int(faces.at<float>(0, 0)), int(faces.at<float>(0, 1)), int(faces.at<float>(0, 2)), int(faces.at<float>(0, 3)));
			face.faceRects_ = {faceRect};
			//find landmarks if faces have been detected
			face.found_ = !faceRect.empty() && facemark->fit(down, face.faceRects_, face.shapes_);
			if(face.found_)
				face.features_ = FaceFeatures(face.faceRects_[0], face.shapes_[0], float(down.size().width) / sz.width);
		}, R(size()), RW(detector_), RW(facemark_), R(frames_.down_), RW(face_));

		window->branch(isTrue_, R(face_.found_))
			->nvg([](const FaceFeatures& features) {
				//Draw the face oval of the first face
				draw_face_oval_mask(features);
			}, R(face_.features_))
			->fb([](const cv::UMat& framebuffer, cv::UMat& faceOval) {
				//Convert/Copy the mask
				cvtColor(framebuffer, faceOval, cv::COLOR_BGRA2GRAY);
			}, RW(frames_.faceOval_))
			->nvg([](const FaceFeatures& features) {
				//Draw eyes eyes and lips areas of the first face
				draw_face_eyes_and_lips_mask(features);
			}, R(face_.features_))
			->fb([](const cv::UMat &framebuffer, cv::UMat& eyesAndLipsMaskGrey) {
				//Convert/Copy the mask
				cvtColor(framebuffer, eyesAndLipsMaskGrey, cv::COLOR_BGRA2GRAY);
			}, RW(frames_.eyesAndLipsMaskGrey_))
			->plain([](Frames& frames, const Params& params, Cache& cache) {
				//Create the skin mask
				cv::subtract(frames.faceOval_, frames.eyesAndLipsMaskGrey_, frames.faceSkinMaskGrey_);
				//Create the background mask
				cv::bitwise_not(frames.faceOval_, frames.backgroundMaskGrey_);
				//boost saturation of eyes and lips
				adjust_saturation(frames.orig_,  frames.eyesAndLips_, params.eyesAndLipsSaturation_, cache);
				//reduce skin contrast
				multiply(frames.orig_, cv::Scalar::all(params.skinContrast_), frames.contrast_);
				//fix skin brightness
				add(frames.contrast_, cv::Scalar::all((1.0 - params.skinContrast_) / 2.0) * 255.0, frames.contrast_);
				//boost skin saturation
				adjust_saturation(frames.contrast_, frames.skin_, params.skinSaturation_, cache);
			}, RW(frames_), R_C(params_), RW(cache_))
			->plain([](cv::Ptr<cv::detail::MultiBandBlender>& bl, const Frames& frames, cv::UMat& frameOut, Cache& cache) {
				CV_Assert(!frames.skin_.empty());
				CV_Assert(!frames.eyesAndLips_.empty());
				//piece it all together
				bl->prepare(cv::Rect(0, 0, frames.skin_.cols, frames.skin_.rows));
				bl->feed(frames.skin_, frames.faceSkinMaskGrey_, cv::Point(0, 0));
				bl->feed(frames.orig_, frames.backgroundMaskGrey_, cv::Point(0, 0));
				bl->feed(frames.eyesAndLips_, frames.eyesAndLipsMaskGrey_, cv::Point(0, 0));
				bl->blend(cache.frameOutFloat_, cv::UMat());
				CV_Assert(!cache.frameOutFloat_.empty());
				cache.frameOutFloat_.convertTo(frameOut, CV_8U, 1.0);
			}, RW(blender_), R(frames_), RW(frameOut_), RW(cache_))
			->plain([](const cv::Size& sz, const cv::UMat& orig, cv::UMat& frameOut, cv::UMat& lhalf, cv::UMat& rhalf, const Params& params) {
				if (params.sideBySide_) {
					//create side-by-side view with a result
					cv::resize(orig, lhalf, cv::Size(0, 0), 0.5, 0.5);
					cv::resize(frameOut, rhalf, cv::Size(0, 0), 0.5, 0.5);

					frameOut = cv::Scalar::all(0);
					lhalf.copyTo(frameOut(cv::Rect(0, sz.height / 2.0, lhalf.size().width, lhalf.size().height)));
					rhalf.copyTo(frameOut(cv::Rect(sz.width / 2.0, sz.height / 2.0, lhalf.size().width, lhalf.size().height)));
				}
			}, R(size()), R(frames_.orig_), RW(frameOut_), RW(frames_.lhalf_), RW(frames_.rhalf_), R_C(params_))
		->elseBranch()
			->plain([](const cv::Size& sz, const cv::UMat& orig, cv::UMat& frameOut, cv::UMat& lhalf, const Params& params) {
				if (params.sideBySide_) {
					//create side-by-side view without a result (using the input image for both sides)
					frameOut = cv::Scalar::all(0);
					cv::resize(orig, lhalf, cv::Size(0, 0), 0.5, 0.5);
					lhalf.copyTo(frameOut(cv::Rect(0, sz.height / 2.0, lhalf.size().width, lhalf.size().height)));
					lhalf.copyTo(frameOut(cv::Rect(sz.width / 2.0, sz.height / 2.0, lhalf.size().width, lhalf.size().height)));
				} else {
					orig.copyTo(frameOut);
				}
			}, R(size()), R(frames_.orig_), RW(frameOut_), RW(frames_.lhalf_), R_C(params_))
		->endBranch();

		//write the result to the framebuffer
		window->fb([](cv::UMat& framebuffer, const cv::UMat& f, Cache& cache) {
			cvtColor(f, cache.bgra_, cv::COLOR_BGR2BGRA);
			cv::resize(cache.bgra_, framebuffer, framebuffer.size());
		}, R(frameOut_), RW(cache_));
	}
};

BeautyDemoPlan::Params BeautyDemoPlan::params_;

int main(int argc, char **argv) {
	if (argc != 2) {
        cerr << "Usage: beauty-demo <input-video-file>" << endl;
        exit(1);
    }

	cv::Rect viewport(0, 0, 1280, 720);
    cv::Ptr<V4D> window = V4D::make(viewport.size(), "Beautification Demo", AllocateFlags::NANOVG | AllocateFlags::IMGUI);
    auto src = Source::make(window, argv[1]);
    window->setSource(src);
    window->run<BeautyDemoPlan>(0, viewport);

    return 0;
}
