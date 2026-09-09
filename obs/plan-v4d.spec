Name:           plan-v4d
Version:        4.13.0~beta~kallaballa
Release:        1%{?dist}
Summary:        OpenCV with Plan-DSL and V4D Visualization Modules
License:        Apache-2.0
Group:          Development/Libraries/C and C++
URL:            https://github.com/kallaballa/Plan-V4D
Source0:        opencv-%{version}.tar.gz
Source1:        plan-v4d-%{version}.tar.gz

Epoch:          1

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++
BuildRequires:  git-core
BuildRequires:  make
BuildRequires:  pkgconfig
BuildRequires:  opencl-headers
BuildRequires:  zlib-devel
BuildRequires:  libpng-devel
BuildRequires:  libjpeg-devel
BuildRequires:  pkgconfig(libva)
BuildRequires:  libXinerama-devel
BuildRequires:  libXcursor-devel
BuildRequires:  libXi-devel

%if 0%{?suse_version}
BuildRequires:  Mesa-libGL-devel
BuildRequires:  glu-devel
BuildRequires:  glew-devel
BuildRequires:  libglfw-devel
BuildRequires:  libqt5-qtbase-devel
BuildRequires:  ocl-icd-devel
BuildRequires:  fdupes
%endif

%if 0%{?fedora}
BuildRequires:  mesa-libGL-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  glew-devel
BuildRequires:  glfw-devel
BuildRequires:  qt5-qtbase-devel
BuildRequires:  ocl-icd-devel
%endif

# ====================================================================
# Subpackage declarations
# ====================================================================

%package -n plan-v4d-data
Summary:        Data files for OpenCV+Plan-V4D
Group:          System/Data
BuildArch:      noarch

%description -n plan-v4d-data
Pre-trained models (LBF, YuNet face detection), cascade classifiers, fonts
and other data files needed at runtime by the V4D and Plan modules.

# --------------------------------------------------------------------

%package -n plan-v4d-libs
Summary:        OpenCV+Plan-V4D shared libraries
Group:          System/Libraries
Requires:       plan-v4d-data = %{epoch}:%{version}-%{release}
Requires:       plan-v4d-docs = %{epoch}:%{version}-%{release}
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%if 0%{?suse_version}
Recommends:     intel-opencl-icd
Recommends:     Mesa-libOpenGL0
Recommends:     ocl-icd
Recommends:     libva2
Recommends:     libva-drm2
Recommends:     libva-x11-2
Recommends:     libQt5OpenGL5
Recommends:     libavcodec60
Recommends:     libavformat60
Recommends:     libswscale7
%endif

%if 0%{?fedora}
Recommends:     intel-compute-runtime
Recommends:     mesa-libGL
Recommends:     ocl-icd
Recommends:     libva
Recommends:     libva-drm
Recommends:     libva-x11
Recommends:     qt5-qtbase
Recommends:     ffmpeg
%endif

%description -n plan-v4d-libs
Shared libraries for OpenCV built with the Plan-DSL and V4D visualization
modules. Includes support for FFmpeg, Qt5, OpenGL, OpenCL, and VA-API.

# --------------------------------------------------------------------

%package -n plan-v4d-docs
Summary:        Documentation for OpenCV+Plan-V4D
Group:          Documentation
BuildArch:      noarch

%description -n plan-v4d-docs
Plan-DSL programming guide, V4D application programming guide,
sample walkthroughs, and module READMEs for OpenCV with Plan-DSL
and V4D visualization modules.

# --------------------------------------------------------------------

%package -n plan-v4d-devel
Summary:        Development files for OpenCV+Plan-V4D
Group:          Development/Libraries/C and C++
Requires:       plan-v4d-libs = %{epoch}:%{version}-%{release}

%if 0%{?suse_version}
Recommends:     Mesa-libGL-devel
Recommends:     glu-devel
Recommends:     glew-devel
Recommends:     libglfw-devel
Recommends:     libqt5-qtbase-devel
Recommends:     ffmpeg-devel
%endif

