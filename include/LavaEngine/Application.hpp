#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../LavaVK/include/LavaVK/LavaVK.hpp"
#include "../../external/glm/glm/glm.hpp"

#include "Window.hpp"
#include "Container.hpp"
#include "EventBus.hpp"
#include "Framework.hpp"
#include "Frameworks.hpp"
#include "Logger.hpp"
#include "Module.hpp"
#include "Resource.hpp"
#include "Scheduler.hpp"

// built-ins
#include "built-ins/containers/MeshManager/MeshManager.h"
#include "built-ins/modules/GraphicalInterface/GraphicalInterface.h"
#include "built-ins/modules/MeshLoader/MeshLoader.h"

namespace LavaEngine
{
    using namespace LavaVK;

    /**
     * @brief Root object and owner of a LavaEngine game session.
     * @detail Owns the Scheduler, Logger, framework (Framework), and every
     * Container created through createContainer(). Lifecycle is driven
     * through this object: create containers/frameworks, then run() or
     * step() the game loop.
     * @note Ownership: `Application` is the session root. It uniquely owns
     * the framework, the scheduler's jobs, the containers, and the logger.
     * References and pointers borrowed from it (e.g. getScheduler(),
     * getLogger(), findFramework<>(), findContainer<>()) are valid only
     * while the Application outlives them. unloadGame() teardown runs in fixed phases:
     * (1) container/module onUnload() hooks, (2) framework shutdown(),
     * (3) scheduler job destruction, (4) worker pool drain/join, (5) event
     * bus clearing, (6) container destruction, (7) framework destruction.
     * A teardown hook at phase 1 is
     * the last point at which non-owning references (e.g. the renderer's
     * overlay callback) may be released while the borrowed-from objects are
     * still alive.
     * @example
     * @code
     * LavaEngine::Application app;
     * auto& world = app.createContainer<Container>("world");
     * app.setFramework<VulkanRenderer>(app, 1280, 720, "Game");
     * app.run();
     * @endcode
     */
    class Application
    {
    public:
        Application() = default;

        ~Application()
        {
            shutdown();
        }

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /**
         * @brief Runs scheduled jobs until they finish or exit is requested.
         * @detail Passes are paced at frameRateLimit() (60 FPS by default).
         * Jobs created during execute() are picked up on the next pass. A job
         * that repeats forever must call requestExit() to stop run().
         */
        void run()
        {
            while (!m_exitRequested && !m_scheduler.done())
            {
                const auto frameStart =
                    std::chrono::steady_clock::now();

                m_scheduler.execute();

                if (m_exitRequested || m_scheduler.done())
                    break;

                waitForNextFrame(frameStart);
            }
        }

        /**
         * @brief Advances one scheduler pass.
         * @return 1 when the scheduler is done or exit was requested, else 0.
         * @note Not paced; real-time callers should use waitForNextFrame().
         */
        int step()
        {
            if (m_exitRequested || m_scheduler.done())
                return 1;

            m_scheduler.execute();

            return (m_exitRequested || m_scheduler.done()) ? 1 : 0;
        }

        /**
         * @brief Requests that run() and step() stop.
         * @detail Sticky until unloadGame() resets the session. This is the
         * explicit exit path for jobs that would otherwise repeat forever.
         */
        void requestExit()
        {
            m_exitRequested = true;
        }

        [[nodiscard]]
        bool exitRequested() const
        {
            return m_exitRequested;
        }

        /**
         * @brief Sets the target frame rate used by run() and waitForNextFrame().
         * @param framesPerSecond Target FPS. Values <= 0 disable pacing.
         */
        void setFrameRateLimit(double framesPerSecond)
        {
            m_frameRateLimit =
                framesPerSecond > 0.0 ? framesPerSecond : 0.0;
        }

        [[nodiscard]]
        double frameRateLimit() const
        {
            return m_frameRateLimit;
        }

