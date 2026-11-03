# LavaEngine - Changelog

> **Snapshot**: 2026-09-18
> **Repository**: https://github.com/RanveerisdeGOAT/LavaEngine
> **Version observed**: 0.11.0-indev
>
> This file contains all notable changes to LavaEngine are documented here.
>

## [0.11.0-indev] - 1/11/26

### Added:
* Added priority tasks in job market.
* Added Inspector commands.

## [0.10.0-indev] - 1/11/26

### Added:
* Added multi-threading using `JobMarket()` and `WorkerPool()`.

## [0.9.0-indev] - 1/11/26

### Added:
* Added Events
* Added Interfaces

### Fixed:
* Logger accessors are thread-safe. (ID7)
* Window is now non=copyable and immovable to prevent destructor from calling `glfwTerminate()`. (ID8)
* Application::run()`/`step()` wint busy-spin. (ID10)

## [0.8.2-indev] - 19/09/26

### Fixed:
* The contract is now defined in `Scheduler`'s doxygen (`include/LavaEngine/
Scheduler.hpp`) (ID2)
* Scheduler is not re-entrant (iterator invalidation / UB) (ID3)


## [0.8.1-indev] - 18/09/26

### Fixed:
* Modules registered via `addModule<T>()` never had their container back-pointer set, so `Module::getContainer()` dereferenced null until the container was moved (ID4).
* Modules kept pointing at the moved-from Container after a move, fixed by the addition of a container back-pointer on `ModuleRegistry::add()` (ID4).
* `typeID<T>()` used `typeid(T).hash_code()`, which is collision-prone and unstable across hot-reloaded shared objects; replaced with a compile-time FNV-1a hash of each type's canonical name (ID5).

## [0.8.0-indev] - 17/09/26

### Added:
* Generation-checked `ResourceHandle`s and `ResourceView<T>` weak borrows.
* `Container::importResource` returns a `ResourceView<T>` instead of a raw pointer.
* Teardown hooks `Container::onUnload()`/`Module::onUnload()` and phased, documented `Application::unloadGame()`.
* `expose()` boxes values into Container-owned storage, including a new `double` overload.
* `Inspector::detach()`.
* Regression test suite (`tests/`) wired into CTest.
* Documented the teardown/ownership model in `README.md`.

### Fixed:
* Modules kept pointing at the moved-from Container after a move (ID4).
* Stale resource handles could alias recreated resources (ID1).
* `importResource`/`expose` no longer hand out dangling raw pointers (ID6).
* Inspector edited a freed pointer on removed/teardown'd exposed variables, and corrupted `std::string` variables via a char-buffer cast (ID6).
* Inspector toolbar tooltip varargs format mismatch.

## [0.7.2-indev] - 17/09/26

### Added:
* Added issue tracker in `ISSUES.md`.

## [0.7.1-indev] - 16/09/26

### Fixed:
* Small progress to fixing (ID1).

## [0.7.0-indev] - 14/09/26

### Added:
* Notable improvements to the inspector.
    - Logger
    - Explorer
    - File Inspector
* Added Logger
* Added InputHandler

## [0.6.0-indev] - 08/09/26

### Added:
* Added Inspector

## [0.5.0-indev] - 31/08/26

### Added:
* Added Frameworks
* Added Hot-Reloading

## [0.4.0-indev] - 30/08/26

### Added:
* Added Lava Compiler `lavac` to run .so games.

## [0.3.0-indev] - 30/08/26

### Added:
* Added scheduler
* Added GraphicalPiplineModule

## [0.2.0-indev] - 29/08/26

### Added:
* Added window manager
* Implemented containers
* Implemented Modules
* Started implementing resources
* Added Renderer module

## [0.1.0-indev] - 28/08/26
* Initial commit


