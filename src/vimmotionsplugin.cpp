#include "vimmotionsplugin.h"
#include "vimhandler.h"
#include "vimlog.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>

#include <QApplication>

namespace Fooyin::VimMotions {

VimMotionsPlugin::VimMotionsPlugin() = default;
VimMotionsPlugin::~VimMotionsPlugin() = default;

void VimMotionsPlugin::initialise(const CorePluginContext& context)
{
    m_playlistHandler = context.playlistHandler;
    qCInfo(VIM_LOG) << "VimMotionsPlugin: core initialised"
                    << (m_playlistHandler ? "(PlaylistHandler acquired)" : "(no PlaylistHandler!)");
}

void VimMotionsPlugin::initialise(const GuiPluginContext& context)
{
    m_searchController = context.searchController;
    qCInfo(VIM_LOG) << "VimMotionsPlugin: GUI initialising, installing event filter";
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setPlaylistHandler(m_playlistHandler);
    qApp->installEventFilter(m_vimHandler);
}

void VimMotionsPlugin::shutdown()
{
    qCInfo(VIM_LOG) << "VimMotionsPlugin: shutting down";
    if (m_vimHandler) {
        qApp->removeEventFilter(m_vimHandler);
        m_vimHandler = nullptr;
    }
}

} // namespace Fooyin::VimMotions