        /**
         * @brief Sleeps until one frame interval has elapsed since frameStart.
         * @param frameStart Time recorded at the start of the current frame.
         * @note Returns immediately when pacing is disabled or the frame
         * already overran its interval.
         */
        void waitForNextFrame(
            std::chrono::steady_clock::time_point frameStart
        ) const
        {
            if (m_frameRateLimit <= 0.0)
                return;

            const auto interval =
                std::chrono::duration_cast<
                    std::chrono::steady_clock::duration
                >(
                    std::chrono::duration<double>(
                        1.0 / m_frameRateLimit
                    )
                );

            const auto elapsed =
                std::chrono::steady_clock::now() - frameStart;

            if (elapsed < interval)
            {
                std::this_thread::sleep_for(
                    interval - elapsed
                );
            }
        }

        template <typename T, typename... Args>
        T& createContainer(Args&&... args)
        {
            auto container =
                std::make_unique<T>(
                    std::forward<Args>(args)...
                );

            T& ref = *container;

            m_containers.push_back(
                std::move(container)
            );

            ref.setEventBus(&m_eventBus);

            return ref;
        }

        [[nodiscard]]
        const std::vector<std::unique_ptr<Container>>& containers() const
        {
            return m_containers;
        }


        template <typename T, typename... Args>
        T& setFramework(Args&&... args)
        {
            auto framework =
                std::make_unique<T>(
                    *this,
                    std::forward<Args>(args)...
                );

            T& ref = *framework;

            m_framework = std::move(framework);

            return ref;
        }

        template <typename T>
        T* findFramework()
        {
            return dynamic_cast<T*>(m_framework.get());
        }

        template <typename T>
        T* findContainer()
        {
            for (auto& container : m_containers)
            {
                if (auto* result = dynamic_cast<T*>(container.get()))
                {
                    return result;
                }
            }

            return nullptr;
        }

        Scheduler* getScheduler()
        {
            return &m_scheduler;
        }

        /**
         * @brief Requests a new, named event of type Event from the session.
         * @param name Human-readable label shown by tooling (e.g. Inspector).
         * @return The event's stable handle, usable with EventBus::emit(handle).
         * @throws std::runtime_error if an event of type Event already exists.
         * @example
         * @code
         * auto handle = app.createEvent<PlayerDied>("PlayerDied");
         * @endcode
         */
        template <typename Event>
        EventBus::EventHandle createEvent(const std::string& name)
        {
            return m_eventBus.createEvent<Event>(name);
        }

        [[nodiscard]]
        EventBus& eventBus()
        {
            return m_eventBus;
        }

        [[nodiscard]]
        const EventBus& eventBus() const
        {
            return m_eventBus;
        }

        Logger* getLogger()
        {
            return &m_logger;
        }

        void unloadGame()
        {
            // Phase 1 - teardown hooks: give containers/modules a chance
            // to release borrowed references while the framework is alive.
            for (auto& container : m_containers)
                container->onUnload();

            // Phase 2 - framework detach: release the framework's
            // references into container-owned objects (e.g. overlay
            // callback, offscreen target).
            if (m_framework) m_framework->shutdown();

            // Phase 3 - scheduler: destroy jobs before the objects they
            // may borrow from.
            m_scheduler.clear();

            // Phase 3.5 - workers: finish in-flight tasks, discard unhired
            // jobs, and join before containers are destroyed or a hot-reloaded
            // game library is unloaded.
            m_scheduler.stopWorkers();

            // Phase 4 - event bus: drop registrations and their type-erased
            // callbacks before the game library that defined them is unloaded.
            m_eventBus.clear();

            // Phase 5 - containers: destroy modules, then resources.
            m_containers.clear();

            // Phase 6 - framework (shutdown() already ran; resetting
            // prevents a second shutdown() call).
            if (m_framework) m_framework.reset();

            m_exitRequested = false;
        }

    private:
        void shutdown()
        {
            if (m_shutdown)
                return;

            m_shutdown = true;

            unloadGame();
        }

        Scheduler m_scheduler;
        Logger m_logger;
        EventBus m_eventBus;

        std::vector<std::unique_ptr<Container>> m_containers;
        std::unique_ptr<Framework> m_framework;

        bool m_shutdown = false;
        bool m_exitRequested = false;

        double m_frameRateLimit = 60.0;
    };
}