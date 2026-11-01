#pragma once

#include <chrono>
#include <dlfcn.h>
#include <functional>
#include <iostream>
#include <thread>
#include <utility>

#include "LavaEngine/Application.hpp"

using EngineFunction = void (*)(LavaEngine::Application&);

/**
 * @brief Hosts a dynamically loaded game (.so) and the Application that runs it.
 * @detail Loads a game library, resolves its `lavaEngineMain` entry point,
 * runs/steps it, and supports hot reload when the library is rebuilt.
 * @note Ownership: UserGame owns the `Application` (m_engine, by value),
 * the dlopen'd library handle, and the runtime copy of the game file. On
 * reload/unload it tears the Application session down (unloadGame) before
 * dlclosing the library. Borrowers of the Application can use setReloadHooks()
 * to release references before teardown and re-resolve them after the new
 * session loads; the Inspector uses this to detach and rebind automatically.
 * @example
 * @code
 * UserGame game("MyGame.so");
 * game.load();
 * while (!game.step()) {}
 * game.unload();
 * @endcode
 */
class UserGame
{
public:
    UserGame(
        const std::string& gameLibName
    ):
          m_libraryPath("./" + gameLibName)
    {
        loadLibrary();
    }

    ~UserGame()
    {
        unload();
    }

    /**
     * @brief Installs callbacks around session reload and teardown.
     * @param before Called while the current framework is still alive.
     * @param after Called after the replacement library has loaded.
     * @note Single observer; the Inspector uses this to detach and rebind.
     */
    void setReloadHooks(
        std::function<void()> before,
        std::function<void()> after
    )
    {
        m_beforeReload = std::move(before);
        m_afterReload = std::move(after);
    }

    void clearReloadHooks()
    {
        m_beforeReload = nullptr;
        m_afterReload = nullptr;
    }

    void loadLibrary()
    {
        namespace fs = std::filesystem;

        if (!fs::exists(m_libraryPath))
        {
            throw std::runtime_error(
                "Game library does not exist: " + m_libraryPath.string()
            );
        }

        if (!waitForStableFile(m_libraryPath))
        {
            throw std::runtime_error(
                "Timed out waiting for game library to finish writing: "
                + m_libraryPath.string()
            );
        }

        fs::path runtimePath =
            "./.lavac_game_" + std::to_string(m_generation++) + ".so";

        fs::copy_file(m_libraryPath, runtimePath, fs::copy_options::overwrite_existing);

        m_runtimePath = runtimePath;

        m_library = dlopen(
            m_runtimePath.c_str(),
            RTLD_NOW | RTLD_LOCAL
        );

        if (!m_library)
        {
            throw std::runtime_error(dlerror());
        }

        m_main =
            reinterpret_cast<EngineFunction>(
                dlsym(
                    m_library,
                    "lavaEngineMain"
                )
            );

        if (!m_main)
        {
            dlclose(m_library);
            m_library = nullptr;

            throw std::runtime_error(
                "Failed to find game function"
            );
        }

        m_lastWriteTime =
            fs::last_write_time(m_libraryPath);
    }

    void load()
    {
        m_main(m_engine);
    }

    void reload()
    {
        std::cerr << "[LAVAC] Reloading...\n";

        if (m_beforeReload)
            m_beforeReload();

        m_engine.unloadGame();

        dlclose(m_library);

        m_library = nullptr;
        m_main = nullptr;

        loadLibrary();

        load();

        if (m_afterReload)
            m_afterReload();

        m_lastWriteTime =
            std::filesystem::last_write_time(m_libraryPath);

        std::cerr << "[LAVAC] Reload complete.\n";
    }

    static bool waitForStableFile(
        const std::filesystem::path& path,
        std::chrono::milliseconds timeout = std::chrono::seconds(3)
    )
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        const auto deadline = std::chrono::steady_clock::now() + timeout;

        // Wait for the file to exist at all first.
        while (!fs::exists(path, ec))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        auto lastSize = fs::file_size(path, ec);
        if (ec) lastSize = static_cast<std::uintmax_t>(-1); // force at least one more comparison

        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            const auto size = fs::file_size(path, ec);
            if (ec)
            {
                lastSize = static_cast<std::uintmax_t>(-1);
                continue;
            } // file vanished/relocked — keep waiting

            if (size == lastSize && size > 0)
                return true;

            lastSize = size;
        }

        return false;
    }

    bool changed() const
    {
        std::error_code ec;

        const auto current =
            std::filesystem::last_write_time(
                m_libraryPath,
                ec
            );

        if (ec)
        {
            // Library is currently being rebuilt.
            return false;
        }

        return current != m_lastWriteTime;
    }

    void run()
    {
        while (true)
        {
            const auto frameStart =
                std::chrono::steady_clock::now();

            if (step())
                break;

            m_engine.waitForNextFrame(frameStart);
        }
    }

    int step()
    {
        if (changed())
        {
            std::cerr << "[LAVAC] Changes detected.\n";
            if (!waitForStableFile(m_libraryPath))   // let any remaining build steps finish
            {
                return 0;
            }
            try { reload(); }
            catch (const std::exception& e) { std::cerr << "[LAVAC] Reload failed: " << e.what() << '\n'; }
        }
        return m_engine.step();
    }

    void unload()
    {
        if (!m_library)
            return;

        if (m_beforeReload)
            m_beforeReload();

        m_engine.unloadGame();

        dlclose(m_library);

        m_library = nullptr;
        m_main = nullptr;
    }

    [[nodiscard]] LavaEngine::Application& application()
    {
        return m_engine;
    }

private:
    LavaEngine::Application m_engine;

    std::function<void()> m_beforeReload;
    std::function<void()> m_afterReload;

    void* m_library = nullptr;

    EngineFunction m_main = nullptr;

    std::filesystem::path m_libraryPath;
    std::filesystem::path m_runtimePath;

    std::filesystem::file_time_type m_lastWriteTime;

    uint64_t m_generation = 0;
};
