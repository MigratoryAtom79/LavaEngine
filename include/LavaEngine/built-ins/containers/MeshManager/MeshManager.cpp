#include "MeshManager.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "imgui.h"

namespace LavaEngine
{
    MeshManager::MeshManager(std::string name)
        : Container(std::move(name))
    {
        addModule<MeshLoader>();
    }

    MeshLoader& MeshManager::loader()
    {
        MeshLoader* meshLoader = getModule<MeshLoader>();

        if (!meshLoader)
            throw std::logic_error("MeshManager has no MeshLoader module");

        return *meshLoader;
    }

    const MeshLoader& MeshManager::loader() const
    {
        const MeshLoader* meshLoader = modules().get<MeshLoader>();

        if (!meshLoader)
            throw std::logic_error("MeshManager has no MeshLoader module");

        return *meshLoader;
    }

    MeshLoader::Result MeshManager::loadMesh(const std::string& filename)
    {
        return loadMesh(filename, MeshLoader::Options{});
    }

    MeshLoader::Result MeshManager::loadMesh(
        const std::string& filename,
        const MeshLoader::Options& options
    )
    {
        return loadMesh(filename, nameFromFilename(filename), options);
    }

    MeshLoader::Result MeshManager::loadMesh(
        const std::string& filename,
        const std::string& meshName,
        const MeshLoader::Options& options
    )
    {
        const std::string name = meshName.empty()
            ? nameFromFilename(filename)
            : meshName;

        MeshLoader::Result result;

        if (m_meshes.contains(name))
        {
            result.error = "Mesh '" + name + "' is already loaded";
            return result;
        }

        result = loader().load(filename, options);

        if (result.ok())
        {
            exportResource<Mesh>(result.handle);
            m_meshes.emplace(name, result.handle);
        }

        return result;
    }

    bool MeshManager::hasMesh(const std::string& name) const
    {
        return m_meshes.contains(name);
    }

    Mesh* MeshManager::getMesh(const std::string& name)
    {
        const ResourceHandle handle = meshHandle(name);

        return handle.valid()
            ? getResource<Mesh>(handle)
            : nullptr;
    }

    const Mesh* MeshManager::getMesh(const std::string& name) const
    {
        const ResourceHandle handle = meshHandle(name);

        return handle.valid()
            ? getResource<Mesh>(handle)
            : nullptr;
    }

    ResourceHandle MeshManager::meshHandle(const std::string& name) const
    {
        const auto it = m_meshes.find(name);

        return it != m_meshes.end()
            ? it->second
            : ResourceHandle{};
    }

    void MeshManager::imgui() const
    {
        ImGui::Text("Meshes: %zu", m_meshes.size());

        for (const auto& [name, handle] : m_meshes)
        {
            const Mesh* mesh = getResource<Mesh>(handle);

            if (!mesh)
            {
                ImGui::BulletText("%s: missing", name.c_str());
                continue;
            }

            ImGui::BulletText(
                "%s: %zu vertices, %zu indices",
                name.c_str(),
                mesh->vertexCount(),
                mesh->indexCount()
            );
        }
    }

    std::string MeshManager::nameFromFilename(const std::string& filename)
    {
        std::string name =
            std::filesystem::path(filename).stem().string();

        if (name.empty())
            name = "mesh";

        return name;
    }
}
