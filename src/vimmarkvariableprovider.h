#pragma once

#include <core/scripting/scriptdefs.h>

#include <array>

namespace Fooyin::VimMotions {

class VimHandler;

class VimMarkVariableProvider
{
public:
    explicit VimMarkVariableProvider(const VimHandler* handler);
    VimMarkVariableProvider(const VimMarkVariableProvider&)            = delete;
    VimMarkVariableProvider& operator=(const VimMarkVariableProvider&) = delete;

    [[nodiscard]] std::span<const ScriptVariableDescriptor> variables() const;

private:
    ScriptResult localMarkVariable(const ScriptContext& context) const;
    ScriptResult globalMarkVariable(const ScriptContext& context) const;

    const VimHandler* m_handler;
    std::array<ScriptVariableDescriptor, 2> m_variables;
};

} // namespace Fooyin::VimMotions
