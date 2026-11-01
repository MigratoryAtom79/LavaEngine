#pragma once
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace LavaEngine
{
    /**
     * @brief Base class for data stored by a Container via its ResourceRegistry.
     * @detail Resources are created with Container::createResource<T>() and
     * owned by the registry. exportResource()/retractResource() mark a
     * resource as importable by other Containers without transferring
     * ownership.
     * @note Ownership: Owned (std::unique_ptr<Resource>) by the owning
     * ResourceRegistry/Container. Exporting does NOT transfer or share
     * ownership; other Containers may borrow it via importResource() while
     * the exporting Container keeps it alive. To survive the exporter's
     * destruction, borrow-results must be kept as ResourceView<T> instead of
     * raw T*.
     * @example
     * @code
     * struct Mesh : Resource {};
     * ResourceHandle h = world.createResource<Mesh>(...);
     * world.exportResource<Mesh>(h); // other containers may import it
     * @endcode
     */
    class Resource
    {
    public:
        virtual ~Resource() = default;

        virtual void exportResource()
        {
            m_exported = true;
        }

        virtual void retractResource()
        {
            m_exported = false;
        }

        [[nodiscard]] bool isExported() const
        {
            return m_exported;
        }

    private:
        bool m_exported = false;
    };

    using ResourceID = uint32_t;
    using ResourceGeneration = uint32_t;

    /**
     * @brief Non-owning borrow token identifying one Resource in a registry.
     * @detail Value type that addresses a resource by (id, generation).
     * Issued only by ResourceRegistry::create(); the generation advances on
     * every removal so a stale handle never aliases a freshly created
     * replacement. valid() reports whether the id is not the sentinel;
     * meaningful only against the registry that issued it.
     * @note Ownership: Non-owning. A handle is validated against the owning
     * registry on every access (get/contains/remove/view). It must not
     * outlive the owning registry/Container; for borrows that must survive
     * the owner, use ResourceView<T>.
     * @example
     * @code
     * ResourceHandle h = world.createResource<Mesh>(...);
     * if (Mesh* m = world.getResource<Mesh>(h)) draw(*m);
     * @endcode
     */
    class ResourceHandle
    {
    public:
        ResourceHandle() = default;

        [[nodiscard]]
        bool valid() const
        {
            return m_id != INVALID_ID;
        }

        [[nodiscard]]
        ResourceID id() const
        {
            return m_id;
        }

        [[nodiscard]]
        ResourceGeneration generation() const
        {
            return m_generation;
        }

        friend bool operator==(
            ResourceHandle lhs,
            ResourceHandle rhs
        )
        {
            return lhs.m_id == rhs.m_id
                && lhs.m_generation == rhs.m_generation;
        }

        friend bool operator!=(
            ResourceHandle lhs,
            ResourceHandle rhs
        )
        {
            return !(lhs == rhs);
        }

    private:
        static constexpr ResourceID INVALID_ID =
            static_cast<ResourceID>(-1);

        explicit ResourceHandle(
            ResourceID id,
            ResourceGeneration generation
        )
            : m_id(id), m_generation(generation)
        {
        }

        ResourceID m_id = INVALID_ID;
        ResourceGeneration m_generation = 0;

        friend class ResourceRegistry;
        friend class Container;
    };

    template <typename T>
    class ResourceView;

    /**
     * @brief Owns the Resources of a Container, addressed by ResourceHandle.
     * @detail create()/get()/contains()/remove()/clear() manage resource
     * lifetime; get() performs a type-checked dynamic_cast. Every access
     * verifies the handle generation, so handles invalidated by remove(),
     * clear() or id reuse are rejected rather than aliasing a new resource.
     * @note Ownership: Owned by its Container. Storage is heap-allocated and
     * held through std::shared_ptr so that borrows (ResourceView<T>) can
     * detect when the owning registry/Container ha s been destroyed; it is
     * non-copyable (a Container uniquely owns its registry) but movable.
     * remove(), clear(), or Container destruction invalidates handles and
     * expires views.
     * @example
     * @code
     * ResourceRegistry reg;
     * ResourceHandle h = reg.create<Mesh>(...);
     * Mesh* m = reg.get<Mesh>(h);
     * ResourceView<Mesh> v = reg.view<Mesh>(h);
     * @endcode
     */
    class ResourceRegistry
    {
    public:
        ResourceRegistry() = default;

        ResourceRegistry(const ResourceRegistry&) = delete;
        ResourceRegistry& operator=(const ResourceRegistry&) = delete;

        ResourceRegistry(ResourceRegistry&&) noexcept = default;
        ResourceRegistry& operator=(ResourceRegistry&&) noexcept = default;

        /**
         * @brief A row in the registry: a resource and its generation.
         * @detail Combined with the map key (ResourceID) it forms the full
         * identity of a unique resource instance. Exposed so tooling can
         * iterate all() without copying; never construct it directly.
         * @note Ownership: Owned by the registry. `resource` is the
         * unique_ptr that owns the Resource; callers hold borrowed pointers.
         */
        struct Entry
        {
            ResourceGeneration generation = 0;
            std::unique_ptr<Resource> resource;
        };

        template <typename T, typename... Args>
        ResourceHandle create(Args&&... args)
        {
            static_assert(
                std::is_base_of_v<Resource, T>,
                "T must derive from Resource"
            );

            if (!m_storage)
                m_storage = std::make_shared<Storage>();

            const ResourceID id = m_storage->nextID++;
            const ResourceGeneration generation = m_storage->nextGeneration++;

            auto resource =
                std::make_unique<T>(
                    std::forward<Args>(args)...
                );

            m_storage->resources.emplace(
                id,
                Entry{ generation, std::move(resource) }
            );

            return ResourceHandle(id, generation);
        }

        template <typename T>
        T* get(ResourceHandle handle)
        {
            if (!handle.valid() || !m_storage)
                return nullptr;

            const Entry* entry = findEntry(handle);

            return entry
                ? dynamic_cast<T*>(entry->resource.get())
                : nullptr;
        }

        template <typename T>
        const T* get(ResourceHandle handle) const
        {
            if (!handle.valid() || !m_storage)
                return nullptr;

            const Entry* entry = findEntry(handle);

            return entry
                ? dynamic_cast<const T*>(entry->resource.get())
                : nullptr;
        }

        /**
         * @brief Returns a weak, lifetime-checked borrow of a resource.
         * @detail The returned view holds a weak reference to the storage and
         * revalidates the handle against it on every access, so it safely
         * outlives this registry: it merely reports invalid/empty afterwards.
         * @param handle A handle issued by this registry.
         * @return A ResourceView<T> that is valid() while the resource exists.
         */
        template <typename T>
        ResourceView<T> view(ResourceHandle handle) const
        {
            return ResourceView<T>(m_storage, handle);
        }

        bool contains(ResourceHandle handle) const;

        void remove(ResourceHandle handle);

        void clear();

        std::size_t size() const;

        bool empty() const;

        [[nodiscard]]
        const std::unordered_map<ResourceID, Entry>& all() const
        {
            static const std::unordered_map<ResourceID, Entry> kEmpty;
            return m_storage ? m_storage->resources : kEmpty;
        }

    private:
        friend class Container;

        template <typename T>
        friend class ResourceView;

        struct Storage
        {
            std::unordered_map<ResourceID, Entry> resources;
            ResourceID nextID = 1;
            ResourceGeneration nextGeneration = 1;
        };

        [[nodiscard]]
        const Entry* findEntry(ResourceHandle handle) const
        {
            auto it = m_storage->resources.find(handle.id());

            if (it == m_storage->resources.end())
                return nullptr;

            if (it->second.generation != handle.generation())
                return nullptr;

            return &it->second;
        }

        std::shared_ptr<Storage> m_storage =
            std::make_shared<Storage>();
    };

    /**
     * @brief Safe, non-owning borrow of a Resource that may outlive the owner.
     * @detail Holds a weak reference to the owning registry's storage plus
     * the original handle. Every get()/valid() revalidates against the
     * storage, so a removed resource, a cleared registry or a destroyed
     * Container immediately shows up as invalid and get() returns nullptr.
     * @note Ownership: Non-owning. Keeps the resource alive only while the
     * registry does; it never owns the Resource itself.
     * @example
     * @code
     * ResourceView<Mesh> mesh = world.importResource<Mesh>(exporter, h);
     * if (const Mesh* m = mesh.get()) draw(*m); // null after exporter dies
     * @endcode
     */
    template <typename T>
    class ResourceView
    {
    public:
        static_assert(
            std::is_base_of_v<Resource, T>,
            "T must derive from Resource"
        );

        ResourceView() = default;

        [[nodiscard]]
        ResourceHandle handle() const
        {
            return m_handle;
        }

        [[nodiscard]]
        bool valid() const
        {
            if (!m_handle.valid())
                return false;

            auto owner = m_owner.lock();

            if (!owner)
                return false;

            auto it = owner->resources.find(m_handle.id());

            return it != owner->resources.end()
                && it->second.generation == m_handle.generation();
        }

        [[nodiscard]]
        bool expired() const
        {
            return m_owner.expired();
        }

        T* get()
        {
            auto owner = m_owner.lock();

            if (!owner)
                return nullptr;

            auto it = owner->resources.find(m_handle.id());

            if (it == owner->resources.end())
                return nullptr;

            if (it->second.generation != m_handle.generation())
                return nullptr;

            return dynamic_cast<T*>(it->second.resource.get());
        }

        const T* get() const
        {
            auto owner = m_owner.lock();

            if (!owner)
                return nullptr;

            auto it = owner->resources.find(m_handle.id());

            if (it == owner->resources.end())
                return nullptr;

            if (it->second.generation != m_handle.generation())
                return nullptr;

            return dynamic_cast<const T*>(it->second.resource.get());
        }

        explicit operator bool() const
        {
            return get() != nullptr;
        }

    private:
        friend class ResourceRegistry;
        friend class Container;

        ResourceView(
            std::weak_ptr<ResourceRegistry::Storage> owner,
            ResourceHandle handle
        )
            : m_owner(std::move(owner)), m_handle(handle)
        {
        }

        std::weak_ptr<ResourceRegistry::Storage> m_owner;
        ResourceHandle m_handle;
    };
}