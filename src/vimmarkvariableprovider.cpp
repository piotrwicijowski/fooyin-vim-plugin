#include "vimmarkvariableprovider.h"

#include "vimhandler.h"

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

VimMarkVariableProvider::VimMarkVariableProvider(VimHandler* handler)
    : m_variables{makeScriptVariableDescriptor<&VimMarkVariableProvider::markVariable>(
          VariableKind::Generic, u"MARK"_s, u"Vim Motions"_s, u"Local vim mark for the current playlist entry"_s)}
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

ScriptResult VimMarkVariableProvider::markVariable(const ScriptContext& context, const QString& /*name*/)
{
    if(s_handler == nullptr) {
        return {};
    }

    const QString mark = s_handler->localMarkForScriptContext(context);
    return {.value = mark, .cond = !mark.isEmpty()};
}

} // namespace Fooyin::VimMotions