%if 0%{?fedora}
Recommends:     glfw-devel
Recommends:     mesa-libGL-devel
Recommends:     mesa-libGLU-devel
Recommends:     glew-devel
Recommends:     qt5-qtbase-devel
Recommends:     ffmpeg-free-devel
%endif

Provides:       opencv-devel = %{epoch}:%{version}-%{release}
Provides:       opencv4-devel = %{epoch}:%{version}-%{release}

%description -n plan-v4d-devel
Header files, pkgconfig files and development libraries for building
applications against OpenCV with Plan-DSL and V4D modules.

# --------------------------------------------------------------------

%package -n plan-v4d-samples
Summary:        Sample binaries for OpenCV+Plan-V4D
Group:          Development/Examples
Requires:       plan-v4d-libs = %{epoch}:%{version}-%{release}
Requires:       plan-v4d-data = %{epoch}:%{version}-%{release}

%description -n plan-v4d-samples
Demonstration programs for the V4D visualization module including
framebuffer, vector graphics, font rendering, video editing, face
detection, optical flow, and shader demos.

# ====================================================================
# Main description
# ====================================================================

%description
Custom build of OpenCV 4.13.0 with Plan-DSL (compile-time task-graph language)
and V4D (GPU visualization runtime) modules from the kallaballa fork.
Built with FFmpeg, Qt5, OpenGL, OpenCL, and VA-API support.

# ====================================================================
# Prep
# ====================================================================
%prep
mkdir -p opencv-%{version}
tar -xzf %{SOURCE0} -C opencv-%{version} --strip-components=1
mkdir -p plan-v4d-%{version}
tar -xzf %{SOURCE1} -C plan-v4d-%{version} --strip-components=1

