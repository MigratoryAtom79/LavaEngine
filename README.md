# LavaEngine [0.11.0-indev]

**LavaEngine** is a modular C++ game framework designed to provide the infrastructure needed to build games and interactive applications without imposing a predefined engine architecture.

Unlike traditional engines such as Unity or Unreal, LavaEngine does not require concepts such as Scenes, GameObjects, Entities, Components, or a mandatory game lifecycle. Instead, it provides a small set of general-purpose primitives that developers can combine and organize however they choose.

The central concept of LavaEngine is the **Container**.

---

## Containers

A **Container** is an isolated context that can hold modules, resources, interfaces, and other state, while providing controlled communication with other Containers.

A Container does not necessarily represent a scene or even a game. It could represent:

- A game
- A level
- A level editor
- A simulation
- An asset processor
- A dedicated server
- A UI system
- A networking system
- A collection of shared state
- Or any custom system the developer needs

For example:

```text
LavaEngine
│
├── Game Container
│   ├── ECS
│   ├── Physics
│   ├── World
│   └── Renderer
│
├── Level Container
│   └── LevelData
│
└── Editor Container
    ├── UI
    └── World Editor
```

There is no requirement for these Containers to follow the same structure.

A game might use an ECS and physics system, while an asset-processing Container might contain nothing but importers and asset data.

The Container is fundamentally a **boundary for composition, state, and communication**, rather than a predefined type of application object.

### Container Communication

Containers communicate through explicit mechanisms rather than relying on hidden global state.

LavaEngine provides three primary communication mechanisms:

```text
Resources
    Share data and state.

Interfaces
    Provide access to behavior.

Events
    Notify a subscribed Container that something happened.
```

### Resources

A Container can export a resource for other Containers to access.

For example:

```c++
ResourceHandle levelData = level.createResource<LevelData>(...);
level.exportResource<LevelData>(levelData);

ResourceView<LevelData> view =
    game.importResource<LevelData>(level, levelData);
```

Resources do not necessarily have to be copied.

They can be represented using handles or shared references, allowing multiple Containers to access the same underlying resource.

This is particularly useful for large resources such as:

- Voxel data
- World data
- Meshes
- Textures
- Audio data
- GPU buffers
- Vulkan resources

For example:

```text
Level Container
      │
      └── GPU World Buffer
              │
              ├────────→ Game Container
              │
              └────────→ Editor Container
```

Both Containers can potentially access the same underlying resource without unnecessary CPU or GPU copies.

### Interfaces

Resources are useful when a Container needs access to data.

When a Container needs to request behavior, it can instead use an interface.

For example:

```text
Editor
   │
   │ requires
   ↓
WorldEditorInterface
   │
   ↓
World
```

The Editor does not need to know how the World is internally implemented.

It only needs to know what the `WorldEditorInterface` provides.

This creates a clear boundary between systems and prevents Containers from becoming tightly coupled to each other's implementations.

A Container owns at most one typed interface:

```c++
struct IWorldEditor : Interface
{
    virtual ResourceHandle spawn(const SpawnDesc&) = 0;
};

class WorldEditor final : public IWorldEditor
{
public:
    ResourceHandle spawn(const SpawnDesc& desc) override
    {
        return owner().createResource<EntityResource>(desc);
    }
};

world.addInterface<IWorldEditor, WorldEditor>();

auto* editor = world.getInterface<IWorldEditor>();

if (editor)
    editor->spawn(desc);
```

Resolve interfaces through a Container reference; there is no app-wide
`findInterface<T>()`. Interfaces are non-owning behavior facades over the owning
Container, invalid after that Container is destroyed or the session is
unloaded. Use `owner()` to reach modules/resources. `Container::variables()`
returns the current bindings, and each `Variable` stores a non-owning pointer
to the exposed caller-owned value; do not keep those pointers past the
binding's lifetime.

Override `Interface::imgui()` to draw interface-specific controls in the
Inspector's Containers panel.

### Events

Events provide a mechanism for transient communication.

For example:

```text
Game ──── PlayerDied ─────→ UI
Game ──── LevelLoaded ────→ Editor
Editor ── AssetChanged ───→ Game
```

Events are intended for notifications rather than replacing resources or
interfaces. Each event type has exactly one subscriber per session; create a
distinct event type for each listener that needs notification.

A useful distinction is:

```text
Resources
    "Here is something."

Interfaces
    "Here is something you can do."

Events
    "Something happened."
```

### Modules

