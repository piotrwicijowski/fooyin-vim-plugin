#include "vimmarkvariableprovider.h"

#include "vimhandler.h"

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

VimMarkVariableProvider::VimMarkVariableProvider(VimHandler* handler)
    : m_variables{
          makeScriptVariableDescriptor<&VimMarkVariableProvider::localMarkVariable>(
              VariableKind::Generic, u"MARK"_s, u"Vim Motions"_s, u"Local vim mark for the current playlist entry"_s),
          makeScriptVariableDescriptor<&VimMarkVariableProvider::globalMarkVariable>(
              VariableKind::Generic, u"GLOBAL_MARK"_s, u"Vim Motions"_s,
              u"Global vim mark for the current playlist entry"_s)}
{
    setHandler(handler);
}

void VimMarkVariableProvider::setHandler(VimHandler* handler)
{
    s_handler = handler;
}

std::span<const ScriptVariableDescriptor> VimMarkVariableProvider::variables() const
{
    return m_variables;
}

ScriptResult VimMarkVariableProvider::localMarkVariable(const ScriptContext& context, const QString& /*name*/)
{
    if(s_handler == nullptr) {
        return {};
    }

    const QString mark = s_handler->localMarkForScriptContext(context);
    return {.value = mark, .cond = !mark.isEmpty()};
}

ScriptResult VimMarkVariableProvider::globalMarkVariable(const ScriptContext& context, const QString& /*name*/)
{
    if(s_handler == nullptr) {
        return {};
    }

    const QString mark = s_handler->globalMarkForScriptContext(context);
    return {.value = mark, .cond = !mark.isEmpty()};
}

} // namespace Fooyin::VimMotions
