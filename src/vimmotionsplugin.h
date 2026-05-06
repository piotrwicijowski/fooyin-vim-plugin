#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>

#include <QObject>

namespace Fooyin {
struct CorePluginContext;
struct GuiPluginContext;
class PlaylistHandler;

namespace VimMotions {

class VimHandler;

class VimMotionsPlugin : public QObject,
                         public Plugin,
                         public CorePlugin,
                         public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.fooyin.fooyin.plugin/1.0" FILE "vimmotions.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    explicit VimMotionsPlugin();
    ~VimMotionsPlugin() override;

    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;
    void shutdown() override;

private:
    PlaylistHandler* m_playlistHandler{nullptr};
    VimHandler*      m_vimHandler{nullptr};
};

} // namespace VimMotions
} // namespace Fooyin
