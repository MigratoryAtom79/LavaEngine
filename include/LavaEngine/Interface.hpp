#pragma once

namespace LavaEngine
{
    class Container;

    /**
     * @brief Base class for a typed behavior interface exposed by a Container.
     * @detail A Container owns at most one Interface. Implementations derive
     * from a specific interface type and access their owning Container through
     * owner(); they do not own resources or the Container.
     * @note Ownership: Owned by the Container that added it. owner() is a
     * non-owning back-pointer kept valid across Container moves and cleared
     * when the Interface is removed. Interfaces do not survive Container
     * destruction or Application::unloadGame().
     * @example
     * @code
     * struct IWorldEditor : Interface {
     *     virtual void spawn() = 0;
     * };
     * class WorldEditor : public IWorldEditor {
     * public:
     *     void spawn() override { owner().createResource<Mesh>(); }
     * };
     * world.addInterface<IWorldEditor, WorldEditor>();
     * auto* editor = world.getInterface<IWorldEditor>();
     * @endcode
     */
    class Interface
    {
    public:
        virtual ~Interface() = default;

        [[nodiscard]] Container& owner() const
        {
            return *m_owner;
        }

        /**
         * @brief Draws optional Inspector controls for this interface.
         * @note Override to expose interface-specific debugging UI.
         */
        virtual void imgui() const {}

    protected:
        Interface() = default;

    private:
        friend class Container;

        void setOwner(Container* owner)
        {
            m_owner = owner;
        }

        Container* m_owner = nullptr;
    };
}
