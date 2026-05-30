#pragma once

#include <core/scripting/scriptproviders.h>

#include <array>

namespace Fooyin::VimMotions {

class VimHandler;

class VimMarkVariableProvider : public ScriptVariableProvider
{
public:
    explicit VimMarkVariableProvider(VimHandler* handler = nullptr);

    void setHandler(VimHandler* handler);

    [[nodiscard]] std::span<const ScriptVariableDescriptor> variables() const override;

private:
    static ScriptResult localMarkVariable(const ScriptContext& context, const QString& name);
    static ScriptResult globalMarkVariable(const ScriptContext& context, const QString& name);

    inline static VimHandler* s_handler{nullptr};
    std::array<ScriptVariableDescriptor, 2> m_variables;
};

} // namespace Fooyin::VimMotions
