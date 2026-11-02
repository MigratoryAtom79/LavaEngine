#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "EventBus.hpp"
#include "Interface.hpp"
#include "Module.hpp"
#include "Resource.hpp"

namespace LavaEngine
{
    class Application;

    enum class VarType
    {
        Float,
        Int,
        Bool,
        String,
        Double,
    };

    /**
     * @brief A name/type/value binding exposed to the Inspector.
     * @detail Created via Container::expose() or Container::exposeLive();
     * the Inspector reads and writes through `ptr` according to `type`.
     * @note Ownership: Owned by the owning Container (by value in
     * `m_variables`). Snapshot variables point into an owned heap box in
     * `m_boxes`. Live variables point at caller-owned memory that must
     * outlive the binding.
     */
    struct Variable
    {
        std::string name;
        void* ptr;
        VarType type;
        bool live = false;
    };


    /**
     * @brief Logical grouping unit: owns modules, resources, one interface and exposed variables.
     * @detail A Container is created on an Application via
     * createContainer<T>() and hosts Modules (ModuleRegistry), Resources
     * (ResourceRegistry), an optional typed Interface, and inspector-exposed
     * variables. Modules can reach the owning Container through
     * Module::getContainer(); an Interface can reach it through Interface::owner().
     * @note Ownership: A Container is uniquely owned by the Application
     * that created it (std::unique_ptr in Application::m_containers). It
     * owns its modules, resources, interface and variables and destroys them
     * during teardown. Any reference obtained from it is invalid once the
     * Container is destroyed or the session is unloaded.
     * @example
     * @code
     * auto& world = app.createContainer<Container>("world");
     * auto& player = world.addModule<PlayerModule>();
     * ResourceHandle mesh = world.createResource<MeshResource>(...);
     * @endcode
     */
    class Container
    {
    public:
        Container()
        {
            m_modules.setContainer(this);
        }

        explicit Container(std::string name);

        virtual ~Container()
        {
            removeInterface();
            m_modules.clear();
            m_resources.clear();
        }

        // Containers cannot be copied.
        Container(const Container&) = delete;
        Container& operator=(const Container&) = delete;

        // Containers can be moved.
        Container(Container&& other) noexcept;
        Container& operator=(Container&& other) noexcept;


        template <typename T, typename... Args>
        T& addModule(Args&&... args)
        {
            return m_modules.add<T>(
                std::forward<Args>(args)...
            );
        }


        template <typename T>
        T* getModule()
        {
            return m_modules.get<T>();
        }


        template <typename T>
        bool hasModule() const
        {
            return m_modules.has<T>();
        }


        template <typename T>
        void removeModule()
        {
            m_modules.remove<T>();
        }


        /**
         * @brief Creates and owns this Container's interface.
         * @param args Forwarded to the implementation constructor.
         * @return A reference to the interface as its public type I.
         * @throws std::runtime_error if an interface already exists.
         * @example
         * @code
         * auto& editor = world.addInterface<IWorldEditor, WorldEditor>();
         * @endcode
         */
        template <typename I, typename Impl = I, typename... Args>
        I& addInterface(Args&&... args)
        {
            static_assert(
                std::is_base_of_v<Interface, I>,
                "Interface type must derive from Interface"
            );
            static_assert(
                std::is_base_of_v<I, Impl>,
                "Implementation must derive from the interface type"
            );
            static_assert(
                !std::is_abstract_v<Impl>,
                "Interface implementation must be concrete"
            );

            if (m_interface)
            {
                throw std::runtime_error(
                    "Container already has an interface"
                );
            }

            auto interface = std::make_unique<Impl>(
                std::forward<Args>(args)...
            );

            Interface* base = interface.get();
            I* result = interface.get();

            base->setOwner(this);
            m_interface = std::move(interface);

            return *result;
        }


        /**
         * @brief Returns this Container's interface viewed as I, or null.
         */
        template <typename I>
        I* getInterface()
        {
            static_assert(
                std::is_base_of_v<Interface, I>,
                "Interface type must derive from Interface"
            );

            return dynamic_cast<I*>(m_interface.get());
        }


        template <typename I>
        const I* getInterface() const
        {
            static_assert(
                std::is_base_of_v<Interface, I>,
                "Interface type must derive from Interface"
            );

            return dynamic_cast<const I*>(m_interface.get());
        }


        template <typename I>
        bool hasInterface() const
        {
            return getInterface<I>() != nullptr;
        }


        /**
         * @brief Destroys this Container's interface, if any.
         */
        void removeInterface()
        {
            if (!m_interface)
                return;

            m_interface->setOwner(nullptr);
            m_interface.reset();
        }


        /**
         * @brief Raw interface access for tooling; gameplay should use
         * getInterface<I>().
         */
        [[nodiscard]]
        Interface* interface()
        {
            return m_interface.get();
        }


        [[nodiscard]]
        const Interface* interface() const
        {
            return m_interface.get();
        }


        template <typename T, typename... Args>
        ResourceHandle createResource(
            Args&&... args
        )
        {
            return m_resources.create<T>(
                std::forward<Args>(args)...
            );
        }


