#include "../include/LavaEngine/Container.hpp"

namespace LavaEngine
{
    Container::Container(std::string name)
        : m_name(std::move(name))
    {
        m_modules.setContainer(this);
    }

    Container::Container(Container&& other) noexcept
        : m_name(std::move(other.m_name)),
          m_modules(std::move(other.m_modules)),
          m_resources(std::move(other.m_resources)),
          m_eventBus(other.m_eventBus),
          m_variables(std::move(other.m_variables)),
          m_interface(std::move(other.m_interface))
    {
        other.m_eventBus = nullptr;
        m_modules.setContainer(this);

        if (m_interface)
            m_interface->setOwner(this);
    }


    Container& Container::operator=(Container&& other) noexcept
    {
        if (this == &other)
            return *this;

        removeInterface();

        m_name = std::move(other.m_name);
        m_modules = std::move(other.m_modules);
        m_resources = std::move(other.m_resources);
        m_variables = std::move(other.m_variables);
        m_interface = std::move(other.m_interface);

        m_eventBus = other.m_eventBus;
        other.m_eventBus = nullptr;

        m_modules.setContainer(this);

        if (m_interface)
            m_interface->setOwner(this);

        return *this;
    }

    const std::string& Container::name() const
    {
        return m_name;
    }

    void Container::expose(const std::string& name, float* variable)
    {
        auto box = std::make_unique<ExposedBox>();
        box->floating = *variable;

        m_variables.push_back(
            Variable{name, &box->floating, VarType::Float}
        );

        m_boxes.push_back(std::move(box));
    }

    void Container::expose(const std::string& name, int* variable)
    {
        auto box = std::make_unique<ExposedBox>();
        box->integer = *variable;

        m_variables.push_back(
            Variable{name, &box->integer, VarType::Int}
        );

        m_boxes.push_back(std::move(box));
    }

    void Container::expose(const std::string& name, bool* variable)
    {
        auto box = std::make_unique<ExposedBox>();
        box->boolean = *variable;

        m_variables.push_back(
            Variable{name, &box->boolean, VarType::Bool}
        );

        m_boxes.push_back(std::move(box));
    }

    void Container::expose(const std::string& name, std::string* variable)
    {
        auto box = std::make_unique<ExposedBox>();
        box->stringValue = *variable;

        m_variables.push_back(
            Variable{name, &box->stringValue, VarType::String}
        );

        m_boxes.push_back(std::move(box));
    }

    void Container::expose(const std::string& name, double* variable)
    {
        auto box = std::make_unique<ExposedBox>();
        box->doublePrec = *variable;

        m_variables.push_back(
            Variable{name, &box->doublePrec, VarType::Double}
        );

        m_boxes.push_back(std::move(box));
    }
}
