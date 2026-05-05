#include "vimmotionsplugin.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>

namespace Fooyin::VimMotions {

VimMotionsPlugin::VimMotionsPlugin() = default;
VimMotionsPlugin::~VimMotionsPlugin() = default;

void VimMotionsPlugin::initialise(const CorePluginContext& /*context*/)
{
    // Phase 3+: store playlistHandler for track manipulation (dd/yy/p/P)
}

void VimMotionsPlugin::initialise(const GuiPluginContext& /*context*/)
{
    // Phase 2: construct VimHandler, install as event filter on QApplication
    // Phase 7: register all actions with context.actionManager
}

void VimMotionsPlugin::shutdown()
{
    // Phase 2: remove event filter
}

} // namespace Fooyin::VimMotions
