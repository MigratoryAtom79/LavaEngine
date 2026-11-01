#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "../include/LavaEngine/Application.hpp"

using namespace LavaEngine;

namespace
{
    struct TestContext
    {
        int checks = 0;
        int failures = 0;
    };

    TestContext* g_currentTest = nullptr;

#define CHECK(cond)                                                        \
    do                                                                     \
    {                                                                      \
        ++g_currentTest->checks;                                           \
        if (!(cond))                                                       \
        {                                                                  \
            ++g_currentTest->failures;                                     \
            std::fprintf(                                                  \
                stderr,                                                    \
                "      FAIL %s:%d: %s\n",                                  \
                __FILE__,                                                   \
                __LINE__,                                                   \
                #cond                                                       \
            );                                                             \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do                                                                     \
    {                                                                      \
        ++g_currentTest->checks;                                           \
        const auto& _a = (a);                                              \
        const auto& _b = (b);                                              \
        if (!(_a == _b))                                                   \
        {                                                                  \
            ++g_currentTest->failures;                                     \
            std::fprintf(                                                  \
                stderr,                                                    \
                "      FAIL %s:%d: %s == %s\n",                            \
                __FILE__,                                                   \
                __LINE__,                                                   \
                #a,                                                         \
                #b                                                          \
            );                                                             \
        }                                                                  \
    } while (0)

#define CHECK_NE(a, b)                                                     \
    do                                                                     \
    {                                                                      \
        ++g_currentTest->checks;                                           \
        const auto& _a = (a);                                              \
        const auto& _b = (b);                                              \
        if (!(_a != _b))                                                   \
        {                                                                  \
            ++g_currentTest->failures;                                     \
            std::fprintf(                                                  \
                stderr,                                                    \
                "      FAIL %s:%d: %s != %s\n",                            \
                __FILE__,                                                   \
                __LINE__,                                                   \
                #a,                                                         \
                #b                                                          \
            );                                                             \
        }                                                                  \
    } while (0)

    struct Mesh : Resource
    {
    };

    struct RecorderModule : Module
    {
        void imgui() const override {}

        void onUnload() override
        {
            g_order.push_back("module.onUnload");
        }

        ~RecorderModule() override
        {
            g_order.push_back("module.dtor");
        }

        inline static std::vector<std::string> g_order;
    };

    struct TestInterface : Interface
    {
        virtual int value() const = 0;
    };

    struct TestInterfaceImpl : TestInterface
    {
        explicit TestInterfaceImpl(int initial)
            : m_value(initial)
        {
        }

        int value() const override
        {
            return m_value;
        }

        void imgui() const override
        {
            ++guiCalls;
        }

        int m_value;
        mutable int guiCalls = 0;
    };

    struct LifecycleContainer : Container
    {
        void imgui() const override {}

        void onUnload() override
        {
            RecorderModule::g_order.push_back("container.onUnload");
            Container::onUnload();
        }

        ~LifecycleContainer() override
        {
            RecorderModule::g_order.push_back("container.dtor");
        }
    };

    std::size_t positionOf(const std::string& event)
    {
        const auto it =
            std::find(
                RecorderModule::g_order.begin(),
                RecorderModule::g_order.end(),
                event
            );

        if (it == RecorderModule::g_order.end())
            return static_cast<std::size_t>(-1);

        return static_cast<std::size_t>(
            std::distance(
                RecorderModule::g_order.begin(),
                it
            )
        );
    }

    void testGenerationHandles()
    {
        ResourceRegistry reg;

        ResourceHandle h1 = reg.create<Mesh>();
        ResourceHandle h2 = reg.create<Mesh>();

        CHECK(h1.valid());
        CHECK(h2.valid());
        CHECK_NE(h1, h2);
        CHECK(reg.contains(h1));
        CHECK(reg.contains(h2));
        CHECK_EQ(reg.size(), std::size_t{2});

        reg.remove(h1);

        CHECK(!reg.contains(h1));
        CHECK(reg.get<Mesh>(h1) == nullptr);
        CHECK(reg.contains(h2));
        CHECK_EQ(reg.size(), std::size_t{1});

        // Recreating at the same slot must not revive the stale handle.
        ResourceHandle h3 = reg.create<Mesh>();

        CHECK(h3.valid());
        CHECK_NE(h3, h1);
        CHECK(!reg.contains(h1));
        CHECK(reg.get<Mesh>(h1) == nullptr);
        CHECK(reg.contains(h3));
        CHECK(reg.get<Mesh>(h3) != nullptr);
        CHECK_EQ(reg.size(), std::size_t{2});

        ResourceHandle invalid;

        CHECK(!invalid.valid());
        CHECK(!reg.contains(invalid));
        CHECK(reg.get<Mesh>(invalid) == nullptr);

        reg.clear();

        CHECK(reg.empty());
        CHECK_EQ(reg.size(), std::size_t{0});
        CHECK(reg.get<Mesh>(h2) == nullptr);
        CHECK(reg.get<Mesh>(h3) == nullptr);
    }

    void testResourceViewLifetime()
    {
        ResourceView<Mesh> view;

        {
            ResourceRegistry reg;
            ResourceHandle h = reg.create<Mesh>();

            view = reg.view<Mesh>(h);

            CHECK(view.valid());
            CHECK(view.get() != nullptr);
            CHECK(static_cast<bool>(view));
            CHECK(!view.expired());

            reg.remove(h);

            CHECK(!view.valid());
            CHECK(view.get() == nullptr);
            CHECK(!static_cast<bool>(view));
        }

        CHECK(view.expired());
        CHECK(view.get() == nullptr);
    }

    void testViewSurvivesRegistryMove()
    {
        ResourceRegistry reg;
        ResourceHandle h = reg.create<Mesh>();

        ResourceView<Mesh> view = reg.view<Mesh>(h);

        CHECK(view.valid());
        CHECK(view.get() != nullptr);

        ResourceRegistry moved = std::move(reg);

        CHECK(moved.contains(h));
        CHECK(view.valid());
        CHECK(view.get() != nullptr);

        ResourceRegistry target;
        target = std::move(moved);

        CHECK(target.contains(h));
        CHECK(view.valid());
        CHECK(view.get() != nullptr);
    }

    void testAllIteration()
    {
        ResourceRegistry reg;

        ResourceHandle h1 = reg.create<Mesh>();
        ResourceHandle h2 = reg.create<Mesh>();

        std::size_t count = 0;

        for (const auto& [id, entry] : reg.all())
        {
            ++count;

            CHECK(
                id == h1.id() ||
                id == h2.id()
            );

            CHECK(entry.resource != nullptr);

            if (id == h1.id())
                CHECK_EQ(entry.generation, h1.generation());

            if (id == h2.id())
                CHECK_EQ(entry.generation, h2.generation());
        }

        CHECK_EQ(count, std::size_t{2});
    }

    void testContainerImportResource()
    {
        Container exporter("exporter");
        Container importer("importer");

        ResourceHandle h = exporter.createResource<Mesh>();

        exporter.exportResource<Mesh>(h);

        ResourceView<Mesh> view =
            importer.importResource<Mesh>(exporter, h);

        CHECK(view.valid());
        CHECK(view.get() != nullptr);
        CHECK(!view.expired());

        // A non-exported resource cannot be imported.
        Container other("other");
        ResourceHandle h2 = other.createResource<Mesh>();

        ResourceView<Mesh> view2 =
            importer.importResource<Mesh>(other, h2);

        CHECK(!view2.valid());
        CHECK(view2.get() == nullptr);

        // Destroying the exporter must invalidate imported views.
        {
            Container dying("dying");
            ResourceHandle hd = dying.createResource<Mesh>();

            dying.exportResource<Mesh>(hd);

            view = importer.importResource<Mesh>(dying, hd);

            CHECK(view.valid());
            CHECK(view.get() != nullptr);
        }

        CHECK(!view.valid());
        CHECK(view.get() == nullptr);
        CHECK(view.expired());
    }

    void testContainerMoveRepointsModules()
    {
        Container a("a");

        RecorderModule& module =
            a.addModule<RecorderModule>();

        CHECK(&module.getContainer() == &a);
        CHECK(a.hasModule<RecorderModule>());

        Container b = std::move(a);

        CHECK(&module.getContainer() == &b);
        CHECK(b.hasModule<RecorderModule>());
        CHECK(b.getModule<RecorderModule>() == &module);

        Container c("c");

        c = std::move(b);

        CHECK(&module.getContainer() == &c);
        CHECK(c.hasModule<RecorderModule>());
        CHECK(c.getModule<RecorderModule>() == &module);
    }

    void testContainerInterface()
    {
        Container c("c");

        CHECK(c.interface() == nullptr);
        CHECK(!c.hasInterface<TestInterface>());
        CHECK(c.getInterface<TestInterface>() == nullptr);

        TestInterface& interface =
            c.addInterface<TestInterface, TestInterfaceImpl>(7);

        CHECK_EQ(interface.value(), 7);
        CHECK(&interface.owner() == &c);
        CHECK(c.interface() == &interface);
        CHECK(c.hasInterface<TestInterface>());
        CHECK(c.hasInterface<TestInterfaceImpl>());
        CHECK(c.getInterface<TestInterface>() == &interface);
        CHECK(c.getInterface<TestInterfaceImpl>() == &interface);

        const Container& constContainer = c;
        CHECK(constContainer.getInterface<TestInterface>() == &interface);
        CHECK(constContainer.interface() == &interface);

        TestInterfaceImpl& implementation =
            static_cast<TestInterfaceImpl&>(interface);
        const Interface& base = implementation;
        base.imgui();
        CHECK_EQ(implementation.guiCalls, 1);

        bool threw = false;

        try
        {
            c.addInterface<TestInterface, TestInterfaceImpl>(9);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);
        CHECK_EQ(interface.value(), 7);

        c.removeInterface();

        CHECK(c.interface() == nullptr);
        CHECK(!c.hasInterface<TestInterface>());
    }

    void testContainerMoveRepointsInterface()
    {
        Container a("a");

        TestInterface& interface =
            a.addInterface<TestInterface, TestInterfaceImpl>(11);

        CHECK(&interface.owner() == &a);

        Container b = std::move(a);

        CHECK(&interface.owner() == &b);
        CHECK(b.getInterface<TestInterface>() == &interface);
        CHECK(a.interface() == nullptr);

        Container c("c");

        c = std::move(b);

        CHECK(&interface.owner() == &c);
        CHECK(c.getInterface<TestInterface>() == &interface);
        CHECK(b.interface() == nullptr);
    }

    void testExposedBoxes()
    {
        Container c("c");

        bool callerBool = true;
        int callerInt = 7;
        float callerFloat = 1.5f;
        double callerDouble = 2.5;
        std::string callerString = "original";

        c.expose("flag", &callerBool);
        c.expose("count", &callerInt);
        c.expose("speed", &callerFloat);
        c.expose("scale", &callerDouble);
        c.expose("title", &callerString);

        CHECK_EQ(c.variables().size(), std::size_t{5});

        Variable* flag = nullptr;
        Variable* count = nullptr;
        Variable* speed = nullptr;
        Variable* scale = nullptr;
        Variable* title = nullptr;

        for (Variable& variable : c.variables())
        {
            if (variable.name == "flag")
                flag = &variable;
            else if (variable.name == "count")
                count = &variable;
            else if (variable.name == "speed")
                speed = &variable;
            else if (variable.name == "scale")
                scale = &variable;
            else if (variable.name == "title")
                title = &variable;
        }

        CHECK(flag != nullptr);
        CHECK(count != nullptr);
        CHECK(speed != nullptr);
        CHECK(scale != nullptr);
        CHECK(title != nullptr);

        // Simulate Inspector edits.
        *static_cast<bool*>(flag->ptr) = false;
        *static_cast<int*>(count->ptr) = 42;
        *static_cast<float*>(speed->ptr) = 99.0f;
        *static_cast<double*>(scale->ptr) = 123.0;
        *static_cast<std::string*>(title->ptr) = "edited";

        // Exposed variables are boxed copies, not aliases to caller memory.
        CHECK(callerBool);
        CHECK_EQ(callerInt, 7);
        CHECK_EQ(callerFloat, 1.5f);
        CHECK_EQ(callerDouble, 2.5);
        CHECK_EQ(callerString, std::string{"original"});

        CHECK(!*static_cast<bool*>(flag->ptr));
        CHECK_EQ(*static_cast<int*>(count->ptr), 42);
        CHECK_EQ(*static_cast<float*>(speed->ptr), 99.0f);
        CHECK_EQ(*static_cast<double*>(scale->ptr), 123.0);
        CHECK_EQ(
            *static_cast<std::string*>(title->ptr),
            std::string{"edited"}
        );
    }

    void testTeardownHooksRunBeforeDestruction()
    {
        RecorderModule::g_order.clear();

        {
            Application app;

            LifecycleContainer& world =
                app.createContainer<LifecycleContainer>();

            world.addModule<RecorderModule>();

            app.unloadGame();
        }

        const std::size_t containerHook =
            positionOf("container.onUnload");

        const std::size_t moduleHook =
            positionOf("module.onUnload");

        const std::size_t moduleDtor =
            positionOf("module.dtor");

        const std::size_t containerDtor =
            positionOf("container.dtor");

        CHECK_EQ(containerHook, std::size_t{0});

        CHECK(moduleHook != static_cast<std::size_t>(-1));
        CHECK(moduleDtor != static_cast<std::size_t>(-1));
        CHECK(containerDtor != static_cast<std::size_t>(-1));

        CHECK(moduleHook < moduleDtor);
        CHECK(moduleHook < containerDtor);
    }

    void testDefaultContainerOnUnload()
    {
        RecorderModule::g_order.clear();

        Container c("plain");
        c.addModule<RecorderModule>();

        c.onUnload();

        CHECK(
            RecorderModule::g_order ==
            std::vector<std::string>{"module.onUnload"}
        );
    }

    struct IdTypeA : Module
    {
        void imgui() const override {}
    };

    struct IdTypeB : Module
    {
        void imgui() const override {}
    };

    void testSchedulerBasics()
    {
        Scheduler scheduler;

        scheduler.createJob([] { return 1; });
        scheduler.createJob([] { return 1; });

        CHECK_EQ(scheduler.jobCount(), std::size_t{2});
        CHECK_EQ(scheduler.completedJobCount(), std::size_t{0});
        CHECK(!scheduler.done());

        scheduler.execute();

        CHECK_EQ(scheduler.completedJobCount(), std::size_t{2});
        CHECK(scheduler.done());

        scheduler.execute();
        CHECK_EQ(scheduler.completedJobCount(), std::size_t{2});
    }

    void testSchedulerRepeatContract()
    {
        Scheduler scheduler;

        int runs = 0;

        scheduler.createJob(
            [&]
            {
                ++runs;
                return runs < 3 ? 0 : 1;
            }
        );

        scheduler.execute();
        CHECK_EQ(runs, 1);

        scheduler.execute();
        CHECK_EQ(runs, 2);

        scheduler.execute();
        CHECK_EQ(runs, 3);
        CHECK(scheduler.done());
    }

    void testSchedulerCancelContract()
    {
        Scheduler scheduler;

        int runs = 0;

        scheduler.createJob(
            [&]
            {
                ++runs;
                return -1;
            }
        );

        scheduler.execute();
        CHECK_EQ(runs, 1);

        scheduler.execute();
        CHECK_EQ(runs, 1);
        CHECK(scheduler.done());
    }

    void testSchedulerDependencies()
    {
        Scheduler scheduler;

        std::vector<int> order;

        JobID setup = scheduler.createJob(
            [&]
            {
                order.push_back(1);
                return 1;
            }
        );

        JobID worker = scheduler.createJob(
            [&]
            {
                order.push_back(2);
                return 1;
            }
        );

        scheduler.dependsOn(worker, setup);

        scheduler.execute();
        CHECK_EQ(order.size(), std::size_t{2});

        // The dependency id is stripped from every dependency list once the
        // dependency job is destroyed.
        scheduler.destroyJob(setup);
        CHECK_EQ(scheduler.jobCount(), std::size_t{1});

        scheduler.execute();
        scheduler.execute();
        CHECK_EQ(order.size(), std::size_t{2});
    }

    void testSchedulerSelfAndCycleRejected()
    {
        Scheduler scheduler;

        JobID a = scheduler.createJob([] { return 1; });
        JobID b = scheduler.createJob([] { return 1; });

        bool threw = false;

        try
        {
            scheduler.dependsOn(a, a);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);

        scheduler.dependsOn(a, b);

        threw = false;

        try
        {
            scheduler.dependsOn(b, a);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);

        threw = false;

        try
        {
            scheduler.dependsOn(a, 9999);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);
    }

    void testSchedulerReentrantCreate()
    {
        Scheduler scheduler;

        int runs = 0;
        int childRuns = 0;

        scheduler.createJob(
            [&]
            {
                ++runs;

                scheduler.createJob(
                    [&]
                    {
                        ++childRuns;
                        return 1;
                    }
                );

                return 1;
            }
        );

        scheduler.execute();

        // The child job was created re-entrantly and only materializes after
        // the pass; it must not run in the same pass.
        CHECK_EQ(runs, 1);
        CHECK_EQ(childRuns, 0);
        CHECK_EQ(scheduler.jobCount(), std::size_t{2});

        scheduler.execute();
        CHECK_EQ(childRuns, 1);
        CHECK(scheduler.done());
    }

    void testSchedulerReentrantDestroy()
    {
        Scheduler scheduler;

        JobID victim = 0;
        int victimRuns = 0;
        int victimExits = 0;
        int killerRuns = 0;

        // The killer is created first, so it runs first in the pass and
        // destroys a victim that has not run yet.
        JobID killer = scheduler.createJob(
            [&]
            {
                ++killerRuns;

                scheduler.destroyJob(victim);

                return 1;
            }
        );

        victim = scheduler.createJob(
            [&]
            {
                ++victimRuns;
                return 1;
            },
            [&]
            {
                ++victimExits;
            }
        );

        scheduler.execute();

        // The victim is destroyed re-entrantly mid-pass; it never runs, its
        // exit fires once, and it is removed from the scheduler.
        CHECK_EQ(killerRuns, 1);
        CHECK_EQ(victimRuns, 0);
        CHECK_EQ(victimExits, 1);
        CHECK_EQ(scheduler.jobCount(), std::size_t{1});
        CHECK(scheduler.done());

        scheduler.execute();
        CHECK_EQ(victimRuns, 0);
        CHECK_EQ(victimExits, 1);
    }

    void testSchedulerExitExactlyOnce()
    {
        Scheduler scheduler;

        int exits = 0;
        int runs = 0;

        JobID id = scheduler.createJob(
            [&]
            {
                ++runs;
                return 1;
            },
            [&]
            {
                ++exits;
            }
        );

        scheduler.execute();
        scheduler.execute();

        CHECK_EQ(runs, 1);
        CHECK_EQ(exits, 1);

        // Destroying a completed job must not run exit() a second time.
        scheduler.destroyJob(id);
        CHECK_EQ(exits, 1);
        CHECK_EQ(scheduler.jobCount(), std::size_t{0});
    }

    void testSchedulerExitAllDrainsCreatedDuringExit()
    {
        Scheduler scheduler;

        int exits = 0;

        scheduler.createJob(
            [] { return 0; },
            [&]
            {
                ++exits;

                // Re-entrantly create a fresh job; exitAll must drain it too.
                scheduler.createJob(
                    [] { return 0; },
                    [&] { ++exits; }
                );
            }
        );

        scheduler.exitAll();

        CHECK_EQ(exits, 2);
        CHECK_EQ(scheduler.jobCount(), std::size_t{0});
        CHECK(scheduler.done());
    }

    void testApplicationStepCompletesJob()
    {
        Application app;

        app.setFrameRateLimit(0.0);

        int runs = 0;

        app.getScheduler()->createJob(
            [&]
            {
                ++runs;
                return 1;
            }
        );

        CHECK_EQ(app.step(), 1);
        CHECK_EQ(runs, 1);

        CHECK_EQ(app.step(), 1);
        CHECK_EQ(runs, 1);
    }

    void testApplicationRunStopsOnRequestExit()
    {
        Application app;

        app.setFrameRateLimit(0.0);

        int runs = 0;

        app.getScheduler()->createJob(
            [&]
            {
                ++runs;

                if (runs == 3)
                    app.requestExit();

                return 0;
            }
        );

        app.run();

        CHECK_EQ(runs, 3);
        CHECK(app.exitRequested());

        app.unloadGame();
        CHECK(!app.exitRequested());
    }

    void testApplicationRunPicksUpJobsCreatedDuringExecute()
    {
        Application app;

        app.setFrameRateLimit(0.0);

        Scheduler* scheduler = app.getScheduler();

        int childRuns = 0;

        scheduler->createJob(
            [&]
            {
                scheduler->createJob(
                    [&]
                    {
                        ++childRuns;
                        return 1;
                    }
                );

                return 1;
            }
        );

        app.run();

        CHECK_EQ(childRuns, 1);
        CHECK(scheduler->done());
    }

    void testApplicationEmptySchedulerIsDone()
    {
        Application app;

        app.setFrameRateLimit(0.0);

        CHECK(app.getScheduler()->done());
        CHECK_EQ(app.step(), 1);

        app.run();

        CHECK(!app.exitRequested());
    }

    void testApplicationFrameRateLimit()
    {
        Application app;

        CHECK_EQ(app.frameRateLimit(), 60.0);

        app.setFrameRateLimit(144.0);
        CHECK_EQ(app.frameRateLimit(), 144.0);

        app.setFrameRateLimit(-1.0);
        CHECK_EQ(app.frameRateLimit(), 0.0);
    }

    void testTypeIdStability()
    {
        // Compile-time stable and deterministic: the same type always maps
        // to the same ID, and distinct types map to distinct IDs.
        static_assert(typeID<IdTypeA>() == typeID<IdTypeA>());
        static_assert(typeID<IdTypeB>() == typeID<IdTypeB>());
        static_assert(typeID<int>() == typeID<int>());

        CHECK_EQ(typeID<IdTypeA>(), typeID<IdTypeA>());
        CHECK_EQ(typeID<IdTypeB>(), typeID<IdTypeB>());
        CHECK_NE(typeID<IdTypeA>(), typeID<IdTypeB>());

        // The ID derives from the type's name, not from a typeid token, so
        // a hot-reloaded copy of the same source type still maps to the same
        // key. Building the registry with the name-derived ID feels like any
        // clean compile: no collision errors, adds/removes resolve.
        Container c("c");

        IdTypeA& a = c.addModule<IdTypeA>();

        CHECK(c.hasModule<IdTypeA>());
        CHECK(c.getModule<IdTypeA>() == &a);

        c.removeModule<IdTypeA>();

        CHECK(!c.hasModule<IdTypeA>());
    }

    struct TestEvent { int value = 0; };
    struct OtherEvent { int value = 0; };
    struct EventModuleProbe { bool fired = false; };

    struct EventSubscriberModule : Module
    {
        void imgui() const override {}

        EventBus::Subscription subscription;
    };

    void testEventBusRegisterSubscribeEmit()
    {
        Application app;

        const EventBus::EventHandle handle =
            app.createEvent<TestEvent>("TestEvent");

        CHECK_NE(
            handle,
            static_cast<EventBus::EventHandle>(0)
        );

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent& e) { received += e.value; }
        );

        CHECK(sub.valid());

        app.eventBus().emit(TestEvent{7});
        app.eventBus().emit(TestEvent{3});

        CHECK_EQ(received, 10);
    }

