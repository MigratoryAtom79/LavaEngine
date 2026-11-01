#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Module.hpp"

namespace LavaEngine
{
    class Container;

    /**
     * @brief Application-owned registry and synchronously dispatching event bus.
     * @detail A Container requests a new, named event from the Application
     * (Application::createEvent<Event>(name)) and is then the *only* Container
     * that may subscribe to it (EventBus::subscribe / Container::subscribe).
     * emit<Event>(payload) dispatches the payload to that single subscriber in
     * the same order it was registered. emit(handle) is a type-erased path that
     * default-constructs the payload, used by tooling such as Inspector debug
     * buttons that cannot name the event's type at compile time; events()
     * exposes the live registry so tooling can enumerate and trigger events.
     * @note Ownership: Owned by `Application` (by value). The bus stores a
     * type-erased copy of the subscribed handler; the handler borrows whatever
     * it captures and must not reference objects that die before the
     * Subscription is destroyed. The returned `Subscription` is a RAII handle —
     * keeping it as a Module member unsubscribes automatically when the
     * module/Container is torn down. Exactly one Container may subscribe to an
     * event; re-subscribing requires releasing the current subscription first.
     * Event registrations are session-scoped: Application::unloadGame() clears
     * the bus, so handles and Subscriptions do not survive a hot reload.
     * @example
     * @code
     * app.createEvent<PlayerDied>("PlayerDied");          // register (request)
     * auto sub = ui.subscribe<PlayerDied>(                // single subscriber
     *     [&](const PlayerDied& e) { scoreboard.remove(e.id); });
     * app.eventBus().emit(PlayerDied{42});                // notify
     * @endcode
     */
    class EventBus
    {
    public:
        using EventHandle = std::uint64_t;

        /**
         * @brief RAII handle to an event subscription; unsubscribes on destruction.
         * @detail Move-only. Destruction, explicit unsubscribe(), or move-
         * assignment releases the subscriber slot so another subscribe() is
         * allowed. A moved-from or already-unsubscribed handle is inert and its
         * destruction is a no-op.
         */
        class Subscription
        {
        public:
            Subscription() = default;

            ~Subscription()
            {
                unsubscribe();
            }

            Subscription(const Subscription&) = delete;
            Subscription& operator=(const Subscription&) = delete;

            Subscription(Subscription&& other) noexcept
                : m_bus(other.m_bus),
                  m_handle(other.m_handle)
            {
                other.m_bus = nullptr;
                other.m_handle = 0;
            }

            Subscription& operator=(Subscription&& other) noexcept
            {
                if (this != &other)
                {
                    unsubscribe();

                    m_bus = other.m_bus;
                    m_handle = other.m_handle;

                    other.m_bus = nullptr;
                    other.m_handle = 0;
                }

                return *this;
            }

            /**
             * @brief Releases the subscriber slot now. Idempotent.
             */
            void unsubscribe()
            {
                if (m_bus)
                {
                    m_bus->releaseSubscription(m_handle);
                    m_bus = nullptr;
                    m_handle = 0;
                }
            }

            /**
             * @return True while this handle still owns its slot. Note that
             * EventBus::clear() invalidates every registration but leaves
             * handles inert; unsubscribe() stays a safe no-op either way.
             */
            [[nodiscard]] bool valid() const
            {
                return m_bus != nullptr;
            }

        private:
            friend class EventBus;

            Subscription(EventBus* bus, EventHandle handle)
                : m_bus(bus),
                  m_handle(handle)
            {
            }

            EventBus* m_bus = nullptr;
            EventHandle m_handle = 0;
        };

        /**
         * @brief One registered event, for tooling enumeration.
         */
        struct RegisteredEvent
        {
            EventHandle handle;
            std::string name;
        };

        /**
         * @brief Registers a new, named event of type Event.
         * @param name Human-readable label shown by tooling (e.g. Inspector).
         * @return The event's stable handle, used by emit(handle).
         * @note The event is keyed by typeID<Event>(), so this is the standard
         * step a container takes to "request a new event" from the application.
         * The type-erased emit(handle) default-constructs a payload, so
         * inspectable events must be default-constructible.
         * @throws std::runtime_error if an event of type Event already exists.
         */
        template <typename Event>
        EventHandle createEvent(const std::string& name);

        /**
         * @brief Subscribes the single Container that may listen for Event.
         * @param container The subscribing Container.
         * @param handler Callable invoked with `const Event&` on every emit.
         * @return The Subscription RAII handle owning the slot.
         * @throws std::runtime_error on an unregistered event, or when the
         * event already has a subscriber (exactly one is allowed).
         */
        template <typename Event, typename Handler>
        Subscription subscribe(Container& container, Handler handler);

