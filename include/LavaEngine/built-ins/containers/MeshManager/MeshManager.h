#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "LavaEngine/Container.hpp"
#include "LavaEngine/built-ins/modules/MeshLoader/MeshLoader.h"

namespace LavaEngine
{
    /**
     * @brief Container that owns imported meshes and the MeshLoader module
     * used to import them.
     * @detail Adds a MeshLoader to itself on construction and keeps a
     * name-to-resource map on top of the Container's ResourceRegistry so
     * meshes can be looked up without carrying ResourceHandles around.
     * Loaded meshes are exported so other Containers can import them.
     * Loading a name that is already registered is rejected instead of
     * silently replacing the existing mesh.
     * @note Ownership: Owns its Mesh resources and MeshLoader through the
     * Container's registries. Returned Mesh pointers are borrowed and are
     * invalidated when the container is destroyed or the resource is removed.
     * @example
     * @code
     * auto& meshes = app.createContainer<MeshManager>("meshes");
     * auto result = meshes.loadMesh("models/teapot.obj");
     * if (result)
     *     draw(*meshes.getMesh("teapot"));
     * @endcode
     */
    class MeshManager : public Container
    {
    public:
        explicit MeshManager(std::string name = "MeshManager");

        ~MeshManager() override = default;

        MeshManager(const MeshManager&) = delete;
        MeshManager& operator=(const MeshManager&) = delete;

        MeshManager(MeshManager&&) noexcept = default;
        MeshManager& operator=(MeshManager&&) noexcept = default;

        /**
         * @brief The MeshLoader module owned by this container.
         * @throws std::logic_error If the module was removed.
         */
        MeshLoader& loader();

        /**
         * @brief The MeshLoader module owned by this container.
         * @throws std::logic_error If the module was removed.
         */
        [[nodiscard]]
        const MeshLoader& loader() const;

        /**
         * @brief Loads a mesh using its file stem as the lookup name.
         * @param filename Path to the .obj file.
         * @return The loader result; on success it also holds the mesh handle.
         */
        MeshLoader::Result loadMesh(const std::string& filename);

        /**
         * @brief Loads a mesh using its file stem as the lookup name.
         * @param filename Path to the .obj file.
         * @param options Import options.
         * @return The loader result; on success it also holds the mesh handle.
         */
        MeshLoader::Result loadMesh(
            const std::string& filename,
            const MeshLoader::Options& options
        );

        MeshLoader::Result loadMesh(
            const std::string& filename,
            const std::string& meshName
        )
        {
            return loadMesh(
                filename,
                meshName,
                MeshLoader::Options{}
            );
        }

        /**
         * @brief Loads a mesh and registers it under an explicit name.
         * @param filename Path to the .obj file.
         * @param meshName Name used by getMesh()/meshHandle().
         * @param options Import options.
         * @return The loader result; on success it also holds the mesh handle.
         */
        MeshLoader::Result loadMesh(
            const std::string& filename,
            const std::string& meshName,
            const MeshLoader::Options& options
        );

        [[nodiscard]]
        bool hasMesh(const std::string& name) const;

        [[nodiscard]]
        Mesh* getMesh(const std::string& name);

        [[nodiscard]]
        const Mesh* getMesh(const std::string& name) const;

        [[nodiscard]]
        ResourceHandle meshHandle(const std::string& name) const;

        [[nodiscard]]
        const std::unordered_map<std::string, ResourceHandle>& meshes() const
        {
            return m_meshes;
        }

        [[nodiscard]]
        std::size_t meshCount() const
        {
            return m_meshes.size();
        }

        /**
         * @brief Lists the loaded meshes and their sizes in the Inspector.
         */
        void imgui() const override;

    private:
        static std::string nameFromFilename(const std::string& filename);

        std::unordered_map<std::string, ResourceHandle> m_meshes;
    };
}
