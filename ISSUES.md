# LavaEngine - Issues

> **Snapshot:** 2026-09-17  
> **Repository:** https://github.com/RanveerisdeGOAT/LavaEngine  
> **Version observed:** `0.8.0-indev`
>
> This file tracks issues, gaps, and engineering work identified from the current repository structure, README, changelog, and public project state.
>
> **Important:** Items marked as architectural/feature gaps are not necessarily bugs. They are things that should be clarified, implemented, tested, or documented as LavaEngine matures.

---

## Status Legend

- `[ ]` Open
- `[~]` In progress / partially implemented
- `[x]` Complete
- `[?]` Needs verification

## Priority Legend

- **P0 — Critical:** blocks reliable use of the framework
- **P1 — High:** important for the next usable milestone
- **P2 — Medium:** important for robustness/usability
- **P3 — Low:** polish, convenience, or long-term work
- **IDX** issue id.

---

## [x] ID1 - Define and enforce ownership/lifetime rules

**Priority:** P0  
**Area:** Core / Containers / Modules / Resources

The 0.7.1 changelog says that several lifetime issues were fixed, but the architecture relies heavily on Containers, Modules, Resources, shared references/handles, and cross-container access.

We need a documented and tested ownership model covering:

- Container destruction
- Module destruction
- Resource destruction
- Resource handles after owner destruction
- Imported/exported resources
- References held by scheduled jobs
- References held by Inspector code
- References held by events
- Cross-container resource access
- Shutdown ordering

### Acceptance criteria

- No use-after-free in core lifecycle paths.
- Resources have an unambiguous owner/lifetime policy.
- Jobs cannot outlive objects they capture unless explicitly supported.
- Destruction order is deterministic and documented.
- Regression tests cover lifetime edge cases.

### Resolution (0.8.0-indev)

- Generation-tagged `ResourceHandle`s; every `get`/`contains`/`remove` is
  checked, so stale handles can no longer alias a recreated resource.
- `ResourceView<T>`: weak, lifetime-checked borrows; `importResource` now
  returns a `ResourceView<T>` instead of a raw `T*` (see ID6).
- `Container`/`ModuleRegistry` moves re-point `Module::m_container` to the
  destination container (see ID4).
- Deterministic, documented `Application::unloadGame()` teardown phases with
  `Container::onUnload()`/`Module::onUnload()` hooks (see README,
  "Application Teardown").
- `expose()` snapshots values into Container-owned boxes (see ID6).
- Regression suite in `tests/`, passing under ASan with leak detection.

Residual lifetime surface remains tracked under ID3 (scheduler re-entrancy),
ID7 (logger races), and the Inspector hot-reload caveat in README.

---

## [x] ID2 - Define scheduler dependency and execution semantics

**Priority:** P0  
**Area:** Scheduler

The README establishes the scheduler as a fundamental part of LavaEngine, but the public design does not yet fully specify:

- Job ordering
- Dependencies between jobs
- Whether jobs may execute concurrently
- Thread affinity
- Cancellation
- Error propagation
- Shutdown behavior
- Re-entrancy
- Long-running jobs
- Resource lifetime while jobs are running

The current scheduler example also uses return values to determine whether a job repeats. This needs a precise contract.

### Resolution (0.8.1-indev)