**Modules** provide functionality.

They are optional and are added to Containers explicitly.

Examples include:

- ECS
- Physics
- Rendering
- Audio
- Networking
- UI
- Asset management
- Logger

For example:

```c++
Container game;

game.addModule<ECS>();
game.addModule<Physics>();
game.addModule<GraphicalPipline>(
    device,
    layout,
    renderPass,
    vertexLayout,
    "main.vert",
    "main.frag"
);
```

A different Container might only require:

```c++
Container assetCompiler;

assetCompiler.addModule<ModelImporter>();
assetCompiler.addModule<TextureCompiler>();
```

LavaEngine does not need to know what `ModelImporter` or `TextureCompiler` are.

Developers can create their own modules without modifying the framework.

This is a fundamental part of LavaEngine's design:

> **Functionality should be added to the framework through modules rather than being forced into its core.**

---

## Inspector

The **Inspector** is a development and debugging system that allows developers to visualize, monitor, and modify Containers and their contents at runtime.

The Inspector does not need to know what a Container represents.

It can inspect:

- Containers
- Modules
- Resources
- Exported resources
- Interfaces
- Variables
- Runtime state
- Performance information
- Logs
- Debug information
- GPU resources
- Custom user-defined data

For example:

```text
Inspector
│
├── Game Container
│   ├── Modules
│   │   ├── ECS
│   │   ├── Physics
│   │   └── Renderer
│   │
│   ├── Resources
│   │   ├── World
│   │   ├── Player
│   │   └── GPU World Buffer
│   │
│   ├── Exports
│   │   └── WorldEditorInterface
│   │
│   └── Variables
│       ├── gravity
│       ├── tickRate
│       └── debugRendering
│
└── Level Container
    ├── Resources
    │   └── LevelData
    └── Variables
        ├── seed
        └── chunkSize
```

### Inspectable Properties

Modules can expose variables to the Inspector.

For example:

```c++
class Physics : public Module
{
public:
    void imgui() const override {}

    float gravity = 9.81f;
    bool debugDraw = false;
};
```

After adding the module, expose its values through the owning Container:

```c++
auto& physics = game.addModule<Physics>();

game.expose("gravity", &physics.gravity);
game.expose("debugDraw", &physics.debugDraw);
```

The Inspector can then automatically display:

```text
Physics

Gravity       [ 9.81 ]
Debug Draw    [ ✓ ]
```

`expose()` binds caller-owned memory directly. With the default `live = true`,
Inspector edits modify the value immediately, so `physics.gravity` changes
while the game runs. Pass `false` to keep a binding read-only without copying
it. The bound object must outlive the binding and keep a stable address; call
`unexpose("gravity")` before destroying it early.

For example:

```text
Render Distance
16 → 32 → 64 → 128

Physics Gravity
9.81 → 5.0 → 0.0
```

This can be used for:

- Rendering settings
- Physics parameters
- AI settings
- World generation
- Debug features
- Network configuration
- Audio settings

### Custom Inspector Interfaces

Simple types can be inspected automatically:

```text
bool
int
float
string
vector
```

More complicated modules can provide custom Inspector interfaces.

For example, a Renderer could expose:

```text
Renderer
├── Statistics
│   ├── FPS
│   ├── Frame Time
│   ├── Draw Calls
│   └── Triangles
│
├── GPU Memory
│   ├── Buffers
│   ├── Textures
│   └── Allocations
│
└── Debug
    ├── Wireframe
    ├── Bounding Boxes
    └── Chunk Borders
```

An ECS module could expose:

```text
ECS
├── Entities
├── Components
├── Systems
└── Archetypes
```

The Inspector remains generic while modules provide domain-specific information.

### Inspector Commands

The Inspector can also expose actions rather than only variables.

For example:

```text
Physics

Gravity        9.81
Debug Draw     ✓

[Reset Simulation]
[Clear Bodies]
[Rebuild Broadphase]
```

These commands are registered on the owning Container and executed by the
Inspector:

```c++
auto& physics = game.addModule<Physics>();

game.addCommand("Reset Simulation", [&physics] { physics.reset(); });
game.addCommand("Clear Bodies", [&physics] { physics.clearBodies(); });
```

`addCommand()` replaces an existing command with the same name. Captured state
must outlive the command; call `removeCommand("Reset Simulation")` before
destroying it. Commands run synchronously on the main thread, so they can
safely touch game state while the simulation is paused.

This allows development tools to interact with systems without requiring the Inspector to understand their internal implementation.

