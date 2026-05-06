#include "vimmotionsplugin.h"
#include "vimhandler.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>

#include <QApplication>

namespace Fooyin::VimMotions {

VimMotionsPlugin::VimMotionsPlugin() = default;
VimMotionsPlugin::~VimMotionsPlugin() = default;

void VimMotionsPlugin::initialise(const CorePluginContext& /*context*/)
{
    // Phase 3+: store playlistHandler for track manipulation (dd/yy/p/P)
}

void VimMotionsPlugin::initialise(const GuiPluginContext& /*context*/)
{
    m_vimHandler = new VimHandler(this);
    qApp->installEventFilter(m_vimHandler);
    // Phase 7: register all actions with context.actionManager
}

void VimMotionsPlugin::shutdown()
{
    if (m_vimHandler) {
        qApp->removeEventFilter(m_vimHandler);
        m_vimHandler = nullptr;
    }
}

} // namespace Fooyin::VimMotions