    void testEventBusRequiresRegistrationBeforeSubscribe()
    {
        Application app;

        auto& world = app.createContainer<Container>("world");

        bool threw = false;

        try
        {
            world.subscribe<TestEvent>([](const TestEvent&) {});
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);
    }

    void testEventBusSingleSubscriberOnly()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& a = app.createContainer<Container>("a");
        auto& b = app.createContainer<Container>("b");

        auto subA = a.subscribe<TestEvent>([](const TestEvent&) {});

        bool threw = false;

        try
        {
            auto subB = b.subscribe<TestEvent>([](const TestEvent&) {});
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);

        // Releasing the slot lets another container subscribe.
        subA.unsubscribe();
        CHECK(!subA.valid());

        auto subB = b.subscribe<TestEvent>([](const TestEvent&) {});
        CHECK(subB.valid());
    }

    void testEventBusUnsubscribeStopsDelivery()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent&) { ++received; }
        );

        app.eventBus().emit(TestEvent{0});
        CHECK_EQ(received, 1);

        sub.unsubscribe();

        app.eventBus().emit(TestEvent{0});
        CHECK_EQ(received, 1);
    }

    void testEventBusRAIIScopeEnd()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        {
            auto sub = world.subscribe<TestEvent>(
                [&](const TestEvent&) { ++received; }
            );

            app.eventBus().emit(TestEvent{0});
            CHECK_EQ(received, 1);
        }

        app.eventBus().emit(TestEvent{0});
        CHECK_EQ(received, 1);
    }

    void testEventBusMoveTransfersSubscription()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent&) { ++received; }
        );

        auto moved = std::move(sub);

        CHECK(!sub.valid());
        CHECK(moved.valid());

        app.eventBus().emit(TestEvent{0});
        CHECK_EQ(received, 1);

        moved.unsubscribe();

        app.eventBus().emit(TestEvent{0});
        CHECK_EQ(received, 1);
    }

    void testEventBusTypedIsolation()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");
        app.createEvent<OtherEvent>("OtherEvent");

        auto& world = app.createContainer<Container>("world");

        int testHits = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent&) { ++testHits; }
        );

        app.eventBus().emit(OtherEvent{99});
        CHECK_EQ(testHits, 0);

        app.eventBus().emit(TestEvent{1});
        CHECK_EQ(testHits, 1);
    }

    void testEventBusDuplicateRegistrationRejected()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        bool threw = false;

        try
        {
            app.createEvent<TestEvent>("TestEvent again");
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        CHECK(threw);
    }

    void testEventBusEmitBeforeSubscribeIsNoop()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        // Registered but unsubscribed -> nothing to notify, no error.
        app.eventBus().emit(TestEvent{5});

        // Emitting an unregistered type is also fire-and-forget.
        app.eventBus().emit(OtherEvent{5});
    }

    void testEventBusHandleEmitAndEnumeration()
    {
        Application app;

        app.createEvent<EventModuleProbe>("EventModuleProbe");

        const auto& events = app.eventBus().events();

        CHECK_EQ(events.size(), std::size_t{1});
        CHECK_EQ(events[0].name, std::string{"EventModuleProbe"});

        auto& world = app.createContainer<Container>("world");

        bool fired = false;

        auto sub = world.subscribe<EventModuleProbe>(
            [&](const EventModuleProbe&) { fired = true; }
        );

        // Inspector-style type-erased emit, default-constructing the payload.
        app.eventBus().emit(events[0].handle);
        CHECK(fired);
    }

    void testEventBusClearInvalidatesRegistrations()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent& e) { received += e.value; }
        );

        app.eventBus().clear();

        app.eventBus().emit(TestEvent{5});
        CHECK_EQ(received, 0);

        CHECK(app.eventBus().events().empty());

        // A surviving handle unsubscribes safely once the bus is cleared.
        sub.unsubscribe();
        CHECK(!sub.valid());
    }

    void testEventBusUnsubscribesOnContainerTeardown()
    {
        Application app;

        app.createEvent<EventModuleProbe>("EventModuleProbe");

        int received = 0;

        auto& world = app.createContainer<Container>("world");
        auto& module = world.addModule<EventSubscriberModule>();

        module.subscription =
            module.eventBus().subscribe<EventModuleProbe>(
                world,
                [&](const EventModuleProbe&) { ++received; }
            );

        app.eventBus().emit(EventModuleProbe{});
        CHECK_EQ(received, 1);

        // Teardown destroys the container, whose module's RAII Subscription
        // auto-unsubscribes, so a handler borrowing module state can no longer
        // fire afterwards.
        app.unloadGame();

        app.eventBus().emit(EventModuleProbe{});
        CHECK_EQ(received, 1);
    }

    void testApplicationUnloadClearsEventBus()
    {
        Application app;

        app.createEvent<TestEvent>("TestEvent");

        auto& world = app.createContainer<Container>("world");

        int received = 0;

        auto sub = world.subscribe<TestEvent>(
            [&](const TestEvent& e) { received += e.value; }
        );

        app.eventBus().emit(TestEvent{7});
        CHECK_EQ(received, 7);
        CHECK_EQ(app.eventBus().events().size(), std::size_t{1});

        // The event triggers live in the game library, so unloadGame() must
        // drop them before lavac calls dlclose().
        app.unloadGame();

        CHECK(app.eventBus().events().empty());

        app.eventBus().emit(TestEvent{3});
        CHECK_EQ(received, 7);

        sub.unsubscribe();
        CHECK(!sub.valid());
    }

    struct TestCase
    {
        const char* name;
        void (*function)();
    };

    constexpr TestCase tests[] =
    {
        {"Generation handles", testGenerationHandles},
        {"Resource view lifetime", testResourceViewLifetime},
        {"Views survive registry moves", testViewSurvivesRegistryMove},
        {"Resource iteration", testAllIteration},
        {"Container resource import", testContainerImportResource},
        {"Container module move", testContainerMoveRepointsModules},
        {"Container interface", testContainerInterface},
        {"Container interface move", testContainerMoveRepointsInterface},
        {"Exposed variables", testExposedBoxes},
        {"Teardown ordering", testTeardownHooksRunBeforeDestruction},
        {"Default container unload", testDefaultContainerOnUnload},
        {"Type ID stability", testTypeIdStability},
        {"Scheduler basics", testSchedulerBasics},
        {"Scheduler repeat contract", testSchedulerRepeatContract},
        {"Scheduler cancel contract", testSchedulerCancelContract},
        {"Scheduler dependencies", testSchedulerDependencies},
        {"Scheduler rejects self/cycle deps", testSchedulerSelfAndCycleRejected},
        {"Scheduler re-entrant create", testSchedulerReentrantCreate},
        {"Scheduler re-entrant destroy", testSchedulerReentrantDestroy},
        {"Scheduler exit exactly once", testSchedulerExitExactlyOnce},
        {"Scheduler exitAll drains", testSchedulerExitAllDrainsCreatedDuringExit},
        {"Application step completes job", testApplicationStepCompletesJob},
        {"Application run requestExit", testApplicationRunStopsOnRequestExit},
        {"Application run picks up new jobs", testApplicationRunPicksUpJobsCreatedDuringExecute},
        {"Application empty scheduler", testApplicationEmptySchedulerIsDone},
        {"Application frame rate limit", testApplicationFrameRateLimit},
        {"EventBus register/subscribe/emit", testEventBusRegisterSubscribeEmit},
        {"EventBus requires registration", testEventBusRequiresRegistrationBeforeSubscribe},
        {"EventBus single subscriber", testEventBusSingleSubscriberOnly},
        {"EventBus unsubscribe stops delivery", testEventBusUnsubscribeStopsDelivery},
        {"EventBus RAII scope end", testEventBusRAIIScopeEnd},
        {"EventBus move transfers subscription", testEventBusMoveTransfersSubscription},
        {"EventBus typed isolation", testEventBusTypedIsolation},
        {"EventBus duplicate registration", testEventBusDuplicateRegistrationRejected},
        {"EventBus emit-before-subscribe noop", testEventBusEmitBeforeSubscribeIsNoop},
        {"EventBus handle emit + enumeration", testEventBusHandleEmitAndEnumeration},
        {"EventBus clear invalidates", testEventBusClearInvalidatesRegistrations},
        {"EventBus teardown unsubscribes", testEventBusUnsubscribesOnContainerTeardown},
        {"Application unload clears EventBus", testApplicationUnloadClearsEventBus},
    };

    void printSeparator()
    {
        std::printf(
            "------------------------------------------------------------\n"
        );
    }
}

