#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

const std::vector<BindingDefault>& VimMotionsSettings::defaultBindings()
{
    static const std::vector<BindingDefault> bindings = {
        // -- Normal mode --
        {"VimMotions/Bindings/Global/Normal/j", "moveCursor:+1"},
        {"VimMotions/Bindings/Global/Normal/k", "moveCursor:-1"},
        {"VimMotions/Bindings/Global/Normal/gg", "jumpToFirst"},
        {"VimMotions/Bindings/Global/Normal/G", "jumpToLast"},
        {"VimMotions/Bindings/Global/Normal/dd", "deleteRows"},
        {"VimMotions/Bindings/PlaylistView/Normal/yy", "yankRows"},
        {"VimMotions/Bindings/Global/Normal/i", "enterInsert"},
        {"VimMotions/Bindings/Global/Normal/v", "enterVisual"},
        {"VimMotions/Bindings/PlaylistView/Normal/u", "undo"},
        {"VimMotions/Bindings/PlaylistView/Normal/Ctrl+R", "redo"},
        {"VimMotions/Bindings/Global/Normal/h", "treeCloseOrAscend"},
        {"VimMotions/Bindings/Global/Normal/l", "treeOpenOrDescend"},
        {"VimMotions/Bindings/Global/Normal/o", "focusNowPlaying"},
        {"VimMotions/Bindings/Global/Normal/g;", "focusNowPlaying"},
        {"VimMotions/Bindings/PlaylistView/Normal/m", "beginSetMark"},
        {"VimMotions/Bindings/PlaylistView/Normal/'", "beginJumpToMark"},
        {"VimMotions/Bindings/PlaylistView/Normal/`", "beginJumpToMark"},
        {"VimMotions/Bindings/PlaylistView/Normal/p", "pasteAfter"},
        {"VimMotions/Bindings/PlaylistView/Normal/P", "pasteBefore"},
        {"VimMotions/Bindings/PlaylistOrganiser/Normal/a", "organiserCreatePlaylist"},
        {"VimMotions/Bindings/PlaylistOrganiser/Normal/A", "organiserCreateGroup"},
        {"VimMotions/Bindings/Global/Normal/<Slash>", "enterSearch"},
        {"VimMotions/Bindings/Global/Normal/n", "nextMatch"},
        {"VimMotions/Bindings/Global/Normal/N", "prevMatch"},
        {"VimMotions/Bindings/Global/Normal/<Esc>", "clearPending"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+J", "spatialMoveFocus:down"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+K", "spatialMoveFocus:up"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+H", "spatialMoveFocus:left"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+L", "spatialMoveFocus:right"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+D", "moveCursorHalfPage:+1"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+U", "moveCursorHalfPage:-1"},
        {"VimMotions/Bindings/Global/Normal/Ctrl+I", "enterFilter"},
        {"VimMotions/Bindings/PlaylistOrganiser/Normal/Ctrl+Shift+J", "treeMoveSibling:+1"},
        {"VimMotions/Bindings/PlaylistOrganiser/Normal/Ctrl+Shift+K", "treeMoveSibling:-1"},
        {"VimMotions/Bindings/Global/Normal/Alt+J", "moveRows:+1"},
        {"VimMotions/Bindings/Global/Normal/Alt+K", "moveRows:-1"},

        // -- Visual mode --
        {"VimMotions/Bindings/Global/Visual/j", "extendCursor:+1"},
        {"VimMotions/Bindings/Global/Visual/k", "extendCursor:-1"},
        {"VimMotions/Bindings/Global/Visual/gg", "extendToFirst"},
        {"VimMotions/Bindings/Global/Visual/G", "extendToEnd"},
        {"VimMotions/Bindings/Global/Visual/o", "swapAnchor"},
        {"VimMotions/Bindings/Global/Visual/v", "leaveVisualMode"},
        {"VimMotions/Bindings/PlaylistView/Visual/d", "deleteSelection"},
        {"VimMotions/Bindings/PlaylistView/Visual/y", "yankSelection"},
        {"VimMotions/Bindings/Global/Visual/<Esc>", "leaveVisualMode"},
        {"VimMotions/Bindings/Global/Visual/n", "nextMatchAndExit"},
        {"VimMotions/Bindings/Global/Visual/N", "prevMatchAndExit"},
        {"VimMotions/Bindings/Global/Visual/<Slash>", "enterSearchAndExit"},
        {"VimMotions/Bindings/PlaylistView/Visual/m", "beginSetMark"},
        {"VimMotions/Bindings/PlaylistView/Visual/'", "beginJumpToMark"},
        {"VimMotions/Bindings/PlaylistView/Visual/`", "beginJumpToMark"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+D", "extendHalfPage:+1"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+U", "extendHalfPage:-1"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+J", "spatialMoveFocus:down"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+K", "spatialMoveFocus:up"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+H", "spatialMoveFocus:left"},
        {"VimMotions/Bindings/Global/Visual/Ctrl+L", "spatialMoveFocus:right"},
        {"VimMotions/Bindings/PlaylistView/Visual/Alt+J", "moveVisualSelection:+1"},
        {"VimMotions/Bindings/PlaylistView/Visual/Alt+K", "moveVisualSelection:-1"},
        {"VimMotions/Bindings/Global/Visual/h", "treeCloseOrAscend"},
        {"VimMotions/Bindings/Global/Visual/l", "treeOpenOrDescend"},
        {"VimMotions/Bindings/Global/Visual/g;", "focusNowPlayingAndExit"},

        // -- Insert mode --
        {"VimMotions/Bindings/Global/Insert/<Esc>", "leaveInsertMode"},
    };
    return bindings;
}

VimMotionsSettings::VimMotionsSettings(SettingsManager* settingsManager)
{
    using namespace Settings::VimMotions;

    settingsManager->createSetting<EnableSettingsUi>(true, u"VimMotions/EnableSettingsUi"_s);

    settingsManager->createSetting<UseDefaultBindings>(true, u"VimMotions/UseDefaultBindings"_s);

    settingsManager->createSetting<UseVimMotionsInSettings>(false, u"VimMotions/UseVimMotionsInSettings"_s);

    settingsManager->createSetting<WrapScan>(true, u"VimMotions/WrapScan"_s);

    settingsManager->createSetting<PendingSequenceTimeout>(0, u"VimMotions/PendingSequenceTimeout"_s);

    for(const auto& b : defaultBindings()) {
        settingsManager->createSetting(QString::fromLatin1(b.key), QVariant{QString::fromLatin1(b.value)});
    }
}

} // namespace Fooyin::VimMotions
