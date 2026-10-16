#include "vimmarkvariableprovider.h"

#include "vimhandler.h"

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

VimMarkVariableProvider::VimMarkVariableProvider(const VimHandler* handler)
    : m_handler{handler}
    , m_variables{makeScriptVariableDescriptor<&VimMarkVariableProvider::localMarkVariable>(
                      this, VariableKind::Generic, u"MARK"_s, u"Vim Motions"_s,
                      u"Local vim mark for the current playlist entry"_s),
                  makeScriptVariableDescriptor<&VimMarkVariableProvider::globalMarkVariable>(
                      this, VariableKind::Generic, u"GLOBAL_MARK"_s, u"Vim Motions"_s,
                      u"Global vim mark for the current playlist entry"_s)}
{ }

std::span<const ScriptVariableDescriptor> VimMarkVariableProvider::variables() const
{
    return m_variables;
}

ScriptResult VimMarkVariableProvider::localMarkVariable(const ScriptContext& context) const
{
    if(m_handler == nullptr) {
        return {};
    }

    const QString mark = m_handler->localMarkForScriptContext(context);
    return {.value = mark, .cond = !mark.isEmpty()};
}

ScriptResult VimMarkVariableProvider::globalMarkVariable(const ScriptContext& context) const
{
    if(m_handler == nullptr) {
        return {};
    }

    const QString mark = m_handler->globalMarkForScriptContext(context);
    return {.value = mark, .cond = !mark.isEmpty()};
}

} // namespace Fooyin::VimMotions
