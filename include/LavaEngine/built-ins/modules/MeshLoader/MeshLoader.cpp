#include "MeshLoader.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LavaEngine/Container.hpp"
#include "imgui.h"
#include "tiny_obj_loader.h"

namespace LavaEngine
{
    namespace
    {
        constexpr float kNormalEpsilon = 1.0e-12f;

        struct VertexKey
        {
            int vertex = -1;
            int normal = -1;
            int texcoord = -1;

            bool operator==(const VertexKey&) const = default;
        };

        struct VertexKeyHash
        {
            std::size_t operator()(const VertexKey& key) const noexcept
            {
                std::size_t seed = 0;

                const auto mix = [&seed](int value)
                {
                    seed ^= std::hash<int>{}(value)
                        + 0x9e3779b9u
                        + (seed << 6)
                        + (seed >> 2);
                };

                mix(key.vertex);
                mix(key.normal);
                mix(key.texcoord);

                return seed;
            }
        };

        glm::vec3 readVec3(
            const std::vector<tinyobj::real_t>& values,
            int index
        )
        {
            const std::size_t base =
                static_cast<std::size_t>(index) * 3;

            if (index < 0 || base + 2 >= values.size())
                throw std::runtime_error("OBJ references an invalid vector");

            return {
                values[base],
                values[base + 1],
                values[base + 2]
            };
        }

        glm::vec2 readVec2(
            const std::vector<tinyobj::real_t>& values,
            int index
        )
        {
            const std::size_t base =
                static_cast<std::size_t>(index) * 2;

            if (index < 0 || base + 1 >= values.size())
                throw std::runtime_error("OBJ references an invalid texcoord");

            return {
                values[base],
                values[base + 1]
            };
        }

        glm::vec3 safeNormalize(const glm::vec3& value)
        {
            const float lengthSquared = glm::dot(value, value);

            if (lengthSquared <= kNormalEpsilon)
                return glm::vec3(0.0f);

            return value / std::sqrt(lengthSquared);
        }
    }

    MeshLoader::Result MeshLoader::load(
        const std::string& filename,
        const Options& options
    )
    {
        Result result;

        try
        {
            Mesh mesh;

            if (!loadFromFile(
                filename,
                mesh,
                options,
                &result.warning,
                &result.error
            ))
            {
                ++m_stats.failed;
                m_stats.lastFile = filename;
                m_stats.lastError = result.error;
                return result;
            }

            result.handle =
                getContainer().createResource<Mesh>(std::move(mesh));

            ++m_stats.loaded;
            m_stats.lastFile = filename;
            m_stats.lastError.clear();
        }
        catch (const std::exception& exception)
        {
            result.error = exception.what();

            ++m_stats.failed;
            m_stats.lastFile = filename;
            m_stats.lastError = result.error;
        }

        return result;
    }

