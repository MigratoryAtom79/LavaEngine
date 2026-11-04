#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "LavaEngine/Resource.hpp"
#include "LavaVK/Buffer.hpp"

namespace LavaEngine
{
    using VertexIndex = std::uint32_t;

    /**
     * @brief General purpose mesh holding raw vertex data, optional indices
     * and, once uploaded, the matching GPU buffers.
     * @detail Vertex data is stored as an untyped byte blob plus a stride, so
     * any trivially copyable vertex struct can be used without the mesh
     * knowing its layout. Indices are normalised to 32-bit and may be omitted
     * for non-indexed draws. upload() creates/refills the vertex and index
     * buffers; any CPU-side mutation invalidates them until the next upload.
     * @note Ownership: Owns its CPU vertex/index storage and, when uploaded,
     * its GPU buffers. Buffers keep a non-owning reference to the Device used
     * at upload time, so that Device must outlive the mesh or releaseGpu().
     * @example
     * @code
     * struct Vertex { glm::vec3 position; glm::vec2 uv; };
     *
     * Mesh mesh;
     * mesh.setVertices(vertices);
     * mesh.setIndices(indices);
     * mesh.upload(device);
     * cmd.bindVertexBuffer(*mesh.vertexBuffer());
     * @endcode
     */
    class Mesh : public Resource
    {
    public:
        Mesh() = default;
        ~Mesh() override = default;

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        Mesh(Mesh&&) noexcept = default;
        Mesh& operator=(Mesh&&) noexcept = default;

        /**
         * @brief Replaces the vertex data with a typed vector.
         * @param vertices Trivially copyable vertices, copied byte-for-byte.
         */
        template <typename Vertex>
        void setVertices(const std::vector<Vertex>& vertices)
        {
            static_assert(
                std::is_trivially_copyable_v<Vertex>,
                "Vertex must be trivially copyable"
            );

            setVertices(
                vertices.data(),
                vertices.size(),
                sizeof(Vertex)
            );
        }

        /**
         * @brief Replaces the vertex data from a typed array.
         * @param vertices Pointer to the first vertex (may be null if count is 0).
         * @param count Number of vertices.
         */
        template <typename Vertex>
        void setVertices(const Vertex* vertices, std::size_t count)
        {
            static_assert(
                std::is_trivially_copyable_v<Vertex>,
                "Vertex must be trivially copyable"
            );

            setVertices(vertices, count, sizeof(Vertex));
        }

        void setVertices(
            const void* vertices,
            std::size_t count,
            std::size_t stride
        );

        /**
         * @brief Replaces the indices with a typed vector.
         * @param indices Any integral index type, widened to VertexIndex.
         */
        template <typename Index>
        void setIndices(const std::vector<Index>& indices)
        {
            static_assert(
                std::is_integral_v<Index>,
                "Index must be an integral type"
            );
            static_assert(
                sizeof(Index) <= sizeof(VertexIndex),
                "Index type must not be wider than VertexIndex"
            );

            setIndices(
                indices.data(),
                indices.size()
            );
        }

        /**
         * @brief Replaces the indices from a typed array.
         * @param indices Pointer to the first index (may be null if count is 0).
         * @param count Number of indices.
         */
        template <typename Index>
        void setIndices(const Index* indices, std::size_t count)
        {
            static_assert(
                std::is_integral_v<Index>,
                "Index must be an integral type"
            );
            static_assert(
                sizeof(Index) <= sizeof(VertexIndex),
                "Index type must not be wider than VertexIndex"
            );

            if (count > 0 && !indices)
                throw std::invalid_argument(
                    "Mesh index data must not be null"
                );

            m_indices.clear();
            m_indices.reserve(count);

            for (std::size_t i = 0; i < count; ++i)
                m_indices.push_back(static_cast<VertexIndex>(indices[i]));

            invalidateGpu();
        }

        void setIndices(std::vector<VertexIndex>&& indices);

        void setIndices(std::span<const VertexIndex> indices);

        /**
         * @brief Clears all CPU data and releases the GPU buffers.
         */
        void clear();

        [[nodiscard]]
        const std::byte* vertexData() const
        {
            return m_vertices.data();
        }

        [[nodiscard]]
        std::span<const std::byte> vertexBytes() const
        {
            return m_vertices;
        }

        [[nodiscard]]
        std::size_t vertexByteSize() const
        {
            return m_vertices.size();
        }

        [[nodiscard]]
        std::size_t vertexStride() const
        {
            return m_vertexStride;
        }

        [[nodiscard]]
        std::size_t vertexCount() const
        {
            return m_vertexStride == 0
                ? 0
                : m_vertices.size() / m_vertexStride;
        }

        [[nodiscard]]
        std::span<const VertexIndex> indices() const
        {
            return m_indices;
        }

        [[nodiscard]]
        std::size_t indexCount() const
        {
            return m_indices.size();
        }

        [[nodiscard]]
        bool hasIndices() const
        {
            return !m_indices.empty();
        }

        /**
         * @brief Number of elements submitted to a draw call.
         * @return indexCount() when indexed, vertexCount() otherwise.
         */
        [[nodiscard]]
        std::size_t drawCount() const
        {
            return hasIndices() ? indexCount() : vertexCount();
        }

        [[nodiscard]]
        std::size_t triangleCount() const
        {
            return drawCount() / 3;
        }

        [[nodiscard]]
        bool empty() const
        {
            return m_vertices.empty();
        }

        /**
         * @brief Creates the vertex and index buffers and uploads the CPU data.
         * @param device Device used to allocate the buffers.
         * @return False if there is no vertex data; true on success.
         * @throw std::runtime_error If buffer creation or upload fails.
         */
        bool upload(LavaVK::Device& device);

        /**
         * @brief Destroys any uploaded GPU buffers.
         */
        void releaseGpu();

        [[nodiscard]]
        bool isUploaded() const
        {
            return m_vertexBuffer.has_value();
        }

        [[nodiscard]]
        const LavaVK::Buffer* vertexBuffer() const
        {
            return m_vertexBuffer
                ? &*m_vertexBuffer
                : nullptr;
        }

        [[nodiscard]]
        const LavaVK::Buffer* indexBuffer() const
        {
            return m_indexBuffer
                ? &*m_indexBuffer
                : nullptr;
        }

    private:
        void invalidateGpu()
        {
            releaseGpu();
        }

        std::vector<std::byte> m_vertices;
        std::vector<VertexIndex> m_indices;

        std::size_t m_vertexStride = 0;

        std::optional<LavaVK::Buffer> m_vertexBuffer;
        std::optional<LavaVK::Buffer> m_indexBuffer;
    };
}
