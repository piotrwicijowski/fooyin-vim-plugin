#include "vimmotionsplugin.h"
#include "vimhandler.h"
#include "vimlog.h"

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/id.h>

#include <QAction>
#include <QApplication>
#include <QKeySequence>

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
    qCInfo(VIM_LOG) << "VimMotionsPlugin: GUI initialising, installing event filter";
    m_vimHandler = new VimHandler(this);
    m_vimHandler->setPlaylistHandler(m_playlistHandler);
    qApp->installEventFilter(m_vimHandler);

    const QStringList categories{tr("Vim Motions")};
    auto* am = context.actionManager;

    auto reg = [&](const char* name, const char* id, const QKeySequence& key = {}) {
        auto* action = new QAction{tr(name), this};
        auto* cmd    = am->registerAction(action, Id{id});
        if (!key.isEmpty())
            cmd->setDefaultShortcut(key);
        cmd->setCategories(categories);
    };

    // Single-char keys: no QKeySequence registered. They are handled by the event
    // filter in VimHandler via ShortcutOverride. Registering a Qt shortcut for
    // bare characters (j, k, v, ...) would cause them to be consumed by the
    // shortcut system in Insert mode, preventing normal fooyin key handling.
    reg("Cursor Down",    "VimMotions.CursorDown");    // j
    reg("Cursor Up",      "VimMotions.CursorUp");      // k
    reg("Cursor Top",     "VimMotions.CursorTop");     // gg
    reg("Cursor Bottom",  "VimMotions.CursorBottom");  // G
    reg("Activate",       "VimMotions.Activate");      // Enter
    reg("Visual Mode",    "VimMotions.VisualMode");    // v
    reg("Delete Line",    "VimMotions.DeleteLine");    // dd
    reg("Yank Line",      "VimMotions.YankLine");      // yy
    reg("Paste After",    "VimMotions.PasteAfter");    // p
    reg("Paste Before",   "VimMotions.PasteBefore");   // P
    reg("Insert Mode",    "VimMotions.InsertMode");    // i

    // Ctrl/Alt combos are safe to register as real shortcuts — they do not
    // conflict with fooyin's normal single-key bindings.
    reg("Half Page Down", "VimMotions.HalfPageDown",  QKeySequence{"Ctrl+D"});
    reg("Half Page Up",   "VimMotions.HalfPageUp",    QKeySequence{"Ctrl+U"});
    reg("Focus Down",     "VimMotions.FocusDown",     QKeySequence{"Ctrl+J"});
    reg("Focus Up",       "VimMotions.FocusUp",       QKeySequence{"Ctrl+K"});
    reg("Focus Left",     "VimMotions.FocusLeft",     QKeySequence{"Ctrl+H"});
    reg("Focus Right",    "VimMotions.FocusRight",    QKeySequence{"Ctrl+L"});
    reg("Move Down",      "VimMotions.MoveDown",      QKeySequence{"Alt+J"});
    reg("Move Up",        "VimMotions.MoveUp",        QKeySequence{"Alt+K"});
    qCInfo(VIM_LOG) << "VimMotionsPlugin: registered 19 actions under 'Vim Motions'";
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
