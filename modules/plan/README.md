# Plan-DSL — OpenCV module

A small, embed-in-C++ language for building per-frame computation
graphs. The whole DSL fits in a single header (`opencv2/plan/plan.hpp`)
plus a few small support headers; the runtime is a single translation
unit (`src/plan.cpp`).

## What it is

`Plan` is the base class. A plan describes one iteration of a loop
by **recording** it: every call you write inside `setup()`,
`infer()`, `gui()` and `teardown()` emits a *task node* into a list
that the runtime then replays every frame. Side effects are deferred;
errors surface at graph-build time.

A plan has four lifecycle methods (see `plan.hpp:537-546`):

| Method       | When it runs                                     |
|--------------|--------------------------------------------------|
| `setup()`    | once per worker thread, before the frame loop    |
| `infer()`    | once per worker thread — records the per-frame graph |
| `teardown()` | once per worker thread, after the frame loop     |
| `gui()`      | once, on the main thread, before the frame loop  |

`Plan::run<Tplan>(workers, args...)` boots the lifecycle. The graph
is built during `infer()` and replayed by `runGraph()` every frame.

## Building blocks

* **Edges.** The only value type. Edge-calls `V(x)`, `R(x)`, `RW(x)`,
  `RS(x)`, `RWS(x)`, `CS(x)`, `P<T>(key)`, `E<T>()` describe how a
  node should access storage or a runtime value.
* **Operators.** C++ operators that record nodes (`ADD`, `MUL`,
  `IF`, `IDX`, `DEREF`, …). Use symbol form (`a + b`), named form
  (`ADD(a, b)`) or generic form (`OP<Operators::ADD_>(a, b)`).
  Statement form (`assign(RW(x), R(y))`) for void-result nodes.
* **Functions.** `F(callable, args...)` wraps any C++ callable as a
  node. Non-void results become edges.
* **Control flow.** `branch(pred) / elseBranch() / endBranch()`
  regions. Predicates may be bool edges, predefined predicates
  (`always_`, `isTrue_`, `and_`, `or_`, …), or callable returning
  bool. `BranchType::PARALLEL` (default), `SINGLE`, `ONCE`,
  `PARALLEL_ONCE` select execution semantics.
* **Sub-plans.** Construct with `_sub<T>(parent, args...)`; splice
  into the parent with `subInfer(sub)` / `subSetup(sub)` /
  `subTeardown(sub)`.
* **Shared state.** Per-worker by default. `_shared(member)` gives a
  member a mutex; access via `RS` / `RWS` / `CS`.
* **Properties.** `Property<T>` is a typed edge bound to
  `GlobalState` / `LocalState`.
* **Events.** `Event<T>` is an edge producing a vector of input
  events (mouse, keyboard, window, joystick). The DSL core returns
  empty lists; runtimes like V4D plug in a real fetcher.

## Files

```
modules/plan/
├── CMakeLists.txt
├── README.md                  ← this file
├── doc/
│   ├── plan-dsl-programming-guide.markdown
│   └── plan-dsl-reference.markdown
├── include/
│   └── opencv2/
│       └── plan/
│           ├── flags.hpp              AllocateFlags / ConfigFlags / DebugFlags
│           ├── plan.hpp               Plan, PlanRuntime, Edge, Property, Event, …
│           ├── threadsafeanymap.hpp   backing store for GlobalState / LocalState
│           ├── util.hpp               misc helpers (_OLM_, lambda_ptr_hex, …)
│           └── detail/
│               ├── context.hpp        PlanContext (CPU/GPU dispatch)
│               └── transaction.hpp    Transaction, Node, BranchState
├── samples/                            (empty — see modules/v4d/samples/)
└── src/
    ├── plan.cpp                        PlanRuntime lifecycle, runGraph()
    └── detail/                         per-context implementations
```

## Where to start

1. Read [`doc/plan-dsl-programming-guide.markdown`](doc/plan-dsl-programming-guide.markdown).
   It's a friendly tour through the language, walking through
   `video_editing.cpp` and `beauty-demo.cpp` from the V4D module.
2. Keep [`doc/plan-dsl-reference.markdown`](doc/plan-dsl-reference.markdown)
   open while you write. It's the canonical opcode-by-opcode
   reference.
3. If you're building a windowed, GPU-capable, capture-and-write
   program, jump to the V4D module — it inherits from `Plan` and
   adds the runtime, the window, the source/sink, and the
   rendering contexts.
4. If you're embedding the DSL in a different runtime, read
   `PlanRuntime` (`plan.hpp:84-156`). Override `plainCtx()`,
   `initWorkerThread()`, `runFrameLoop()` and you have a Plan
   driver.

## Requirements

* C++20
* OpenCV core, OpenCV imgproc
* A threading library with `<barrier>` and `<semaphore>` (libstdc++
  11+, libc++ 11+, MSVC 19.28+).

## Building

```bash
./build_plan.sh
```

## License

Apache 2.0, like the rest of OpenCV. See the top-level
[`LICENSE`](../../LICENSE) of this repository.
