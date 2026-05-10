#include "vimmotionsplugin.h"
#include "vimhandler.h"
#include "vimlog.h"
#include "vimmotionssettings.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>

#include <QApplication>

namespace Fooyin::VimMotions {

VimMotionsPlugin::VimMotionsPlugin()  = default;
VimMotionsPlugin::~VimMotionsPlugin() = default;

void VimMotionsPlugin::initialise(const CorePluginContext& context)
{
    m_playlistHandler = context.playlistHandler;
    m_settingsManager = context.settingsManager;
    qCInfo(VIM_LOG) << "VimMotionsPlugin: core initialised"
                    << (m_playlistHandler ? "(PlaylistHandler acquired)" : "(no PlaylistHandler!)");

    VimMotionsSettings{m_settingsManager};
}

void VimMotionsPlugin::initialise(const GuiPluginContext& context)
{
    m_searchController = context.searchController;
    m_actionManager    = context.actionManager;
    qCInfo(VIM_LOG) << "VimMotionsPlugin: GUI initialising, installing event filter";
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setPlaylistHandler(m_playlistHandler);
    m_vimHandler->setActionManager(m_actionManager);
    m_vimHandler->setSettingsManager(m_settingsManager);
    qApp->installEventFilter(m_vimHandler);
}

void VimMotionsPlugin::shutdown()
{
    qCInfo(VIM_LOG) << "VimMotionsPlugin: shutting down";
    if(m_vimHandler) {
        qApp->removeEventFilter(m_vimHandler);
        m_vimHandler = nullptr;
    }
}

} // namespace Fooyin::VimMotions
