#include "vimactions.h"
#include "spatialnavigator.h"
#include "vimhandler.h"

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

void VimActions::registerAction(const QString& name, HandlerFn handler)
{
    m_actions.insert(name, std::move(handler));
}

VimActions::HandlerFn VimActions::find(const QString& name) const
{
    return m_actions.value(name);
}

void VimActions::registerAll()
{
    // -- Normal-mode movement --
    registerAction(u"moveCursor"_s, [](VimHandler& h, const QStringView& a) { h.moveCursor(a.toInt()); });
    registerAction(u"jumpToFirst"_s, [](VimHandler& h, const QStringView&) { h.jumpToFirst(); });
    registerAction(u"jumpToLast"_s, [](VimHandler& h, const QStringView&) {
        if(h.hadExplicitCount()) {
            h.jumpToRow(h.currentCount() - 1);
        }
        else {
            h.jumpToLast();
        }
    });
    registerAction(u"jumpToRow"_s, [](VimHandler& h, const QStringView& a) { h.jumpToRow(a.toInt()); });
    registerAction(u"moveCursorHalfPage"_s,
                   [](VimHandler& h, const QStringView& a) { h.moveCursorHalfPage(a.toInt()); });
    registerAction(u"activateCurrentRow"_s, [](VimHandler& h, const QStringView&) { h.activateCurrentRow(); });

    // -- Tree navigation --
    registerAction(u"treeMoveSibling"_s, [](VimHandler& h, const QStringView& a) { h.treeMoveSibling(a.toInt()); });
    registerAction(u"treeOpenOrDescend"_s, [](VimHandler& h, const QStringView&) { h.treeOpenOrDescend(); });
    registerAction(u"treeCloseOrAscend"_s, [](VimHandler& h, const QStringView&) { h.treeCloseOrAscend(); });
    registerAction(u"organiserCreatePlaylist"_s,
                   [](VimHandler& h, const QStringView&) { h.organiserCreatePlaylist(); });
    registerAction(u"organiserCreateGroup"_s, [](VimHandler& h, const QStringView&) { h.organiserCreateGroup(); });

    // -- Mode switching --
    registerAction(u"enterInsert"_s, [](VimHandler& h, const QStringView&) { h.enterInsert(); });
    registerAction(u"leaveInsertMode"_s, [](VimHandler& h, const QStringView&) { h.enterNormal(); });
    registerAction(u"enterVisual"_s, [](VimHandler& h, const QStringView&) { h.enterVisual(); });
    registerAction(u"selectAll"_s, [](VimHandler& h, const QStringView&) { h.selectAll(); });
    registerAction(u"leaveVisualMode"_s, [](VimHandler& h, const QStringView&) { h.enterNormal(); });

    // -- Yank / delete / paste --
    registerAction(u"deleteRows"_s, [](VimHandler& h, const QStringView&) { h.deleteRows(h.currentCount()); });
    registerAction(u"yankRows"_s, [](VimHandler& h, const QStringView&) { h.yankRows(h.currentCount()); });
    registerAction(u"pasteAfter"_s, [](VimHandler& h, const QStringView&) { h.pasteAfter(); });
    registerAction(u"pasteBefore"_s, [](VimHandler& h, const QStringView&) { h.pasteBefore(); });
    registerAction(u"copyAfterCurrentPlaying"_s,
                   [](VimHandler& h, const QStringView&) { h.copyAfterCurrentPlaying(); });
    registerAction(u"moveAfterCurrentPlaying"_s,
                   [](VimHandler& h, const QStringView&) { h.moveAfterCurrentPlaying(); });

    // -- Row movement (Alt+J/K) --
    registerAction(u"moveRows"_s, [](VimHandler& h, const QStringView& a) { h.moveRows(a.toInt()); });
    registerAction(u"extendCursor"_s, [](VimHandler& h, const QStringView& a) { h.extendVisualCursor(a.toInt()); });
    registerAction(u"extendToFirst"_s, [](VimHandler& h, const QStringView&) { h.extendVisualToFirst(); });
    registerAction(u"extendToEnd"_s, [](VimHandler& h, const QStringView&) { h.extendVisualToEnd(); });
    registerAction(u"extendToRow"_s, [](VimHandler& h, const QStringView& a) { h.extendVisualToRow(a.toInt()); });
    registerAction(u"swapAnchor"_s, [](VimHandler& h, const QStringView&) { h.swapVisualAnchor(); });
    registerAction(u"deleteSelection"_s, [](VimHandler& h, const QStringView&) { h.deleteVisualSelection(); });
    registerAction(u"yankSelection"_s, [](VimHandler& h, const QStringView&) { h.yankVisualSelection(); });
    registerAction(u"extendHalfPage"_s, [](VimHandler& h, const QStringView& a) { h.extendVisualHalfPage(a.toInt()); });
    registerAction(u"moveVisualSelection"_s,
                   [](VimHandler& h, const QStringView& a) { h.moveVisualSelection(a.toInt()); });

    // -- Spatial navigation --
    registerAction(u"spatialMoveFocus"_s, [](VimHandler& h, const QStringView& a) {
        Direction dir = Direction::Down;
        if(a == u"up")
            dir = Direction::Up;
        else if(a == u"left")
            dir = Direction::Left;
        else if(a == u"right")
            dir = Direction::Right;
        h.moveSpatialFocus(dir);
    });

    // -- Undo / redo --
    registerAction(u"undo"_s, [](VimHandler& h, const QStringView&) { h.undo(); });
    registerAction(u"redo"_s, [](VimHandler& h, const QStringView&) { h.redo(); });

    // -- Focus --
    registerAction(u"focusNowPlaying"_s, [](VimHandler& h, const QStringView&) { h.focusNowPlaying(); });
    registerAction(u"nextPlaylist"_s, [](VimHandler& h, const QStringView&) { h.nextPlaylist(); });
    registerAction(u"previousPlaylist"_s, [](VimHandler& h, const QStringView&) { h.previousPlaylist(); });
    registerAction(u"fooyinAction"_s, [](VimHandler& h, const QStringView& a) { h.triggerFooyinAction(a); });
    registerAction(u"focusNowPlayingAndExit"_s, [](VimHandler& h, const QStringView&) {
        h.enterNormal();
        h.focusNowPlaying();
    });
    registerAction(u"focusNowPlayingAndExit"_s, [](VimHandler& h, const QStringView&) {
        h.enterNormal();
        h.focusNowPlaying();
    });
    registerAction(u"beginSetMark"_s, [](VimHandler& h, const QStringView&) { h.beginSetMark(); });
    registerAction(u"beginJumpToMark"_s, [](VimHandler& h, const QStringView&) { h.beginJumpToMark(); });

    // -- Search / filter --
    registerAction(u"enterSearch"_s, [](VimHandler& h, const QStringView&) { h.enterSearch(); });
    registerAction(u"nextMatch"_s, [](VimHandler& h, const QStringView&) { h.nextMatch(); });
    registerAction(u"prevMatch"_s, [](VimHandler& h, const QStringView&) { h.prevMatch(); });
    registerAction(u"enterFilter"_s, [](VimHandler& h, const QStringView&) { h.enterFilter(); });
    registerAction(u"nextMatchAndExit"_s, [](VimHandler& h, const QStringView&) {
        h.enterNormal();
        h.nextMatch();
    });
    registerAction(u"prevMatchAndExit"_s, [](VimHandler& h, const QStringView&) {
        h.enterNormal();
        h.prevMatch();
    });
    registerAction(u"enterSearchAndExit"_s, [](VimHandler& h, const QStringView&) {
        h.enterNormal();
        h.enterSearch();
    });

    // -- State management --
    registerAction(u"clearPending"_s, [](VimHandler& h, const QStringView&) { h.clearPendingState(); });
}

} // namespace Fooyin::VimMotions
