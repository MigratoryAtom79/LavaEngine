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
          m_commands(std::move(other.m_commands)),
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
        m_commands = std::move(other.m_commands);
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

    void Container::expose(const std::string& name, float* variable, bool live)
    {
        m_variables.push_back(
            Variable{name, variable, VarType::Float, live}
        );
    }

    void Container::expose(const std::string& name, int* variable, bool live)
    {
        m_variables.push_back(
            Variable{name, variable, VarType::Int, live}
        );
    }

    void Container::expose(const std::string& name, bool* variable, bool live)
    {
        m_variables.push_back(
            Variable{name, variable, VarType::Bool, live}
        );
    }

    void Container::expose(const std::string& name, std::string* variable, bool live)
    {
        m_variables.push_back(
            Variable{name, variable, VarType::String, live}
        );
    }

    void Container::expose(const std::string& name, double* variable, bool live)
    {
        m_variables.push_back(
            Variable{name, variable, VarType::Double, live}
        );
    }

    void Container::unexpose(const std::string& name)
    {
        for (std::size_t i = 0; i < m_variables.size(); ++i)
        {
            if (m_variables[i].name != name)
                continue;

            m_variables.erase(m_variables.begin() + i);
            return;
        }
    }

    void Container::addCommand(const std::string& name, std::function<void()> action)
    {
        for (Command& command : m_commands)
        {
            if (command.name != name)
                continue;

            command.action = std::move(action);
            return;
        }

        m_commands.push_back(Command{name, std::move(action)});
    }

    void Container::removeCommand(const std::string& name)
    {
        for (std::size_t i = 0; i < m_commands.size(); ++i)
        {
            if (m_commands[i].name != name)
                continue;

            m_commands.erase(m_commands.begin() + i);
            return;
        }
    }
}