mkdir -p extra_modules
cp -a plan-v4d-%{version}/modules/* extra_modules/ 2>/dev/null || true
rm -rf plan-v4d-%{version}

# ====================================================================
# Build
# ====================================================================
%build
export CL_TARGET_OPENCL_VERSION=120
cd opencv-%{version}

cmake -B build \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DOPENCV_EXTRA_MODULES_PATH=%{_builddir}/extra_modules \
    -DOPENCV_V4D_ENABLE_ES3=OFF \
    -DOPENCV_ALGO_HINT_DEFAULT=ALGO_HINT_APPROX \
    -DOPENCV_GENERATE_PKGCONFIG=ON \
    -DOPENCV_CUSTOM_PACKAGE_INFO=ON \
    -DCMAKE_CXX_FLAGS="-DCL_TARGET_OPENCL_VERSION=120" \
    -DCMAKE_SKIP_RPATH=ON \
    \
    -DWITH_WAYLAND=ON \
    -DWITH_OPENGL=ON \
    -DWITH_OPENCL=ON \
    -DWITH_QT=ON \
    -DWITH_FFMPEG=OFF \
    -DWITH_VA=OFF \
    -DWITH_VA_INTEL=OFF \
    -DWITH_PTHREADS_PF=ON \
    -DWITH_QUIRC=ON \
    -DOPENCV_ENABLE_EGL=ON \
    -DOPENCV_ENABLE_GLX=ON \
    -DOPENCV_ENABLE_EGL_INTEROP=ON \
    -DOPENCV_ENABLE_GLX_INTEROP=ON \
    -DOPENCV_FFMPEG_ENABLE_LIBAVDEVICE=OFF \
    -DCV_TRACE=OFF \
    -DCV_ENABLE_INTRINSICS=ON \
    \
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
    -DWITH_OPENCL_SVM=OFF \
    -DWITH_OPENCLAMDFFT=OFF \
    -DWITH_OPENCLAMDBLAS=OFF \
    -DWITH_GPHOTO2=OFF \
    -DWITH_LAPACK=OFF \
    -DWITH_ITT=OFF \
    \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_ZLIB=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_PACKAGE=ON \
    \
    -DBUILD_opencv_calib3d=ON \
    -DBUILD_opencv_ccalib=ON \
    -DBUILD_opencv_dnn=ON \
    -DBUILD_opencv_features2d=ON \
    -DBUILD_opencv_flann=ON \
    -DBUILD_opencv_photo=ON \
    -DBUILD_opencv_imgcodecs=ON \
    -DBUILD_opencv_videoio=ON \
    -DBUILD_opencv_video=ON \
    -DBUILD_opencv_stitching=ON \
    -DBUILD_opencv_face=ON \
    -DBUILD_opencv_optflow=ON \
    -DBUILD_opencv_plot=ON \
    -DBUILD_opencv_tracking=ON \
    -DBUILD_opencv_ximgproc=ON \
    -DBUILD_opencv_plan=ON \
    -DBUILD_opencv_v4d=ON \
    \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_js=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_opencv_gapi=OFF \
    -DBUILD_opencv_ml=OFF \
    -DBUILD_opencv_highgui=OFF \
    -DBUILD_opencv_shape=OFF \
    -DBUILD_opencv_videostab=OFF \
    -DBUILD_opencv_superres=OFF \
    -DBUILD_opencv_world=OFF \
    -DBUILD_opencv_alphamat=OFF \
    -DBUILD_opencv_aruco=OFF \
    -DBUILD_opencv_barcode=OFF \
    -DBUILD_opencv_bgsegm=OFF \
    -DBUILD_opencv_bioinspired=OFF \
    -DBUILD_opencv_cnn_3dobj=OFF \
    -DBUILD_opencv_cvv=OFF \
    -DBUILD_opencv_datasets=OFF \
    -DBUILD_opencv_dnn_objdetect=OFF \
    -DBUILD_opencv_dnn_superres=OFF \
    -DBUILD_opencv_dpm=OFF \
    -DBUILD_opencv_freetype=OFF \
    -DBUILD_opencv_fuzzy=OFF \
    -DBUILD_opencv_hdf=OFF \
    -DBUILD_opencv_hfs=OFF \
    -DBUILD_opencv_img_hash=OFF \
    -DBUILD_opencv_intensity_transform=OFF \
    -DBUILD_opencv_line_descriptor=OFF \
    -DBUILD_opencv_mcc=OFF \
    -DBUILD_opencv_ovis=OFF \
    -DBUILD_opencv_phase_unwrapping=OFF \
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
    -DBUILD_opencv_xfeatures2d=OFF \
    -DBUILD_opencv_xobjdetect=OFF \
    -DBUILD_opencv_xphoto=OFF \
    -DBUILD_opencv_wechat_qrcode=OFF \
    -DGBFX_CONFIG_MULTITHREADED=OFF \
    -DBGFX_CONFIG_PASSIVE=ON

make -C build %{?_smp_mflags}

# ====================================================================
# Install
# ====================================================================
%install
cd opencv-%{version}
make -C build DESTDIR=%{buildroot} install

# nanovg's own CMake also installs a duplicate library into /usr/lib (outside
# %{_libdir}). Drop the stray copy on multilib systems so it is not flagged as
# an unpackaged file; the %{_libdir} copy is the one that gets packaged.
if [ "%{_libdir}" != "/usr/lib" ]; then
    rm -f %{buildroot}/usr/lib/libnanovg.so*
fi

# OpenCV does not install the C++ example binaries by default; copy them so
# the plan-v4d-samples subpackage gets its files.
mkdir -p %{buildroot}%{_bindir}
cp -a build/bin/example_v4d_* %{buildroot}%{_bindir}/ 2>/dev/null || true

# OpenCV's generated pkgconfig template combines ${exec_prefix} with an install
# path that already contains the prefix, producing a double slash that breaks
# debugedit during stripping. Collapse any occurrences.
sed -i 's#//usr#/usr#g' %{buildroot}%{_libdir}/pkgconfig/opencv*.pc

install -d %{buildroot}%{_docdir}/%{name}
install -m 0644 LICENSE        %{buildroot}%{_docdir}/%{name}/LICENSE
install -m 0644 CONTRIBUTING.md %{buildroot}%{_docdir}/%{name}/CONTRIBUTING.md

# ---- Plan-DSL documentation ----
install -d %{buildroot}%{_docdir}/%{name}/plan
install -m 0644 %{_builddir}/extra_modules/plan/README.md \
    %{buildroot}%{_docdir}/%{name}/plan/README.md
install -m 0644 %{_builddir}/extra_modules/plan/doc/plan-dsl-programming-guide.markdown \
    %{buildroot}%{_docdir}/%{name}/plan/plan-dsl-programming-guide.markdown
install -m 0644 %{_builddir}/extra_modules/plan/doc/plan-dsl-reference.markdown \
    %{buildroot}%{_docdir}/%{name}/plan/plan-dsl-reference.markdown

# ---- V4D documentation ----
install -d %{buildroot}%{_docdir}/%{name}/v4d
install -m 0644 %{_builddir}/extra_modules/v4d/README.md \
    %{buildroot}%{_docdir}/%{name}/v4d/README.md
install -m 0644 %{_builddir}/extra_modules/v4d/doc/v4d-application-programming-guide.markdown \
    %{buildroot}%{_docdir}/%{name}/v4d/v4d-application-programming-guide.markdown
install -d %{buildroot}%{_docdir}/%{name}/v4d/samples
for f in %{_builddir}/extra_modules/v4d/doc/samples/*.markdown; do
    install -m 0644 "$f" %{buildroot}%{_docdir}/%{name}/v4d/samples/
done

# ---- Plan test sources (for downstream rebuilders / test suites) ----
install -d %{buildroot}%{_datadir}/%{name}/plan/test
install -m 0644 %{_builddir}/extra_modules/plan/test/*.cpp \
    %{_builddir}/extra_modules/plan/test/*.hpp \
    %{buildroot}%{_datadir}/%{name}/plan/test/

# ---- V4D sample sources ----
install -d %{buildroot}%{_datadir}/%{name}/v4d/samples
install -m 0644 %{_builddir}/extra_modules/v4d/samples/*.cpp \
    %{_builddir}/extra_modules/v4d/samples/*.hpp \
    %{buildroot}%{_datadir}/%{name}/v4d/samples/
install -d %{buildroot}%{_datadir}/%{name}/v4d/samples/fonts
install -m 0644 %{_builddir}/extra_modules/v4d/samples/fonts/*.ttf \
    %{buildroot}%{_datadir}/%{name}/v4d/samples/fonts/

%if 0%{?suse_version}
%fdupes %{buildroot}%{_prefix}
%endif

%post -n plan-v4d-libs -p /sbin/ldconfig
%postun -n plan-v4d-libs -p /sbin/ldconfig

# ====================================================================
# Files
# ====================================================================

%files -n plan-v4d-data
%dir %{_datadir}/opencv4
%{_datadir}/opencv4/haarcascades/
%{_datadir}/opencv4/lbpcascades/
%{_datadir}/opencv4/models/
%{_datadir}/opencv4/fonts/
%{_datadir}/opencv4/valgrind.supp
%{_datadir}/opencv4/valgrind_3rdparty.supp
%{_licensedir}/opencv4/

%files -n plan-v4d-docs
%dir %{_docdir}/%{name}
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/CONTRIBUTING.md
%doc %{_docdir}/%{name}/plan/
%doc %{_docdir}/%{name}/v4d/

%files -n plan-v4d-libs
%dir %{_docdir}/%{name}
%{_libdir}/libopencv*.so.*
%{_libdir}/libnanovg.so*

%files -n plan-v4d-devel
%{_includedir}/opencv4/
%{_includedir}/nanovg/
%{_bindir}/setup_vars_opencv4.sh
%{_libdir}/libopencv*.so
%{_libdir}/pkgconfig/opencv*.pc
%{_libdir}/cmake/opencv4/
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/plan/test/

%files -n plan-v4d-samples
%{_bindir}/example_v4d_*
%{_datadir}/%{name}/v4d/samples/

%changelog
* Tue Sep 01 2026 elchaschab <elchaschab@users.noreply.github.com> - 4.13.0~beta~kallaballa-1
- Initial release for openSUSE Tumbleweed / Fedora
- Custom build of OpenCV 4.13.0 with Plan-DSL and V4D visualization modules
- Built with Qt5, OpenGL, and OpenCL support