The contract is now defined in `Scheduler`'s doxygen (`include/LavaEngine/
Scheduler.hpp`) and the README "Execution contract" section:

- Single-threaded, deterministic, creation-order execution on the calling
  thread; jobs never run concurrently (implying thread affinity: drive the
  scheduler from one thread).
- `>0` completes a job, `0` keeps it scheduled, `<0` cancels it; both terminal
  results count as finished for `run()`/`done()`, and `exit()` runs exactly
  once whenever a job finishes, cancels, or is destroyed.
- `dependsOn` rejects self-dependencies, unknown ids, and newly introduced
  cycles (DFS cycle detection).
- `destroyJob`/`exitAll`/`clear` define cancellation and shutdown; teardown is
  drained before containers/modules die (see ID1), so captured references stay
  valid while jobs run.

---

## [x] ID3 - Scheduler is not re-entrant (iterator invalidation / UB)

**Priority:** P0  
**Area:** Scheduler

Concrete hazards found while reviewing `src/Scheduler.cpp`:

- `execute()` (`src/Scheduler.cpp:135-180`) range-iterates `m_jobs` holding a live `Job&`. If `job.task()` calls `createJob()` the `std::vector` can reallocate, and `destroyJob()` erases while the reference is still in use — both invalidate the iterator and the reference → UB.
- `destroyJob()` (`src/Scheduler.cpp:90-119`) calls `it->exit()` (line 106) *before* the erase at line 118. If `exit` re-enters the scheduler (`createJob`/`destroyJob`), `it` is invalidated by the time it is dereferenced/erased.
- `exitAll()` (`src/Scheduler.cpp:122-132`) has the same problem: `exit()` callbacks that mutate `m_jobs` invalidate the range-for.
- `findJob()` (`src/Scheduler.cpp:45-54`) hands out a `Job*` that any subsequent `createJob`/`destroyJob` invalidates; callers cannot detect the staleness.

### Resolution (0.8.1-indev)

Mutations (createJob/destroyJob/exitAll/dependsOn) made from inside a `task` or
`exit` are now **deferred**: they are queued (or, for a copy, recorded through
the pending-clear flag) and flushed only after the current `execute()` pass.
`m_jobs` is never resized mid-pass, so index-based iteration with a pass-constant
size is safe:

- `execute()` iterates by index over a fixed pass size; re-entrant
  `createJob`/`destroyJob` queue into `m_pending_adds`/`m_pending_removes`
  instead of touching the vector mid-pass.
- `destroyJobNow()` moves the `Job` out of the vector *before* running `exit()`,
  so `exit()` re-entry cannot invalidate the iterator.
- `exitAll()` drains from the back (`drainAll`) so `exit()` re-entry is safe and
  jobs created by `exit()` are drained too.
- `exit()` is guaranteed to run exactly once (moved out of the live job on
  completion/cancel; destroying an already-finished job does not re-run it).
- Re-entrant `execute()` throws instead of recursing.
- `findJob`'s staleness is documented: the returned pointer is valid only until
  the next mutation.

Covered by `testSchedulerReentrantCreate`, `testSchedulerReentrantDestroy`,
`testSchedulerExitExactlyOnce`, and `testSchedulerExitAllDrainsCreatedDuringExit`
in `tests/tests.cpp`.

---

## [x] ID4 - Container/ModuleRegistry moves leave Modules pointing at the moved-from Container

**Priority:** P1  
**Area:** Containers / Modules

`Container::Container(Container&&)` (`src/Container.cpp:9-29`) moves `m_modules`/`m_resources` but never re-points each Module's `m_container` back at the *new* container. `ModuleRegistry`'s move ops (`src/Module.cpp:14-49`) copy `m_container` verbatim and call `module->setContainer(m_container)`, so every contained module keeps referencing the **moved-from, doomed** container.

After any move, `Module::getContainer()` (`include/LavaEngine/Module.hpp:18-26`) returns a stale container; the next use is use-after-free.

### Suggested fixes

- After moving, walk all modules and `setContainer(this)`.
- Add a regression test moving a container with `addModule<T>`'d modules and verifying `getContainer()`.
- Consider making `Container` non-movable (its name/ownership also make moves awkward) and storing it behind a `unique_ptr` in `Application`.

### Resolution (0.8.0-indev)

`Container`'s move ctor/assignment now call
`ModuleRegistry::setContainer(this)` after moving, which updates the
registry back-pointer and every module's `m_container`. Covered by
`testContainerMoveRepointsModules` in `tests/tests.cpp`.

---

## [x] ID5 - `typeID<T>()` uses `typeid(T).hash_code()`, which is collision- and DSO-unsafe

**Priority:** P1  
**Area:** Modules / Hot-reloading

`include/LavaEngine/Module.hpp:42-48` keys the `ModuleRegistry` on `typeID<T>()`:

```c++
constexpr TypeID typeID() { return typeid(T).hash_code(); }
```

Two problems:

1. `hash_code()` is not guaranteed unique even within a single binary. A collision makes `ModuleRegistry::add` throw "Module already exists" (`Module.hpp:74-77`) for a legitimately new type, or worse, `get<T>`/`remove<T>` (`Module.hpp:94-154`) `static_cast` the wrong type — UB.
2. `type_info` identity is not guaranteed stable across shared-library boundaries. With hot-reloading (`lavac/src/lavac.h:53`, `dlopen` of a game `.so`), a type registered from the game module can hash to a different value than the same type compiled into the engine — silent `get/has/remove` failures under hot reload.

### Suggested fixes

- Replace with a compile-time unique counter (`__COUNTER__`/counter template) or explicit registration macro, or
- A name-based ID, or
- `std::type_index` (itself based on `type_info`, so it does **not** solve the DSO problem — a manual scheme is required).

### Resolution (0.8.0-indev)

`typeID<T>()` is now a compile-time FNV-1a 64-bit hash of the type's
canonical spelling, extracted from `__PRETTY_FUNCTION__`/`__FUNCSIG__`
(`include/LavaEngine/Module.hpp`). The ID is a pure function of the type
name text, so it is:

- deterministic and stable across translation units and hot-reloaded
  shared objects (no dependence on `std::type_info` identity or a
  counter ordering that shifts between builds), and
- collision-resistant in practice (64-bit mix over the name).

A counter-based scheme was deliberately not chosen: `__COUNTER__`
values are assigned per-TU in preprocessing order, so two TUs using the
same type disagree, and adding a module can shift every later counter
across a reload — exactly the subset of problems this fix removes.
Covered by `testTypeIdStability` in `tests/tests.cpp`.

---

## [x] ID6 - `importResource`/`expose` hand out raw pointers with no lifetime guarantee

**Priority:** P0  
**Area:** Containers / Resources / Inspector

These are concrete instances of the lifetime hazards ID1 calls out:

- `Container::importResource` (`include/LavaEngine/Container.hpp:122-137`) returns a raw `T*` into the *exporter's* `ResourceRegistry`. No shared ownership, no refcount, no handle: if the exporter `remove()`s the resource or is destroyed, the importer dangles.
- `Container::expose` (`src/Container.cpp:36-54`) stores caller-supplied raw `void*` in `m_variables`. If the pointee (e.g. a Module member) is destroyed first (`removeModule`, container teardown, hot reload), the Inspector later reads/writes through a freed pointer. The Inspector's bool/int/float/double widgets (`lavac/inspector/src/Inspector.h:656-663`) write through these pointers every frame.

### Suggested fixes

- `importResource` should return a shared/lifetime-tracked handle (or a `shared_ptr`/observer that can be queried for validity) instead of a raw pointer.
- `expose` should either own a copy of the value (with write-back), take a `std::shared_ptr`/`weak_ptr`, or at minimum auto-deregister on teardown; document that exposed pointers must outlive the Container.

### Resolution (0.8.0-indev)

- `Container::importResource` returns `ResourceView<T>` (weak, revalidated on
  each access); the exporter's destruction or `remove()` invalidates it
  safely.
- `Container::expose` snapshots the value into a stable, Container-owned
  `ExposedBox`; the Inspector always writes through owned memory (string
  editing no longer casts a `std::string*` to `char*`). Added the missing
  `expose(const std::string&, double*)` overload.
- Covered by `testContainerImportResource` and `testExposedBoxes`.

---

## [x] ID7 - Logger accessors are not thread-safe

**Priority:** P1  
**Area:** Logger

`Logger::log` (`src/Logger.cpp:62-104`) reads `m_level`, `m_consoleEnabled`, `m_console` under `m_mutex`, but `setLevel`, `level`, `setConsoleEnabled`, `setFileEnabled`, and `setOutput` (`src/Logger.cpp:32-55`) read/write those same fields **without** the lock. Under concurrent logging + configuration this is a data race (UB). `setOutput` can also repoint `m_console` to a stream that is destroyed while `log()` is still flushing.

### Suggested fixes

- Take the mutex in every accessor (or make the hot fields `std::atomic`).
- Document that the redirected output stream must outlive the `Logger`.

### Resolution (unreleased)

- `setLevel`, `level`, `setConsoleEnabled`, `setFileEnabled`, and `setOutput`
  now take `m_mutex`, so every read/write of the shared configuration fields
  is synchronized with `log()`.
- `log()` now reads `m_level` while holding the lock instead of before taking
  it, closing the remaining race on the level check.
- The existing note that a stream passed to `setOutput()` must outlive the
  `Logger` still applies: locking protects the pointer, not the stream's
  lifetime.
- Verified with a TSan-instrumented logging/configuration stress test (no
  reports).

---

## [x] ID8 - `Window::~Window` calls `glfwTerminate()` unconditionally

**Priority:** P2  
**Area:** Window / GLFW lifecycle

`src/Window.cpp:61-67` terminates all of GLFW in the destructor. If more than one `Window` is alive (or a `Window` is moved from and the source is destroyed first), the first destruction tears the entire GLFW context out from under the surviving window(s) — crashes on subsequent `glfw` calls.

Note: `VulkanRenderer` owns a `Window` by value, and LavaVK examples also call `glfwTerminate()` themselves.

### Suggested fixes

- Reference-count `glfwInit`/`glfwTerminate` (e.g. a RAII `GlfwScope` shared by all windows), and only terminate when the last window is destroyed.

### Resolution (unreleased)

- `Window` is now explicitly **non-copyable and non-movable**; its move
  constructor/assignment declarations and definitions were removed. A
  moved-from `Window` can no longer outlive and terminate a live window.
- The library contract is now explicit: exactly one `Window` may exist per
  process, and `~Window` intentionally owns process-wide GLFW termination.
  Multiple windows are unsupported by design.
- Residual caveat: `Application::setFramework()` still constructs the new
  framework/window before destroying the old one, so a *live* framework must
  not be replaced without tearing the session down first (`unloadGame()`).
  That replacement path is outside the supported one-window lifecycle.

---

## [x] ID9 - `InputHandler`/`Window` moves lose state

**Priority:** P2  
**Area:** Window / InputHandler

- `InputHandler`'s move ctor/assignment now transfer `m_focused`.
- `Window` is non-movable, so the `m_lastFrameTime` move concern no longer applies.
- `InputHandler` has no dedicated automated test because constructing it requires a live GLFW window; covered by the manual `lavac` smoke test.

---

## [x] ID10 - `Application::run()`/`step()` busy-spin and can never terminate

**Priority:** P1  
**Area:** Application / Scheduler

`Application::run()` (`include/LavaEngine/Application.hpp:41-51`) loops `while (completed < jobs.size()) execute();`:

- Any job that re-runs forever (returns `0` — per `Scheduler::execute`, `src/Scheduler.cpp:172-178`, only `result > 0` marks a job complete; negatives are treated as "repeat too") spins at 100% CPU with no pacing and no error path.
- A task that calls `createJob` during `execute()` grows `m_jobs`, so the loop can fail to converge.
- An empty scheduler has `0 == 0`; `run()`/`step()` (`Application.hpp:55`) treat it as "done" instantly, which also defeats hot-reload inspection (zero registered jobs ⇒ immediate exit).

### Acceptance criteria

- `run()` should have a deterministic termination contract (e.g. a job explicitly cancels/returns "done").
- Long-running jobs should yield (frame structure) rather than busy-spin.
- Newly created jobs during execution should be handled explicitly (see ID3).

### Resolution (unreleased)

- `Application` now exposes `requestExit()`/`exitRequested()`; `run()` stops
  when the scheduler finishes or exit is requested. `step()` returns `1` on
  the pass that completes the last job or observes an exit request.
- `run()` is paced at `frameRateLimit()` (60 FPS by default;
  `setFrameRateLimit(0)` disables pacing). `UserGame::run()` and
  `Inspector::inspect()` use the same `waitForNextFrame()` helper.
- Jobs created during `execute()` are still deferred by `Scheduler`; `run()`
  re-checks `done()` after each pass, so new jobs keep the loop alive until
  they finish or `requestExit()` is called.
- Empty schedulers remain "done" for `Application::run()`/`step()`. The
  Inspector now polls GLFW and stops only on real job completion, exit request,
  or window close, so a game with no jobs can still be inspected.
- `unloadGame()` clears the exit request so a hot-reloaded session can run
  again.
- Covered by `testApplicationStepCompletesJob`,
  `testApplicationRunStopsOnRequestExit`,
  `testApplicationRunPicksUpJobsCreatedDuringExecute`,
  `testApplicationEmptySchedulerIsDone`, and
  `testApplicationFrameRateLimit`.

---

## [x] ID11 - Hot reload leaves Inspector and EventBus pointing at unloaded code

**Priority:** P1  
**Area:** lavac / Inspector / EventBus

`UserGame::reload()` tears down the framework before `dlclose()`, but the
`Inspector` caches `VulkanRenderer*` and ImGui/Vulkan resources from the old
device. Nothing detached it before teardown, and `detach()` did not reset
`m_imguiPool`, so `lavac inspect` could dereference destroyed state or recreate
resources on the old device.

`Application::unloadGame()` also left `EventBus` intact. Registered `trigger`
callbacks were compiled into the game `.so`; after `dlclose()` they pointed at
unloaded code. The example worked around duplicate registration with a
`try/catch`, but the stale trigger remained callable.

### Resolution (unreleased)

- `Application::unloadGame()` clears the EventBus after scheduler jobs are
  destroyed and before containers/the framework are destroyed. Event
  registrations are now session-scoped; `lavaEngineMain` re-registers them
  after each load.
- `UserGame` exposes `setReloadHooks()`/`clearReloadHooks()`. `reload()` runs
  the before hook while the old framework is alive, then the after hook once
  the replacement library has loaded. `unload()` also runs the before hook.
- `Inspector` registers detach/rebind hooks, resets `m_imguiPool` and stale
  history state during detach, and safely stops if no renderer is available
  after a failed reload.
- Updated the sample event registration, README teardown order, and EventBus
  lifecycle documentation.
- Covered by `testApplicationUnloadClearsEventBus`; GUI reload remains a
  manual verification.
