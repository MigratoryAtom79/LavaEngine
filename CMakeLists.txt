cmake_minimum_required(VERSION 3.25)

project(LavaEngine
        VERSION 0.11.0
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
add_subdirectory(external/glfw-3.5.1)
set(IMGUI_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
)

# LavaVK

add_library(LavaVK SHARED
        LavaVK/src/Instance.cpp
        LavaVK/src/Device.cpp
        LavaVK/src/Queue.cpp
        LavaVK/src/Sync.cpp
        LavaVK/src/Pipeline.cpp
        LavaVK/src/SwapChain.cpp
        LavaVK/src/Buffer.cpp
        LavaVK/src/Command.cpp
        LavaVK/src/Shader.cpp
        LavaVK/src/Descriptor.cpp
        LavaVK/src/Texture.cpp
)

target_include_directories(LavaVK
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/LavaVK/include

        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/LavaVK/external
)

target_link_libraries(LavaVK
        PUBLIC
        Vulkan::Vulkan
        glfw
)

# LavaEngine

add_library(LavaEngine SHARED
        include/LavaEngine/Application.hpp

        include/LavaEngine/Window.hpp
        src/Window.cpp

        include/LavaEngine/Container.hpp
        src/Container.cpp

        include/LavaEngine/Module.hpp
        src/Module.cpp

        include/LavaEngine/Resource.hpp
        src/Resource.cpp

        include/LavaEngine/Scheduler.hpp
        src/Scheduler.cpp

        include/LavaEngine/JobMarket.hpp
        src/JobMarket.cpp

        include/LavaEngine/WorkerPool.hpp
        src/WorkerPool.cpp

        include/LavaEngine/TaskGroup.hpp
        src/TaskGroup.cpp

        include/LavaEngine/EventBus.hpp

        include/LavaEngine/Framework.hpp

        include/LavaEngine/Frameworks.hpp

        include/LavaEngine/Logger.hpp
        src/Logger.cpp

        include/LavaEngine/InputHandler.hpp
        src/InputHandler.cpp

        include/LavaEngine/Interface.hpp

        include/LavaEngine/built-ins/modules/GraphicalInterface/GraphicalInterface.cpp
        include/LavaEngine/built-ins/modules/GraphicalInterface/GraphicalInterface.h

        include/LavaEngine/built-ins/modules/MeshLoader/MeshLoader.cpp
        include/LavaEngine/built-ins/modules/MeshLoader/MeshLoader.h

        include/LavaEngine/built-ins/containers/MeshManager/MeshManager.cpp
        include/LavaEngine/built-ins/containers/MeshManager/MeshManager.h

        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp


        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp

        external/tinyobjloader/tiny_obj_loader.cpp
        external/tinyobjloader/tiny_obj_loader.h
        include/LavaEngine/built-ins/resource/Mesh/Mesh.cpp
        include/LavaEngine/built-ins/resource/Mesh/Mesh.h
)

target_include_directories(LavaEngine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IMGUI_DIR}

        PRIVATE
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyobjloader
)

target_link_libraries(LavaEngine
        PUBLIC
        LavaVK
        Threads::Threads
)

add_library(LavaEngine::LavaEngine ALIAS LavaEngine)

# lavac

add_subdirectory(lavac)

# Tests

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Shader compiler

find_program(GLSLC_EXECUTABLE glslc REQUIRED)