    bool MeshLoader::loadFromFile(
        const std::string& filename,
        Mesh& mesh,
        const Options& options,
        std::string* warning,
        std::string* error
    )
    {
        if (warning)
            warning->clear();

        if (error)
            error->clear();

        try
        {
            tinyobj::ObjReader reader;
            tinyobj::ObjReaderConfig config;

            config.triangulate = options.triangulate;
            config.vertex_color = false;
            config.mtl_search_path = options.mtlSearchPath;

            if (!reader.ParseFromFile(filename, config))
            {
                const std::string message = reader.Error().empty()
                    ? "Failed to load '" + filename + "'"
                    : reader.Error();

                if (error)
                    *error = message;

                if (warning)
                    *warning = reader.Warning();

                return false;
            }

            if (warning)
                *warning = reader.Warning();

            const tinyobj::attrib_t& attrib = reader.GetAttrib();
            const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

            std::vector<Vertex> vertices;
            std::vector<VertexIndex> indices;
            std::unordered_map<VertexKey, VertexIndex, VertexKeyHash> unique;

            bool missingNormals = attrib.normals.empty();

            const auto addVertex = [&](const tinyobj::index_t& source)
            {
                const VertexKey key{
                    source.vertex_index,
                    source.normal_index,
                    source.texcoord_index
                };

                const auto existing = unique.find(key);

                if (existing != unique.end())
                    return existing->second;

                if (vertices.size() >= std::numeric_limits<VertexIndex>::max())
                    throw std::length_error("Mesh has too many unique vertices");

                Vertex vertex;

                vertex.position = readVec3(attrib.vertices, source.vertex_index);

                if (source.normal_index >= 0)
                    vertex.normal = safeNormalize(
                        readVec3(attrib.normals, source.normal_index)
                    );
                else
                    missingNormals = true;

                if (source.texcoord_index >= 0)
                {
                    vertex.texcoord =
                        readVec2(attrib.texcoords, source.texcoord_index);

                    if (options.flipTexcoordV)
                        vertex.texcoord.y = 1.0f - vertex.texcoord.y;
                }

                const auto index = static_cast<VertexIndex>(vertices.size());

                vertices.push_back(vertex);
                unique.emplace(key, index);

                return index;
            };

            for (const tinyobj::shape_t& shape : shapes)
            {
                const tinyobj::mesh_t& objMesh = shape.mesh;

                std::size_t offset = 0;

                for (unsigned int faceVertices : objMesh.num_face_vertices)
                {
                    if (offset + faceVertices > objMesh.indices.size())
                        throw std::runtime_error(
                            "OBJ shape has an invalid face index range"
                        );

                    if (faceVertices < 3)
                    {
                        offset += faceVertices;
                        continue;
                    }

                    for (unsigned int i = 1; i + 1 < faceVertices; ++i)
                    {
                        indices.push_back(addVertex(objMesh.indices[offset]));
                        indices.push_back(addVertex(objMesh.indices[offset + i]));
                        indices.push_back(
                            addVertex(objMesh.indices[offset + i + 1])
                        );
                    }

                    offset += faceVertices;
                }
            }

            if (indices.empty())
            {
                if (error)
                    *error = "'" + filename + "' contains no triangle geometry";

                return false;
            }

            if (options.generateNormals && missingNormals)
            {
                std::vector<glm::vec3> accumulated(
                    vertices.size(),
                    glm::vec3(0.0f)
                );

                for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    const VertexIndex a = indices[i];
                    const VertexIndex b = indices[i + 1];
                    const VertexIndex c = indices[i + 2];

                    const glm::vec3 faceNormal = glm::cross(
                        vertices[b].position - vertices[a].position,
                        vertices[c].position - vertices[a].position
                    );

                    accumulated[a] += faceNormal;
                    accumulated[b] += faceNormal;
                    accumulated[c] += faceNormal;
                }

                for (std::size_t i = 0; i < vertices.size(); ++i)
                {
                    if (glm::dot(vertices[i].normal, vertices[i].normal)
                        <= kNormalEpsilon)
                    {
                        vertices[i].normal = safeNormalize(accumulated[i]);
                    }
                }
            }

            Mesh parsed;
            parsed.setVertices(vertices);
            parsed.setIndices(std::move(indices));

            mesh = std::move(parsed);

            return true;
        }
        catch (const std::exception& exception)
        {
            if (error)
                *error = exception.what();

            return false;
        }
    }

    LavaVK::VertexLayout MeshLoader::vertexLayout()
    {
        constexpr LavaVK::Format vec3Format(
            LavaVK::ChannelOrder::RGB,
            LavaVK::BitDepth::B32,
            LavaVK::NumericType::Float
        );

        constexpr LavaVK::Format vec2Format(
            LavaVK::ChannelOrder::RG,
            LavaVK::BitDepth::B32,
            LavaVK::NumericType::Float
        );

        return LavaVK::VertexLayout::create<Vertex>()
            .attribute<&Vertex::position>(0, vec3Format)
            .attribute<&Vertex::normal>(1, vec3Format)
            .attribute<&Vertex::texcoord>(2, vec2Format);
    }

    void MeshLoader::imgui() const
    {
        ImGui::Text("Loaded meshes: %zu", m_stats.loaded);
        ImGui::Text("Failed loads: %zu", m_stats.failed);

        if (!m_stats.lastFile.empty())
            ImGui::Text("Last file: %s", m_stats.lastFile.c_str());

        if (!m_stats.lastError.empty())
        {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "Error: %s",
                m_stats.lastError.c_str()
            );
        }
    }
}
