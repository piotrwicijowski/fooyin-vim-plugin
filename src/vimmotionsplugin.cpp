#include "vimmotionsplugin.h"
#include "vimhandler.h"
#include "vimlog.h"
#include "vimmodeindicatorwidget.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimmotionssettingsdialog.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>
#include <gui/widgetprovider.h>

#include <gui/plugins/pluginsettingsprovider.h>
#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QMessageBox>

#include <memory>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

namespace {

class VimMotionsPluginSettingsProvider : public PluginSettingsProvider
{
public:
    explicit VimMotionsPluginSettingsProvider(SettingsManager* settings)
        : m_settings{settings}
    { }

    void showSettings(QWidget* parent) override
    {
        if(m_settings && m_settings->value<Settings::VimMotions::EnableSettingsUi>()) {
            auto* dialog = new VimMotionsSettingsDialog(parent);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            return;
        }

        QMessageBox::information(
            parent, QApplication::translate("VimMotionsPlugin", "Vim Motions"),
            QApplication::translate("VimMotionsPlugin", "The Vim Motions settings UI is disabled."));
    }

private:
    SettingsManager* m_settings{nullptr};
};

QString modeIndicatorText(VimHandler::Mode mode)
{
    using Mode = VimHandler::Mode;

    switch(mode) {
        case Mode::Normal:
            return QApplication::translate("VimModeIndicatorWidget", "NORMAL");
        case Mode::Visual:
            return QApplication::translate("VimModeIndicatorWidget", "VISUAL");
        case Mode::Insert:
            return QApplication::translate("VimModeIndicatorWidget", "INSERT");
        case Mode::Filter:
            return QApplication::translate("VimModeIndicatorWidget", "FILTER");
        case Mode::Search:
            return QApplication::translate("VimModeIndicatorWidget", "SEARCH");
    }

    return QApplication::translate("VimModeIndicatorWidget", "NORMAL");
}

} // namespace

VimMotionsPlugin::VimMotionsPlugin()  = default;
VimMotionsPlugin::~VimMotionsPlugin() = default;

std::unique_ptr<PluginSettingsProvider> VimMotionsPlugin::settingsProvider() const
{
    return std::make_unique<VimMotionsPluginSettingsProvider>(m_settingsManager);
}

void VimMotionsPlugin::initialise(const CorePluginContext& context)
{
    m_playlistHandler = context.playlistHandler;
    m_settingsManager = context.settingsManager;
    qCInfo(VIM_LOG) << "VimMotionsPlugin: core initialised"
                    << (m_playlistHandler ? "(PlaylistHandler acquired)" : "(no PlaylistHandler!)");

    VimMotionsSettings{m_settingsManager};
    m_settingsBackend = std::make_unique<VimMotionsBindingBackend>(m_settingsManager);
}

void VimMotionsPlugin::initialise(const GuiPluginContext& context)
{
    m_searchController = context.searchController;
    m_actionManager    = context.actionManager;
    m_trackSelection   = context.trackSelection;

    qCInfo(VIM_LOG) << "VimMotionsPlugin: GUI initialising, installing event filter";
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setPlaylistHandler(m_playlistHandler);
    m_vimHandler->setActionManager(m_actionManager);
    m_vimHandler->setTrackSelectionController(m_trackSelection);
    m_vimHandler->setSettingsBackend(m_settingsBackend.get());
    m_vimHandler->setSettingsManager(m_settingsManager);
    qApp->installEventFilter(m_vimHandler);

    context.widgetProvider->registerWidget(
        u"VimModeIndicator"_s,
        [this]() {
            auto* widget = new VimModeIndicatorWidget();

            if(m_vimHandler) {
                widget->setModeText(modeIndicatorText(m_vimHandler->mode()));
                connect(m_vimHandler, &VimHandler::modeChanged, widget,
                        [widget](VimHandler::Mode mode) { widget->setModeText(modeIndicatorText(mode)); });
            }

            return widget;
        },
        QApplication::translate("VimModeIndicatorWidget", "Vim Mode Indicator"));
    context.widgetProvider->setSubMenus(u"VimModeIndicator"_s,
                                        {QApplication::translate("VimModeIndicatorWidget", "Vim Motions")});
    context.widgetProvider->setLimit(u"VimModeIndicator"_s, 1);
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
