#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>
#include <gui/plugins/pluginconfigguiplugin.h>

#include <QObject>
#include <memory>

namespace Fooyin {
struct CorePluginContext;
struct GuiPluginContext;
class ActionManager;
class PlaylistHandler;
class CurrentPlaylistController;
class SearchController;
class SettingsManager;
class TrackSelectionController;

namespace VimMotions {

class VimHandler;
class VimMotionsBindingBackend;

class VimMotionsPlugin : public QObject,
                         public Plugin,
                         public CorePlugin,
                         public GuiPlugin,
                         public PluginConfigGuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.fooyin.fooyin.plugin/1.0" FILE "vimmotions.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin Fooyin::PluginConfigGuiPlugin)

public:
    explicit VimMotionsPlugin();
    ~VimMotionsPlugin() override;

    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;
    [[nodiscard]] std::unique_ptr<PluginSettingsProvider> settingsProvider() const override;
    void shutdown() override;

private:
    ActionManager* m_actionManager{nullptr};
    PlaylistHandler* m_playlistHandler{nullptr};
    CurrentPlaylistController* m_playlistSelection{nullptr};
    SearchController* m_searchController{nullptr};
    TrackSelectionController* m_trackSelection{nullptr};
    SettingsManager* m_settingsManager{nullptr};
    std::unique_ptr<VimMotionsBindingBackend> m_settingsBackend;
    VimHandler* m_vimHandler{nullptr};
};

} // namespace VimMotions
} // namespace Fooyin
