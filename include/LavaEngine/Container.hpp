#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
        Vector,
    };

    /**
     * @brief A name/type/value binding exposed to the Inspector.
     * @detail Created via Container::expose(); the Inspector reads through
     * `ptr` according to `type`, using `elementType` for vectors. Live
     * variables are editable, non-live variables are read-only.
     * @note Ownership: Owned by the owning Container (by value in
     * `m_variables`). `ptr` is a non-owning pointer to caller-owned memory
     * that must outlive the binding.
     */
    struct Variable
    {
        std::string name;
        void* ptr;
        VarType type;
        bool live = false;
        VarType elementType = VarType::Float;
    };

    /**
     * @brief A named action exposed to the Inspector.
     * @detail Created via Container::addCommand(); the Inspector renders a
     * button and runs `action` when it is clicked.
     * @note Ownership: Owned by the Container (by value in `m_commands`).
     * Captures must outlive the command; call removeCommand() before
     * destroying captured state. Actions run synchronously on the main
     * thread.
     */
    struct Command
    {
        std::string name;
        std::function<void()> action;
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
            m_commands.clear();
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

        /**
         * @brief Binds a caller-owned value to the Inspector.
         * @param variable Must outlive this binding and keep a stable address.
         * @param live True for an editable binding, false for read-only.
         * @note The Inspector reads (and, when live, writes) `*variable`.
         * Call unexpose() before destroying it, or keep it alive for the
         * Container's lifetime.
         */
        void expose(const std::string& name, float* variable, bool live = true);
        void expose(const std::string& name, int* variable, bool live = true);
        void expose(const std::string& name, bool* variable, bool live = true);
        void expose(const std::string& name, double* variable, bool live = true);
        void expose(const std::string& name, std::string* variable, bool live = true);

        /**
         * @brief Live-binds a vector so the Inspector can edit its elements.
         * @param variable Must outlive this binding; the Inspector mutates it
         * in place.
         * @note Supported element types: bool, int, float, double,
         * std::string.
         */
        template <typename T>
        void expose(const std::string& name, std::vector<T>* variable, bool live = true);

        /**
         * @brief Removes an exposed binding by name.
         */
        void unexpose(const std::string& name);

        /**
         * @brief Registers or replaces an Inspector command.
         * @param name Button label shown in the Inspector.
         * @param action Callable run when the button is pressed.
         * @note The action runs synchronously on the main thread. Its
         * captures must outlive the command; call removeCommand() before
         * destroying captured state.
         */
        void addCommand(const std::string& name, std::function<void()> action);

        /**
         * @brief Removes an Inspector command by name.
         */
        void removeCommand(const std::string& name);

        /**
         * @brief Read-only access to this container's Inspector commands.
         */
        [[nodiscard]]
        const std::vector<Command>& commands() const
        {
            return m_commands;
        }

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

        [[nodiscard]]
        std::mutex& mutex() noexcept
        {
            return m_mutex;
        }

        [[nodiscard]]
        std::unique_lock<std::mutex> lock() const
        {
            return std::unique_lock<std::mutex>(m_mutex);
        }


    private:
        friend class Application;

        void setEventBus(EventBus* bus)
        {
            m_eventBus = bus;
        }

        template <typename T>
        static constexpr VarType exposedVectorElementType()
        {
            if constexpr (std::is_same_v<T, bool>)
                return VarType::Bool;
            else if constexpr (std::is_same_v<T, int>)
                return VarType::Int;
            else if constexpr (std::is_same_v<T, float>)
                return VarType::Float;
            else if constexpr (std::is_same_v<T, double>)
                return VarType::Double;
            else if constexpr (std::is_same_v<T, std::string>)
                return VarType::String;
            else
            {
                static_assert(
                    !std::is_same_v<T, T>,
                    "Unsupported vector element type for Container::expose"
                );

                return VarType::Float;
            }
        }

        std::string m_name;

        ModuleRegistry m_modules;
        ResourceRegistry m_resources;
        EventBus* m_eventBus = nullptr;
        std::vector<Variable> m_variables;
        std::vector<Command> m_commands;
        std::unique_ptr<Interface> m_interface;

        mutable std::mutex m_mutex;
    };



    template <typename T>
    void Container::expose(
        const std::string& name,
        std::vector<T>* variable,
        bool live
    )
    {
        m_variables.push_back(
            Variable{
                name,
                variable,
                VarType::Vector,
                live,
                exposedVectorElementType<T>()
            }
        );
    }
}