int main()
{
    const auto suiteStart =
        std::chrono::steady_clock::now();

    int passed = 0;
    int failed = 0;
    int totalChecks = 0;
    int totalFailures = 0;

    std::printf(
        "\n"
        "LavaEngine Unit Tests\n"
    );

    printSeparator();

    for (const TestCase& test : tests)
    {
        TestContext context;
        g_currentTest = &context;

        const auto start =
            std::chrono::steady_clock::now();

        bool threw = false;

        try
        {
            test.function();
        }
        catch (const std::exception& e)
        {
            threw = true;
            ++context.failures;

            std::fprintf(
                stderr,
                "      EXCEPTION: %s\n",
                e.what()
            );
        }
        catch (...)
        {
            threw = true;
            ++context.failures;

            std::fprintf(
                stderr,
                "      EXCEPTION: unknown exception\n"
            );
        }

        const auto end =
            std::chrono::steady_clock::now();

        const double milliseconds =
            std::chrono::duration<double, std::milli>(
                end - start
            ).count();

        totalChecks += context.checks;
        totalFailures += context.failures;

        if (context.failures == 0 && !threw)
        {
            ++passed;

            std::printf(
                "  [PASS] %-32s %4d checks  %.3f ms\n",
                test.name,
                context.checks,
                milliseconds
            );
        }
        else
        {
            ++failed;

            std::printf(
                "  [FAIL] %-32s %4d checks  %.3f ms\n",
                test.name,
                context.checks,
                milliseconds
            );
        }
    }

    g_currentTest = nullptr;

    const auto suiteEnd =
        std::chrono::steady_clock::now();

    const double totalMilliseconds =
        std::chrono::duration<double, std::milli>(
            suiteEnd - suiteStart
        ).count();

    printSeparator();

    std::printf(
        "Tests:   %d passed, %d failed, %zu total\n",
        passed,
        failed,
        sizeof(tests) / sizeof(tests[0])
    );

    std::printf(
        "Checks:  %d executed, %d failed\n",
        totalChecks,
        totalFailures
    );

    std::printf(
        "Time:    %.3f ms\n",
        totalMilliseconds
    );

    if (failed == 0)
    {
        std::printf(
            "\n"
            "RESULT: PASS\n"
        );

        return 0;
    }

    std::printf(
        "\n"
        "RESULT: FAIL\n"
    );

    return 1;
}