---

## Scheduler

LavaEngine does not require every Container to implement a fixed lifecycle such as:

```c++
Start();
Update();
Render();
Stop();
```

Instead, individual Modules can register work with the framework's scheduler when necessary.

For example:

```text
Game Container
│
├── Physics ───── update
├── ECS ───────── update
├── Renderer ──── render
└── World ─────── no scheduled work
```

This means a data-only Container does not need to pretend to have an update loop.

The scheduler operates on the systems that actually need scheduling.

You can schedule a job like this:

```c++
engine.getScheduler()->createJob(
        [&]() -> int
        {
            updateGame(); // your per-frame work
            return 0;     // repeat on the next pass
        }
    );
```

The return of the task decides what happens next. Returning `0` repeats the
job on the next pass, `> 0` completes it successfully, and `< 0` cancels it.
Completed and cancelled jobs run their optional exit callback and no longer
execute.

Run all job with:
```c++
engine.run();
```

`run()` is paced at 60 FPS by default. Call `setFrameRateLimit(0)` to disable
pacing, or `requestExit()` to stop a job that would otherwise repeat forever.
`step()` advances a single pass and does not pace; use `waitForNextFrame()` when
driving your own loop.

### WorkerPool and JobMarket

The `Scheduler` also owns an optional `WorkerPool`. A `WorkerPool` owns a
`JobMarket`: a thread-safe queue of work. Worker threads that are waiting on the
market are unemployed; posting a job wakes one, which is hired to run it.

```c++
engine.getScheduler()->postJobToMarket(
    [&]
    {
        // Runs on a worker thread. No result is returned.
        processChunk(chunk);
    },
    "process chunk"
);
```

`postJobToMarket()` is fire-and-forget: it does not create a `Scheduler::Job`
and does not participate in `JobID`s, dependencies, or completion tracking.

Jobs can be prioritized. Higher `priority` values are taken first, and jobs
with the same priority stay in FIFO order:

```c++
engine.getScheduler()->postJobToMarket(
    [] { loadCriticalChunk(); },
    "critical chunk",
    10
);
```

The same priority argument is available on `WorkerPool::post` and
`JobMarket::post` / `tryPost`, or set `JobMarket::WorkItem::priority` directly.

For parallel work with a phase boundary, use `WorkerPool::parallelFor`:

```c++
auto& pool = engine.getScheduler()->workerPool();

pool.parallelFor(chunks.size(), 16, [&](std::size_t i)
{
    generateTerrain(chunks[i]);
}); // returns only when every chunk is generated

pool.parallelFor(chunks.size(), 16, [&](std::size_t i)
{
    buildMesh(chunks[i]);
}); // next phase
```

`parallelFor` blocks until every index is processed, so consecutive calls form
host-driven phase barriers. Nested calls made from a worker, and calls on a
stopped pool, run inline on the calling thread to avoid deadlock. `TaskGroup`
is the underlying count/wait primitive if you need to dispatch manually.

`JobMarket` can be bounded (`JobMarket(capacity)` or
`WorkerPool(workers, onError, marketCapacity)`). `post` blocks while a bounded
market is full; `tryPost` returns `false` instead. A capacity of `0` is
unbounded.

Worker tasks must not touch main-thread-only engine state (`Container`,
`Module`, `ResourceRegistry`, `EventBus`, GLFW, ImGui, Vulkan). Hand workers
stable, plain data and let the main thread commit results afterwards.

`stopWorkers()` closes the market, discards jobs that have not been hired, and
waits for in-flight jobs before joining the threads. `Application::unloadGame()`
does this automatically before containers are destroyed or a hot-reloaded game
library is unloaded. Posting again after `stopWorkers()` lazily creates a fresh
pool.

---

## Application Teardown

`Application` owns every framework, scheduler job, Container and resource of
a game session. `unloadGame()` (also called by `shutdown()` and by hot
reload) tears the session down in a fixed, deterministic order:

1. **Teardown hooks** — every Container's `onUnload()` runs, which by
   default forwards to each Module's `onUnload()`. This is the last point
   at which game code may release non-owning references to objects that are
   still alive (e.g. a renderer overlay callback capturing Container state).
2. **Framework shutdown** — `Framework::shutdown()` runs; the framework
   releases its references into Container-owned objects (overlay callback,
   offscreen target).
3. **Scheduler** — all jobs are destroyed before the objects they may
   borrow from.