        /**
         * @brief Fire-and-forget dispatch of payload to the event's subscriber.
         * @note A registered-but-unsubscribed emit is a no-op, as is emitting a
         * type with no registered event — nothing is being notified.
         */
        template <typename Event>
        void emit(const Event& payload);

        /**
         * @brief Type-erased emit for tooling: default-constructs the payload
         * and dispatches it to the subscriber.
         * @param handle A handle from createEvent()/events().
         * @note No-op for an unknown handle or an unsubscribed event.
         */
        void emit(EventHandle handle)
        {
            const auto it = m_slots.find(handle);

            if (it == m_slots.end())
                return;

            if (it->second.trigger)
                it->second.trigger();
        }

        /**
         * @brief Read-only view of every registered event, in creation order.
         */
        [[nodiscard]]
        const std::vector<RegisteredEvent>& events() const
        {
            return m_registered;
        }

        /**
         * @brief The Container currently subscribed to the event, or nullptr.
         * @param handle A handle from createEvent()/events().
         * @note Useful for tooling (e.g. the Inspector) to show which
         * Container owns a subscription.
         */
        [[nodiscard]]
        const Container* subscriber(EventHandle handle) const
        {
            const auto it = m_slots.find(handle);

            return (it == m_slots.end())
                ? nullptr
                : it->second.subscriber;
        }

        /**
         * @brief Removes every event and subscription. Idempotent.
         * @note Outstanding Subscription handles are left inert; their
         * unsubscribe() (or destruction) is a safe no-op afterwards.
         */
        void clear()
        {
            m_slots.clear();
            m_handleOfType.clear();
            m_registered.clear();
        }

    private:
        void releaseSubscription(EventHandle handle)
        {
            const auto it = m_slots.find(handle);

            if (it == m_slots.end())
                return;

            it->second.subscriber = nullptr;
            it->second.handler = {};
        }

        struct Slot
        {
            TypeID type;
            Container* subscriber = nullptr;
            std::function<void(const void*)> handler;
            std::function<void()> trigger;
        };

        std::unordered_map<EventHandle, Slot> m_slots;
        std::unordered_map<TypeID, EventHandle> m_handleOfType;
        std::vector<RegisteredEvent> m_registered;
        EventHandle m_nextHandle = 1;
    };

    template <typename Event>
    EventBus::EventHandle EventBus::createEvent(const std::string& name)
    {
        const TypeID type = typeID<Event>();

        if (m_handleOfType.contains(type))
        {
            throw std::runtime_error(
                "EventBus: event type already registered"
            );
        }

        const EventHandle handle = m_nextHandle++;

        Slot slot;
        slot.type = type;

        // Type-erased inspector emit: default-constructs the payload of
        // Event, known only here at registration time.
        slot.trigger = [this, handle]
        {
            const auto it = m_slots.find(handle);

            if (it == m_slots.end())
                return;

            if (!it->second.subscriber)
                return;

            Event payload{};
            it->second.handler(&payload);
        };

        m_slots.emplace(handle, std::move(slot));
        m_handleOfType.emplace(type, handle);
        m_registered.push_back(RegisteredEvent{handle, name});

        return handle;
    }

    template <typename Event, typename Handler>
    EventBus::Subscription EventBus::subscribe(
        Container& container,
        Handler handler
    )
    {
        const TypeID type = typeID<Event>();

        const auto typeIt = m_handleOfType.find(type);

        if (typeIt == m_handleOfType.end())
        {
            throw std::runtime_error(
                "EventBus: event type not registered; create it first"
            );
        }

        const EventHandle handle = typeIt->second;

        Slot& slot = m_slots.at(handle);

        if (slot.subscriber)
        {
            throw std::runtime_error(
                "EventBus: only one container may subscribe to an event"
            );
        }

        slot.subscriber = &container;
        slot.handler = [handler](const void* payload)
        {
            handler(*static_cast<const Event*>(payload));
        };

        return Subscription(this, handle);
    }

    template <typename Event>
    void EventBus::emit(const Event& payload)
    {
        const TypeID type = typeID<Event>();

        const auto typeIt = m_handleOfType.find(type);

        if (typeIt == m_handleOfType.end())
            return;

        const Slot& slot = m_slots.at(typeIt->second);

        if (!slot.subscriber)
            return;

        slot.handler(&payload);
    }
}