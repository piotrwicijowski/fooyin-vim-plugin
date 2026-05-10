#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

const std::vector<BindingDefault>& VimMotionsSettings::defaultBindings()
{
    static const std::vector<BindingDefault> bindings = {
        // -- Normal mode --
        {"VimMotions/Bindings/Normal/j", "moveCursor:+1"},
        {"VimMotions/Bindings/Normal/k", "moveCursor:-1"},
        {"VimMotions/Bindings/Normal/gg", "jumpToFirst"},
        {"VimMotions/Bindings/Normal/G", "jumpToLast"},
        {"VimMotions/Bindings/Normal/dd", "deleteRows"},
        {"VimMotions/Bindings/Normal/yy", "yankRows"},
        {"VimMotions/Bindings/Normal/i", "enterInsert"},
        {"VimMotions/Bindings/Normal/v", "enterVisual"},
        {"VimMotions/Bindings/Normal/u", "undo"},
        {"VimMotions/Bindings/Normal/Ctrl+R", "redo"},
        {"VimMotions/Bindings/Normal/h", "treeCloseOrAscend"},
        {"VimMotions/Bindings/Normal/l", "treeOpenOrDescend"},
        {"VimMotions/Bindings/Normal/o", "focusNowPlaying"},
        {"VimMotions/Bindings/Normal/g;", "focusNowPlaying"},
        {"VimMotions/Bindings/Normal/m", "beginSetMark"},
        {"VimMotions/Bindings/Normal/apostrophe", "beginJumpToMark"},
        {"VimMotions/Bindings/Normal/backtick", "beginJumpToMark"},
        {"VimMotions/Bindings/Normal/p", "pasteAfter"},
        {"VimMotions/Bindings/Normal/P", "pasteBefore"},
        {"VimMotions/Bindings/Normal/a", "organiserCreatePlaylist"},
        {"VimMotions/Bindings/Normal/A", "organiserCreateGroup"},
        {"VimMotions/Bindings/Normal/slash", "enterSearch"},
        {"VimMotions/Bindings/Normal/n", "nextMatch"},
        {"VimMotions/Bindings/Normal/N", "prevMatch"},
        {"VimMotions/Bindings/Normal/Escape", "clearPending"},
        {"VimMotions/Bindings/Normal/Ctrl+J", "spatialMoveFocus:down"},
        {"VimMotions/Bindings/Normal/Ctrl+K", "spatialMoveFocus:up"},
        {"VimMotions/Bindings/Normal/Ctrl+H", "spatialMoveFocus:left"},
        {"VimMotions/Bindings/Normal/Ctrl+L", "spatialMoveFocus:right"},
        {"VimMotions/Bindings/Normal/Ctrl+D", "moveCursorHalfPage:+1"},
        {"VimMotions/Bindings/Normal/Ctrl+U", "moveCursorHalfPage:-1"},
        {"VimMotions/Bindings/Normal/Ctrl+I", "enterFilter"},
        {"VimMotions/Bindings/Normal/Ctrl+Shift+J", "treeMoveSibling:+1"},
        {"VimMotions/Bindings/Normal/Ctrl+Shift+K", "treeMoveSibling:-1"},
        {"VimMotions/Bindings/Normal/Alt+J", "moveRows:+1"},
        {"VimMotions/Bindings/Normal/Alt+K", "moveRows:-1"},

        // -- Visual mode --
        {"VimMotions/Bindings/Visual/j", "extendCursor:+1"},
        {"VimMotions/Bindings/Visual/k", "extendCursor:-1"},
        {"VimMotions/Bindings/Visual/gg", "extendToFirst"},
        {"VimMotions/Bindings/Visual/G", "extendToEnd"},
        {"VimMotions/Bindings/Visual/o", "swapAnchor"},
        {"VimMotions/Bindings/Visual/v", "leaveVisualMode"},
        {"VimMotions/Bindings/Visual/d", "deleteSelection"},
        {"VimMotions/Bindings/Visual/y", "yankSelection"},
        {"VimMotions/Bindings/Visual/Escape", "leaveVisualMode"},
        {"VimMotions/Bindings/Visual/n", "nextMatchAndExit"},
        {"VimMotions/Bindings/Visual/N", "prevMatchAndExit"},
        {"VimMotions/Bindings/Visual/slash", "enterSearchAndExit"},
        {"VimMotions/Bindings/Visual/m", "beginSetMark"},
        {"VimMotions/Bindings/Visual/apostrophe", "beginJumpToMark"},
        {"VimMotions/Bindings/Visual/backtick", "beginJumpToMark"},
        {"VimMotions/Bindings/Visual/Ctrl+D", "extendHalfPage:+1"},
        {"VimMotions/Bindings/Visual/Ctrl+U", "extendHalfPage:-1"},
        {"VimMotions/Bindings/Visual/Ctrl+J", "spatialMoveFocus:down"},
        {"VimMotions/Bindings/Visual/Ctrl+K", "spatialMoveFocus:up"},
        {"VimMotions/Bindings/Visual/Ctrl+H", "spatialMoveFocus:left"},
        {"VimMotions/Bindings/Visual/Ctrl+L", "spatialMoveFocus:right"},
        {"VimMotions/Bindings/Visual/Alt+J", "moveVisualSelection:+1"},
        {"VimMotions/Bindings/Visual/Alt+K", "moveVisualSelection:-1"},
        {"VimMotions/Bindings/Visual/h", "treeCloseOrAscend"},
        {"VimMotions/Bindings/Visual/l", "treeOpenOrDescend"},
        {"VimMotions/Bindings/Visual/g;", "focusNowPlayingAndExit"},

        // -- Insert mode --
        {"VimMotions/Bindings/Insert/Escape", "leaveInsertMode"},
    };
    return bindings;
}

VimMotionsSettings::VimMotionsSettings(SettingsManager* settingsManager)
{
    using namespace Settings::VimMotions;

    settingsManager->createSetting<UseConfigBindings>(false, u"VimMotions/UseConfigBindings"_s);

    settingsManager->createSetting<UseDefaultBindings>(true, u"VimMotions/UseDefaultBindings"_s);

    settingsManager->createSetting<WrapScan>(true, u"VimMotions/WrapScan"_s);

    settingsManager->createSetting<PendingSequenceTimeout>(0, u"VimMotions/PendingSequenceTimeout"_s);

    for(const auto& b : defaultBindings()) {
        settingsManager->createSetting(QString::fromLatin1(b.key), QVariant{QString::fromLatin1(b.value)});
    }
}

} // namespace Fooyin::VimMotions