4. **WorkerPool** — in-flight tasks finish, jobs that have not been hired are
   discarded, and worker threads are joined before containers are destroyed or
   a game library is unloaded.
5. **EventBus** — all event registrations, triggers and subscriptions are
   cleared before the game library that defined them is unloaded. Event
   handles do not survive a reload.
6. **Containers** — destroyed (Modules, then Resources and exposed boxes).
7. **Framework** — destroyed last.

### Ownership model

- A `ResourceRegistry` (and its Container) is the single owner of every
  `Resource`. `ResourceHandle`s are generation-checked on every access and
  are invalidated by `remove()`, `clear()` or destruction.
  `ResourceView` keeps a weak reference and reports `invalid()` once the
  owner or resource is gone.
- `importResource()` returns a `ResourceView`, never a raw pointer.
- `expose()` is a non-owning binding. With `live = true` the Inspector edits
  caller memory directly; with `live = false` it only reads it. The target
  must outlive the binding or be removed with `unexpose()` first.
- `addCommand()` registers an Inspector button. The action's captures must
  outlive the command or be removed with `removeCommand()` first.
- Modules point back to their owning Container; the back-pointer is re-pointed
  when the Container is moved.

Modules can run cleanup at teardown by overriding `onUnload()`:

```c++
struct CleanupModule : Module
{
    void imgui() const override {}

    void onUnload() override
    {
        renderer->clearOverlayCallback();
    }
};
```

Hot reload (`lavac`) calls `unloadGame()` before loading the new library, so
the whole session (except the `Application` object and its `Logger`) is
rebuilt. The EventBus is session-scoped: `lavaEngineMain` must register its
events again after every load. `lavac inspect` handles the Inspector lifecycle
automatically — it detaches before teardown and rebinds to the new
`VulkanRenderer` after the library loads.

---

## Manual Smoke Test

This repository builds the engine, `lavac`, and the unit tests. It does not
ship a playable example game, so the smoke test uses your own shared library.

Build and run the test suite from the repository root:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To run and inspect a game, build a shared library that exports:

```c++
extern "C" void lavaEngineMain(LavaEngine::Application& engine);
```

Then launch it through `lavac`:

```bash
./build/lavac/lavac build/YourGame.so inspect
```

`lavac` resolves the game path relative to the current directory and copies it
to a runtime file before loading it.

Verify:

- The window opens and renders your game.
- The Inspector lists Containers, Modules, Resources, Variables, Commands,
  Interfaces, and Events.
- Exposed live variables and interface controls update your game.
- Inspector commands run and invoked events reach their subscriber.
- Closing the window exits cleanly without an ASan/UBSan report.
- Rebuilding your game library while `lavac inspect` is running triggers a
  reload without a crash.
- `ctest --test-dir build --output-on-failure` passes.

---

## Design Principles

LavaEngine is built around several principles.

### 1. No mandatory game architecture

LavaEngine does not require ECS, scenes, entities, components, game objects, or any other particular design pattern.

### 2. Containers are generic

A Container can represent anything the developer needs.

### 3. Modules are optional

Functionality is added explicitly through modules.

### 4. Resources are independent

Resources are not inherently tied to a particular module or Container.

### 5. Communication is explicit

Resources, interfaces, and events provide controlled communication between Containers.

### 6. Avoid unnecessary copying

Resource handles and shared resources allow large resources to be shared efficiently.

### 7. Keep the core small

The framework should provide fundamental infrastructure rather than implementing every possible engine feature.

### 8. Make systems inspectable

Modules and resources should be able to expose useful runtime information through the Inspector.

### 9. Provide escape hatches

Advanced developers should be able to access lower-level systems when the abstractions provided by LavaEngine are insufficient.

### 10. The developer owns the architecture

LavaEngine provides building blocks. The developer decides how those building blocks are organized.

---

## Philosophy

LavaEngine follows one fundamental principle:

> **The framework should make architecture possible, not make architecture mandatory.**

LavaEngine provides the infrastructure for composing systems, managing resources, connecting modules, communicating between isolated contexts, and inspecting applications at runtime.

The developer decides what those systems actually mean and how they are organized.

A developer can therefore use LavaEngine to build:

```text
A conventional game
A custom ECS architecture
A level editor
A simulation
A dedicated server
An asset pipeline
A rendering experiment
A completely custom engine
```

without forcing any of them into the same predefined architecture.

LavaEngine is not intended to be a smaller version of Unreal or Unity.

It is intended to be a **flexible foundation upon which different engine architectures can be built**.