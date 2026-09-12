#!/bin/bash

BUILD_DIR=/home/elchaschab/devel/opencv/build

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

cmake --fresh \
  -DOPENCV_BUILD_TEST_MODULES_LIST=plan \
  -DOPENCV_BUILD_PERF_TEST_MODULES_LIST=plan \
  -DWITH_WAYLAND=ON \
  -DOPENCV_V4D_ENABLE_ES3=OFF \
  -DOPENCV_V4D_ENABLE_BGFX=OFF \
  -DOPENCV_ALGO_HINT_DEFAULT=ALGO_HINT_APPROX \
  -DCMAKE_CXX_FLAGS="-DCL_TARGET_OPENCL_VERSION=120" \
  -DCMAKE_MODULE_LINKER_FLAGS="/usr/local/lib64/" \
  -DINSTALL_BIN_EXAMPLES=OFF \
  -DOPENCV_CUSTOM_PACKAGE_INFO=ON \
  -DCPACK_PACKAGE_VERSION_MAJOR=4 \
  -DCPACK_PACKAGE_VERSION_MINOR=13 \
  -DCPACK_PACKAGE_VERSION_PATCH=0 \
  -DCPACK_PACKAGE_VERSION=4:13.0-beta-kallaballa \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCPACK_PACKAGE_CONTACT="you@example.com" \
  -DOPENCV_GENERATE_PKGCONFIG=ON \
  -DCPACK_PACKAGE_VENDOR=yourname \
  -DCPACK_DEBIAN_PACKAGE_DEPENDS="libqt5opengl5,freeglut3,ocl-icd-libopencl1,libavcodec58,libavdevice58,libavfilter7,libavformat58,libavutil56,libpostproc55,libswresample3,libswscale5,libglfw3,libstb0,libglew2.2,zlib1g,libxinerama1,libxcursor1,libxi6,libva2,intel-opencl-icd,ca-certificates" \
  -DINSTALL_CREATE_DISTRIB=ON \
  -DCPACK_BINARY_DEB=ON \
  -DCV_TRACE=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DWITH_OPENGL=ON \
  -DOPENCV_ENABLE_EGL=ON \
  -DOPENCV_ENABLE_EGL_INTEROP=ON \
  -DOPENCV_ENABLE_GLX=ON \
  -DOPENCV_ENABLE_GLX_INTEROP=ON \
  -DOPENCV_FFMPEG_ENABLE_LIBAVDEVICE=ON \
  -DWITH_QT=ON \
  -DWITH_FFMPEG=ON \
  -DOPENCV_FFMPEG_SKIP_BUILD_CHECK=ON \
  -DWITH_VA=ON \
  -DWITH_VA_INTEL=ON \
  -DWITH_1394=OFF \
  -DWITH_ADE=OFF \
  -DWITH_VTK=OFF \
  -DWITH_EIGEN=OFF \
  -DWITH_GTK=OFF \
  -DWITH_GTK_2_X=OFF \
  -DWITH_IPP=OFF \
  -DWITH_JASPER=OFF \
  -DWITH_WEBP=OFF \
  -DWITH_OPENEXR=OFF \
  -DWITH_OPENVX=OFF \
  -DWITH_OPENNI=OFF \
  -DWITH_OPENNI2=OFF \
  -DWITH_TBB=OFF \
  -DWITH_TIFF=OFF \
  -DWITH_OPENCL=ON \
  -DWITH_OPENCL_SVM=OFF \
  -DWITH_OPENCLAMDFFT=OFF \
  -DWITH_OPENCLAMDBLAS=OFF \
  -DWITH_GPHOTO2=OFF \
  -DWITH_LAPACK=OFF \
  -DWITH_ITT=OFF \
  -DWITH_QUIRC=ON \
  -DBUILD_ZLIB=OFF \
  -DBUILD_opencv_apps=OFF \
  -DBUILD_opencv_calib3d=ON \
  -DBUILD_opencv_ccalib=ON \
  -DBUILD_opencv_dnn=ON \
  -DBUILD_opencv_features2d=ON \
  -DBUILD_opencv_flann=ON \
  -DBUILD_opencv_gapi=OFF \
  -DBUILD_opencv_ml=OFF \
  -DBUILD_opencv_photo=ON -DBUILD_opencv_shape=OFF \
  -DBUILD_opencv_imgcodecs=ON -DBUILD_opencv_videostab=OFF \
  \
  -DBUILD_opencv_videoio=ON -DBUILD_opencv_superres=OFF \
  \
  -DBUILD_opencv_highgui=ON \
  \
  -DBUILD_opencv_stitching=ON \
  -DBUILD_opencv_java=OFF \
  -DBUILD_opencv_js=OFF \
  -DBUILD_opencv_python2=OFF \
  -DBUILD_opencv_python3=OFF \
  -DBUILD_opencv_alphamat=OFF \
  -DBUILD_opencv_aruco=OFF \
  -DBUILD_opencv_barcode=OFF \
  -DBUILD_opencv_bgsegm=OFF \
  -DBUILD_opencv_bioinspired=OFF \
  -DBUILD_opencv_cnn_3dobj=OFF \
  -DBUILD_opencv_cudaarithm=OFF \
  -DBUILD_opencv_cudabgsegm=OFF \
  -DBUILD_opencv_cudacodec=OFF \
  -DBUILD_opencv_cudafeatures2d=OFF \
  -DBUILD_opencv_cudafilters=OFF \
  -DBUILD_opencv_cudaimgproc=OFF \
  -DBUILD_opencv_cudalegacy=OFF \
  -DBUILD_opencv_cudaobjdetect=OFF \
  -DBUILD_opencv_cudaoptflow=OFF \
  -DBUILD_opencv_cudastereo=OFF \
  -DBUILD_opencv_cudawarping=OFF \
  -DBUILD_opencv_cudev=OFF \
  -DBUILD_opencv_cvv=OFF \
  -DBUILD_opencv_datasets=OFF \
  -DBUILD_opencv_dnn_objdetect=OFF \
  -DBUILD_opencv_dnns_easily_fooled=OFF \
  -DBUILD_opencv_dnn_superres=OFF \
  -DBUILD_opencv_dpm=OFF \
  -DBUILD_opencv_face=ON \
  -DBUILD_opencv_freetype=OFF \
  -DBUILD_opencv_fuzzy=OFF \
  -DBUILD_opencv_hdf=OFF \
  -DBUILD_opencv_hfs=OFF \
  -DBUILD_opencv_img_hash=OFF \
  -DBUILD_opencv_intensity_transform=OFF \
  -DBUILD_opencv_julia=OFF \
  -DBUILD_opencv_line_descriptor=OFF \
  -DBUILD_opencv_matlab=OFF \
  -DBUILD_opencv_mcc=OFF \
  -DBUILD_opencv_optflow=ON \
  -DBUILD_opencv_ovis=OFF \
  -DBUILD_opencv_phase_unwrapping=OFF \
  -DBUILD_opencv_plot=ON \
  -DBUILD_opencv_quality=OFF \
  -DBUILD_opencv_rapid=OFF \
  -DBUILD_opencv_reg=OFF \
  -DBUILD_opencv_rgbd=OFF \
  -DBUILD_opencv_saliency=OFF \
  -DBUILD_opencv_sfm=OFF \
  -DBUILD_opencv_stereo=OFF \
  -DBUILD_opencv_structured_light=OFF \
  -DBUILD_opencv_surface_matching=OFF \
  -DBUILD_opencv_text=OFF \
  -DBUILD_opencv_tracking=ON \
  -DBUILD_opencv_viz=OFF \
  -DBUILD_opencv_wechat_qrcode=OFF \
  -DBUILD_opencv_xfeatures2d=OFF \
  -DBUILD_opencv_ximgproc=ON \
  -DBUILD_opencv_xobjdetect=OFF \
  -DBUILD_opencv_xphoto=OFF \
  -DBUILD_opencv_world=OFF \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_PACKAGE=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_PERF_TESTS=ON \
  -DBUILD_DOCS=OFF \
  -DWITH_PTHREADS_PF=ON \
  -DCV_ENABLE_INTRINSICS=ON \
  -DBUILD_opencv_video=ON \
  -DBUILD_opencv_v4d=OFF \
  -DBUILD_opencv_plan=ON \
  -DBGFX_CONFIG_MULTITHREADED=ON \
  -DBGFX_CONFIG_PASSIVE=ON \
  -DOPENCV_EXTRA_MODULES_PATH="../../Plan-V4D/modules" \
  ..

make -j4 opencv_test_plan

if [ -x ./bin/opencv_test_plan ]; then
  ./bin/opencv_test_plan "$@"
else
  ./opencv_test_plan "$@"
fi

make -j4 opencv_perf_plan

if [ -x ./bin/opencv_perf_plan ]; then
  ./bin/opencv_perf_plan "$@"
else
  ./opencv_perf_plan "$@"
fi