        template <typename T>
        T* getResource(
            ResourceHandle handle
        )
        {
            return m_resources.get<T>(handle);
        }


        template <typename T>
        const T* getResource(
            ResourceHandle handle
        ) const
        {
            return m_resources.get<T>(handle);
        }


        template <typename T>
        void exportResource(ResourceHandle handle)
        {
            T* resource = m_resources.get<T>(handle);

            if (!resource)
                return;

            resource->exportResource();
        }


        /**
         * @brief Borrows an exported resource from another Container.
         * @note Ownership: Non-owning. The returned view tracks the
         * exporting registry's lifetime and reports invalid() once the
         * resource is removed or the exporter is destroyed.
         * @param exporter The Container whose resource is borrowed.
         * @param handle A ResourceHandle valid in the exporter's registry.
         * @return A ResourceView<T>, invalid if the resource is missing,
         * not exported or the exporter no longer owns it.
         */
        template <typename T>
        ResourceView<T> importResource(
            Container& exporter,
            ResourceHandle handle
        )
        {
            if (!exporter.m_resources.contains(handle))
                return {};

            const T* resource = exporter.m_resources.get<T>(handle);

            if (!resource || !resource->isExported())
                return {};

            return exporter.m_resources.view<T>(handle);
        }

        void clearModules()
        {
            m_modules.clear();
        }


        [[nodiscard]]
        const std::string& name() const;

        // Read-only access to this container's registries, for tooling
        // (e.g. the Inspector). Gameplay code should prefer the
        // add/get/has/remove Module/Resource helpers above instead.
        [[nodiscard]]
        const ModuleRegistry& modules() const
        {
            return m_modules;
        }

        [[nodiscard]]
        const ResourceRegistry& resources() const
        {
            return m_resources;
        }

        virtual void imgui() const {}

        // Teardown hook: called by the Application during unloadGame()
        // while the framework is still alive. Default forwards to every
        // module's onUnload(); override to release container-held borrows.
        virtual void onUnload()
        {
            for (auto& module : m_modules.m_modules)
                module->onUnload();
        }

        void expose(const std::string& name, float* variable);
        void expose(const std::string& name, int* variable);
        void expose(const std::string& name, bool* variable);
        void expose(const std::string& name, double* variable);
        void expose(const std::string& name, std::string* variable);

        /**
         * @brief Binds a caller-owned value to the Inspector for live editing.
         * @param variable Must outlive this binding and keep a stable address.
         * @note The Inspector writes directly to `*variable`. Call unexpose()
         * before destroying it, or keep it alive for the Container's lifetime.
         */
        void exposeLive(const std::string& name, float* variable);
        void exposeLive(const std::string& name, int* variable);
        void exposeLive(const std::string& name, bool* variable);
        void exposeLive(const std::string& name, double* variable);
        void exposeLive(const std::string& name, std::string* variable);

        /**
         * @brief Removes an exposed snapshot or live binding by name.
         */
        void unexpose(const std::string& name);

        /**
         * @brief Registers `handler` as this container's subscription to Event.
         * @param handler Callable invoked with `const Event&` on every emit.
         * @return The EventBus::Subscription RAII handle owning the slot.
         * @throws std::runtime_error if the event was not created first, or if
         * another container already subscribed to it (one subscriber only).
         * @example
         * @code
         * auto sub = ui.subscribe<PlayerDied>(
         *     [&](const PlayerDied& e) { scoreboard.remove(e.id); });
         * @endcode
         */
        template <typename Event, typename Handler>
        EventBus::Subscription subscribe(Handler handler)
        {
            return eventBus().subscribe<Event>(*this, std::move(handler));
        }

        /**
         * @brief The Application-wide event bus this container can emit on and
         * subscribe to.
         * @throws std::logic_error if the container was not created via
         * Application::createContainer(), so it holds no bus back-pointer.
         */
        [[nodiscard]]
        EventBus& eventBus() const
        {
            if (!m_eventBus)
            {
                throw std::logic_error(
                    "Container::eventBus(): container is not owned by an "
                    "Application (create it via createContainer())"
                );
            }

            return *m_eventBus;
        }

        [[nodiscard]]
        std::vector<Variable>& variables()
        {
            return m_variables;
        }


    private:
        friend class Application;

        void setEventBus(EventBus* bus)
        {
            m_eventBus = bus;
        }

        // Owns the boxed copy behind each exposed Variable. Heap-allocated
        // so the address the Variable::ptr points into stays stable even if
        // m_variables reallocates.
        struct ExposedBox
        {
            bool boolean = false;
            int integer = 0;
            float floating = 0.0f;
            double doublePrec = 0.0;
            std::string stringValue;
        };

        void rebindExposedVariables() noexcept;

        std::string m_name;

        ModuleRegistry m_modules;
        ResourceRegistry m_resources;
        EventBus* m_eventBus = nullptr;
        std::vector<Variable> m_variables;
        std::vector<std::unique_ptr<ExposedBox>> m_boxes;
        std::unique_ptr<Interface> m_interface;
    };
}