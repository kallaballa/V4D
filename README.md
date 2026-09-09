# Plan-DSL & V4D

Unofficial [OpenCV](https://opencv.org/) contrib modules for building per-frame
computation graphs that drive video, image, GPU, and GUI applications in parallel.

* **[plan](modules/plan/README.md)** — a **type-safe, dataflow-oriented C++ eDSL**
  (embedded domain-specific language). You describe one iteration of a frame
  loop; the compiler checks the graph at build time and the runtime replays it
  every frame. There is dynamic branching.
* **[v4d](modules/v4d/README.md)** — a **graphics runtime for Plan**: a
  GLFW/OpenGL window and event loop, NanoVG and ImGui rendering contexts, and
  video Sources/Sinks on top of the DSL.

Think *"GStreamer, but compile-time"*: the pipeline is written in C++, checked
by the compiler, and fixed at build time instead of being assembled and
negotiated at runtime.

![beauty demo](img/beauty.png) ![display demo](img/display.png)

## Three things to know

* **No frame loop.** Describe one iteration of the loop; the runtime records it
  once and replays it every frame. "Record once, replay forever."
* **Window, GPU and GUI included.** GLFW + OpenGL, NanoVG 2D vector graphics,
  ImGui immediate-mode UI, and bgfx — no boilerplate, no `while (true)`.
* **A normal OpenCV extra module.** Drop it into `OPENCV_EXTRA_MODULES_PATH`,
  build with C++20, and use it like any other contrib module.

## How it compares

Plan-DSL sits alongside a well-known family of graph-based media/vision
frameworks. The difference is *when* the graph is decided.

| Framework | Graph topology | Checking | Notes |
|---|---|---|---|
| GStreamer | built at runtime, plugins linked by name | runtime caps negotiation | playback/streaming graphs, `gst-launch` pipelines |
| NVIDIA DeepStream | built on GStreamer | runtime | inference-oriented plugin pipelines |
| OpenCV G-API | DAG built as C++ macros | runtime for untyped, typed wrapper partially checked | same project family; pipelines as expressions, backends |
| Halide | DSL-specific pipeline | compile-time | stencil/image-processing algorithm+schedule eDSL |
| DSPatch | runtime-wired objects | runtime, untyped | generic C++ dataflow/patching framework |
| TBB flow graph | runtime-wired nodes | runtime | dataflow graphs from function nodes |
| **Plan-DSL** | **recorded once, in C++** | **compile-time, type-safe** | task graphs with control flow, threading, GPU contexts |

Plan-DSL goes beyond a pure DAG:

* **Control flow is first-class.** `branch(pred)` / `elseBranch()` /
  `endBranch()` regions with parallel, single-time, and once-only semantics —
  not just a static DAG.
* **Shared memory is explicit and safe.** Edges declare access intent
  (`R`/`RW`/`RS`/`RWS`/`CS`), and `_shared(member)` layers a mutex on top.
* **The compiler is the checker.** Wrong operand types, dangling references,
  and invalid side-effect contexts fail to compile or fail at graph-build time —
  not ten minutes into a long encode.
* **Workers are per-thread graph copies.** Each worker records and replays its
  own independent copy of the graph, with deterministic in-order execution.

## Hello, graph

```cpp
#include <opencv2/v4d/v4d.hpp>
using namespace cv;
using namespace cv::v4d;

class FontRenderingPlan : public V4DPlan {
    string text_ = "Hello World";
    Property<cv::Size> size_ = P<cv::Size>(V4D::Keys::SIZE);
public:
    void infer() override {
        nvg([](const Size& sz, const string& str) {
            using namespace cv::v4d::nvg;
            clearScreen();
            fontSize(40.0f);
            fillColor(Scalar(255, 0, 0, 255));
            textAlign(NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            text(sz.width / 2.0, sz.height / 2.0, str.c_str(), str.c_str() + str.size());
        }, size_, R(text_));
    }
};

int main() {
    cv::Ptr<V4D> runtime = V4D::init(cv::Rect(0, 0, 960, 960), "Font Rendering",
                                     AllocateFlags::NANOVG);
    V4DPlan::run<FontRenderingPlan>(0);
}
```

No event loop, no GL calls, no cleanup code. `infer()` *records* the per-frame
graph; `V4DPlan::run<...>` boots the workers, drives the frame loop, and joins
when the window closes.

## The two modules

| Module | Description | Docs |
|---|---|---|
| `plan` | The type-safe dataflow eDSL: edges, operators, control flow, sub-plans, shared state. | [plan README](modules/plan/README.md) |
| `v4d` | The graphics runtime: window + GPU contexts, NanoVG/ImGui layers, Sources & Sinks. | [v4d README](modules/v4d/README.md) |

---

## Plan-DSL — the language

Four lifecycle methods on a class derived from `Plan`:

| Method       | When it runs                                        |
|--------------|-----------------------------------------------------|
| `setup()`    | once per worker thread, before the frame loop       |
| `infer()`    | once per worker thread — records the per-frame graph |
| `teardown()` | once per worker thread, after the frame loop        |
| `gui()`      | once, on the main thread, before the frame loop     |

Building blocks:

* **Edges** — the only value type. `V(x)`, `R(x)`, `RW(x)`, `RS(x)`, `RWS(x)`,
  `CS(x)`, `P<T>(key)`, `E<T>()` describe how a node accesses storage or runtime
  state.
* **Operators** — C++ operators that record nodes: `ADD`, `MUL`, `IF`, `IDX`,
  `DEREF`, … in symbol, named, or generic form.
* **Functions** — `F(callable, args...)` wraps any C++ callable as a node.
* **Control flow** — `branch(pred)` / `elseBranch()` / `endBranch()` regions
  with parallel, single, and once-only semantics.
* **Sub-plans** — compose large programs with `_sub<T>(...)` and `subInfer()`.
* **Shared state, properties, events** — mutex-protected members, typed runtime
  property edges, and input event streams.

Because execution is deferred, type errors, dangling references, and invalid
operand combinations surface at graph-build time — not hours into a recording
process.

## V4D — the runtime

A `V4DPlan` subclass gets a window, an event loop, and five side-effect contexts
on top of the DSL's `plain(...)`:

| Call               | Context        | Purpose                               |
|--------------------|----------------|---------------------------------------|
| `gl(fn, args...)`  | OpenGL         | Raw GL commands                       |
| `fb<pos>(fn, args...)` | Framebuffer | Direct framebuffer access             |
| `nvg(fn, args...)` | NanoVG         | Vector graphics on top of GL          |
| `bgfx(fn, args...)`| bgfx           | bgfx rendering (alternative to GL)    |
| `ext(fn, args...)` | External       | External renderer contexts            |
| `capture()` / `write()` | Source / Sink | Pull the next input frame / push the finished frame |
| `imgui(fn, args...)` | ImGui         | UI nodes from `gui()`                 |

Sources and sinks read from video files, webcams, or arbitrary functors, and
write to files or anything else:

```cpp
auto src  = Source::make(rt, "in.mp4");
auto sink = Sink::make(rt, "out.mkv", src->fps(), viewport.size());
rt->setSource(src);
rt->setSink(sink);
```

## Samples

More than two dozen small programs in [modules/v4d/samples/](modules/v4d/samples/):

| Start here | What it shows |
|---|---|
| `video_editing.cpp` | capture → nvg → write, the canonical pipeline |
| `beauty-demo.cpp` | the kitchen sink: shared state, sub-plans, `IF`, events, NanoVG, ImGui |
| `font_rendering.cpp` | the smallest visible program (32 lines) |
| `imshow_reimplementation.cpp` | a full GUI image viewer |

Plus: raw OpenGL (`render_opengl`, `cube-demo`, `shader-demo`), vector graphics
(`nanovg-demo`, `font-demo`), video processing (`optflow-demo`,
`pedestrian-demo`), multi-window (`montage-demo`, `many_cubes-demo`), custom
I/O (`custom_source_and_sink`), and more.

## Requirements

* C++20 (`<barrier>` and `<semaphore>`)
* OpenCV 4.x (core + imgproc; V4D samples additionally use videoio, video,
  imgcodecs, dnn, face, objdetect, tracking, optflow, plot, features2d, flann)
* GLFW 3 (V4D only)
* An OpenGL-capable driver (or OpenGL ES 3.0)

## Building

Both modules build as standard OpenCV extra modules:

```bash
mkdir build && cd build
cmake -DOPENCV_EXTRA_MODULES_PATH=../modules \
      -DBUILD_opencv_plan=ON \
      -DBUILD_opencv_v4d=ON \
      -DBUILD_EXAMPLES=ON \
      ../..
cmake --build . --target example_v4d_video_editing
./bin/example_v4d_video_editing in.mp4 out.mkv
```

| CMake option                    | Effect                                       |
|---------------------------------|----------------------------------------------|
| `OPENCV_V4D_ENABLE_ES3`         | Build against OpenGL ES 3.0 instead of desktop GL. |
| `OPENCV_V4D_ENABLE_BGFX`        | Build the bgfx context and link bgfx.        |
| `OPENCV_V4D_ENABLE_MALI`        | Mali GPU support (requires libmali).         |
| `BUILD_EXAMPLES`                | Build the programs in `modules/v4d/samples/`. |

Run the Plan-DSL test suite with:

```bash
cmake -DOPENCV_BUILD_TEST_MODULES_LIST=plan ...
cmake --build . --target opencv_test_plan
./bin/opencv_test_plan
```

### macOS

* Requires macOS 13+, Xcode 14+ (Apple Clang 14+ / libc++ 14+), and GLFW via
  Homebrew: `brew install glfw`.
* Leave `OPENCV_V4D_ENABLE_ES3=OFF` — the ES3 path uses EGL, which is not
  available on macOS. V4D automatically uses a desktop GL 3.2 core profile with
  forward compatibility and loads system GL function pointers.
* Verified continuously in CI by `macOS-ARM64-v4d` and `macOS-X64-v4d`
  GitHub Actions jobs.

### Third-party code

V4D vendors NanoVG, ImGui, GLAD and friends under
[modules/v4d/third/](modules/v4d/third/); may require
`git submodule update --init --recursive`. Assets such as the YuNet face
detector and the LBF landmark model ship in
[modules/v4d/assets/](modules/v4d/assets/).

## Documentation

* [Plan-DSL Programming Guide](modules/plan/doc/plan-dsl-programming-guide.markdown) —
  a friendly tour through the language.
* [Plan-DSL Reference](modules/plan/doc/plan-dsl-reference.markdown) —
  the canonical edge-by-edge, operator-by-operator reference.
* [V4D Application Programming Tutorial](modules/v4d/doc/v4d-application-programming-guide.markdown) —
  the V4D tutorial, milestone by milestone.
* [Sample walkthroughs](modules/v4d/doc/samples/) — annotated `00-intro` through `18-many-cubes`.

## Packaging

The project ships Debian packaging (`plan-v4d.dsc` + `debian/`), an OBS recipe
(`obs/plan-v4d.spec`), and a Flatpak manifest
(`flatpak/io.github.kallaballa.PlanV4D.yml`).

## License

Apache 2.0, like the rest of OpenCV — see [LICENSE](LICENSE). Vendored
third-party code under `modules/v4d/third/` is licensed under its own terms.
