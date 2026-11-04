#pragma once

#include <cstddef>
#include <string>

#include "../../../../../external/glm/glm/glm.hpp"

#include "LavaEngine/Module.hpp"
#include "LavaEngine/built-ins/resource/Mesh/Mesh.h"
#include "LavaVK/Buffer.hpp"

namespace LavaEngine
{
    /**
     * @brief Module that imports Wavefront .obj files as Mesh resources.
     * @detail Parses one or more shapes from an .obj file with
     * tinyobjloader, deduplicates (position, normal, texcoord) tuples into a
     * single indexed vertex buffer and stores the result as a Mesh resource
     * owned by the module's Container. Missing normals are generated from
     * the triangle faces; missing texture coordinates default to zero.
     * @note Ownership: Owned by a Container via its ModuleRegistry. The
     * created Mesh resources are owned by the same Container, not by this
     * module.
     * @example
     * @code
     * auto& loader = world.addModule<MeshLoader>();
     * auto result = loader.load("models/teapot.obj");
     * if (result)
     *     Mesh* mesh = world.getResource<Mesh>(result.handle);
     * @endcode
     */
    class MeshLoader : public Module
    {
    public:
        /**
         * @brief Vertex layout produced by every loaded mesh.
         */
        struct Vertex
        {
            glm::vec3 position{0.0f};
            glm::vec3 normal{0.0f};
            glm::vec2 texcoord{0.0f};
        };

        /**
         * @brief Controls how an .obj file is imported.
         */
        struct Options
        {
            bool triangulate = true;
            bool generateNormals = true;
            bool flipTexcoordV = false;
            std::string mtlSearchPath;
        };

        /**
         * @brief Outcome of a load request.
         * @detail `handle` is valid on success. `warning` may be populated on
         * success and `error` on failure.
         */
        struct Result
        {
            ResourceHandle handle;
            std::string warning;
            std::string error;

            [[nodiscard]]
            bool ok() const
            {
                return handle.valid();
            }

            explicit operator bool() const
            {
                return ok();
            }
        };

        /**
         * @brief Loads an .obj file into a new Mesh resource in the owner Container.
         * @param filename Path to the .obj file.
         * @param options Import options.
         * @return A Result carrying the new resource handle, or an error string.
         */
        Result load(const std::string& filename)
        {
            return load(filename, Options{});
        }

        Result load(
            const std::string& filename,
            const Options& options
        );

        /**
         * @brief Parses an .obj file into an existing Mesh without creating a resource.
         * @param filename Path to the .obj file.
         * @param mesh Mesh to overwrite on success.
         * @param options Import options.
         * @param warning Optional output for non-fatal parser warnings.
         * @param error Optional output for a failure reason.
         * @return True on success, false if parsing failed (mesh is untouched).
         */
        static bool loadFromFile(
            const std::string& filename,
            Mesh& mesh
        )
        {
            return loadFromFile(filename, mesh, Options{}, nullptr, nullptr);
        }

        static bool loadFromFile(
            const std::string& filename,
            Mesh& mesh,
            const Options& options,
            std::string* warning = nullptr,
            std::string* error = nullptr
        );

        /**
         * @brief Vertex layout matching MeshLoader::Vertex.
         */
        [[nodiscard]]
        static LavaVK::VertexLayout vertexLayout();

        /**
         * @brief Running totals shown by imgui().
         */
        struct Stats
        {
            std::size_t loaded = 0;
            std::size_t failed = 0;
            std::string lastFile;
            std::string lastError;
        };

        [[nodiscard]]
        const Stats& stats() const
        {
            return m_stats;
        }

        void imgui() const override;

    private:
        Stats m_stats;
    };
}
