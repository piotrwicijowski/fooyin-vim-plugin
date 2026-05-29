#include "vimhandler.h"
#include "spatialnavigator.h"
#include "viewlocator.h"
#include "vimbindingparser.h"
#include "vimlog.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimsearchbar.h"

#include <core/playlist/playlist.h>
#include <core/playlist/playlisthandler.h>
#include <core/plugins/coreplugincontext.h>
#include <gui/fywidget.h>
#include <gui/guiconstants.h>
#include <gui/playlist/currentplaylistcontroller.h>
#include <gui/trackselectioncontroller.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QItemSelection>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>
#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(VIM_LOG, "fy.vim")

namespace Fooyin::VimMotions {

namespace {

using ScopedConfigBindings = VimHandler::ScopedConfigBindings;
using ModeConfigBindings   = VimHandler::ModeConfigBindings;

VimHandler::Mode toHandlerMode(BindingMode mode)
{
    switch(mode) {
        case BindingMode::Normal:
            return VimHandler::Mode::Normal;
        case BindingMode::Visual:
            return VimHandler::Mode::Visual;
        case BindingMode::Insert:
            return VimHandler::Mode::Insert;
    }

    return VimHandler::Mode::Normal;
}

ModeConfigBindings convertBindings(const EffectiveBindings& source)
{
    ModeConfigBindings bindings;

    for(auto it = source.cbegin(); it != source.cend(); ++it) {
        ScopedConfigBindings scopedBindings;
        for(auto scopedIt = it.value().cbegin(); scopedIt != it.value().cend(); ++scopedIt)
            scopedBindings.insert(scopedIt.key(), scopedIt.value());

        bindings.insert(toHandlerMode(it.key()), scopedBindings);
    }

    return bindings;
}

bool isSettingsDialogObject(const QObject* object)
{
    return object && QString::fromLatin1(object->metaObject()->className()).contains(QStringLiteral("SettingsDialog"));
}

constexpr int kOrganiserItemTypeRole     = Qt::UserRole;
constexpr int kOrganiserPlaylistDataRole = Qt::UserRole + 1;
constexpr int kOrganiserPlaylistItemType = 2;

void appendOrganiserPlaylists(QAbstractItemModel* model, const QModelIndex& parent, std::vector<Fooyin::Playlist*>& out)
{
    if(!model) {
        return;
    }

    for(int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if(!index.isValid()) {
            continue;
        }

        if(index.data(kOrganiserItemTypeRole).toInt() == kOrganiserPlaylistItemType) {
            if(auto* playlist = index.data(kOrganiserPlaylistDataRole).value<Fooyin::Playlist*>()) {
                out.push_back(playlist);
            }
        }

        if(model->hasChildren(index)) {
            appendOrganiserPlaylists(model, index, out);
        }
    }
}

QTreeView* findPlaylistOrganiserTree(const VimHandler& handler)
{
    if(auto* focused = qobject_cast<QTreeView*>(qApp->focusWidget())) {
        if(handler.viewContext(focused) == VimHandler::ViewContext::PlaylistOrganiser) {
            return focused;
        }
    }

    for(QWidget* widget : QApplication::allWidgets()) {
        auto* tree = qobject_cast<QTreeView*>(widget);
        if(!tree || !tree->model() || !tree->isVisible()) {
            continue;
        }
        if(handler.viewContext(tree) == VimHandler::ViewContext::PlaylistOrganiser) {
            return tree;
        }
    }

    return nullptr;
}

bool hasSettingsDialogAncestor(const QObject* object)
{
    for(auto* current = object; current; current = current->parent()) {
        if(isSettingsDialogObject(current))
            return true;
    }

    if(const auto* widget = qobject_cast<const QWidget*>(object)) {
        if(const QWidget* window = widget->window(); window && window != widget && hasSettingsDialogAncestor(window))
            return true;
    }

    return false;
}

bool isEditableInputObject(const QObject* object)
{
    for(auto* current = object; current; current = current->parent()) {
        if(qobject_cast<const QLineEdit*>(current) || qobject_cast<const QTextEdit*>(current)
           || qobject_cast<const QPlainTextEdit*>(current) || qobject_cast<const QAbstractSpinBox*>(current)) {
            return true;
        }

        if(const auto* comboBox = qobject_cast<const QComboBox*>(current); comboBox && comboBox->isEditable())
            return true;
    }

    if(const auto* widget = qobject_cast<const QWidget*>(object)) {
        if(const QWidget* window = widget->window(); window && window != widget && isEditableInputObject(window))
            return true;
    }

    return false;
}

} // namespace

static QTreeView* asTreeView(QAbstractItemView* v)
{
    return qobject_cast<QTreeView*>(v);
}

static QAbstractItemView* enclosingView(QWidget* widget)
{
    while(widget) {
        if(auto* view = qobject_cast<QAbstractItemView*>(widget))
            return view;
        widget = widget->parentWidget();
    }
    return nullptr;
}

static QString searchTextForIndex(const QAbstractItemModel* model, const QModelIndex& index)
{
    if(!model || !index.isValid())
        return {};

    const QList<int> roles{Qt::ToolTipRole, Qt::DisplayRole, Qt::EditRole};
    for(const int role : roles) {
        const QString text = model->data(index, role).toString();
        if(!text.isEmpty())
            return text;
    }

    return {};
}

static QString searchIndexPath(QModelIndex index)
{
    QStringList parts;
    while(index.isValid()) {
        parts.prepend(searchTextForIndex(index.model(), index));
        index = index.parent();
    }
    return parts.isEmpty() ? QStringLiteral("<root>") : parts.join(QStringLiteral(" / "));
}

static QString searchIndexDescription(const QModelIndex& index)
{
    if(!index.isValid())
        return QStringLiteral("<invalid>");

    const QString title = searchTextForIndex(index.model(), index);
    return QStringLiteral("title=\"%1\" row=%2 col=%3 path=\"%4\"")
        .arg(title, QString::number(index.row()), QString::number(index.column()), searchIndexPath(index));
}

static int searchMatchPosition(const std::vector<QPersistentModelIndex>& matches, const QModelIndex& index)
{
    if(!index.isValid())
        return -1;

    for(int i = 0; i < static_cast<int>(matches.size()); ++i) {
        if(matches[static_cast<size_t>(i)] == index)
            return i;
    }

    return -1;
}

static int firstVisibleSearchMatchIndex(QTreeView* tree, const std::vector<QPersistentModelIndex>& matches,
                                        const QModelIndex& start, bool wrapScan)
{
    if(!tree || matches.empty())
        return -1;

    if(!start.isValid())
        return 0;

    for(QModelIndex index = start; index.isValid(); index = tree->indexBelow(index)) {
        if(const int matchIdx = searchMatchPosition(matches, index); matchIdx >= 0)
            return matchIdx;
    }

    return wrapScan ? 0 : -1;
}

static std::vector<QPersistentModelIndex> orderedSearchMatches(QAbstractItemView* view,
                                                               const std::vector<QPersistentModelIndex>& matches)
{
    std::vector<QPersistentModelIndex> ordered;
    if(!view || !view->model() || matches.empty())
        return ordered;

    auto* model = view->model();
    ordered.reserve(matches.size());

    const auto appendIfMatched = [&](const QModelIndex& index) {
        if(searchMatchPosition(matches, index) >= 0)
            ordered.push_back(index);
    };

    if(auto* tree = asTreeView(view)) {
        QModelIndex index;
        if(model->rowCount() > 0)
            index = model->index(0, 0);

        for(; index.isValid(); index = tree->indexBelow(index))
            appendIfMatched(index);
    }
    else {
        for(int row = 0; row < model->rowCount(); ++row)
            appendIfMatched(model->index(row, 0));
    }

    return ordered;
}

struct OrganiserDropTarget
{
    QModelIndex parent;
    int row{-1};
};

static bool isAncestorIndex(const QModelIndex& ancestor, QModelIndex index)
{
    for(QModelIndex cursor = index.parent(); cursor.isValid(); cursor = cursor.parent()) {
        if(cursor == ancestor)
            return true;
    }
    return false;
}

static QString organiserIndexPath(QModelIndex index)
{
    QStringList parts;
    while(index.isValid()) {
        parts.prepend(index.data().toString());
        index = index.parent();
    }
    return parts.isEmpty() ? QStringLiteral("<root>") : parts.join(QStringLiteral(" / "));
}

static QModelIndex lastVisibleDescendant(QTreeView* tree, QModelIndex index)
{
    Q_ASSERT(tree);

    while(index.isValid() && tree->isExpanded(index) && tree->model()->rowCount(index) > 0)
        index = tree->model()->index(tree->model()->rowCount(index) - 1, 0, index);

    return index;
}

static bool isEmptyGroup(QTreeView* tree, const QModelIndex& index)
{
    return tree && index.isValid() && tree->model() && tree->model()->hasChildren(index)
        && tree->model()->rowCount(index) == 0;
}

static OrganiserDropTarget organiserDropTargetForVisibleMove(QTreeView* tree, const QModelIndex& current,
                                                             const QModelIndex& target, int direction)
{
    Q_ASSERT(tree);
    Q_ASSERT(direction == -1 || direction == +1);

    if(direction < 0) {
        if(isAncestorIndex(target, current))
            return {target.parent(), target.row()};

        if(isEmptyGroup(tree, target)) {
            if(current.parent() != target.parent() && target.parent().isValid())
                return {target.parent(), target.row() + 1};
            return {target, 0};
        }

        if(const QModelIndex targetParent = target.parent();
           targetParent.isValid() && tree->indexBelow(lastVisibleDescendant(tree, targetParent)) == current) {
            if(current.parent() != targetParent.parent() && targetParent.parent().isValid())
                return {targetParent.parent(), targetParent.row() + 1};
            return {targetParent, tree->model()->rowCount(targetParent)};
        }

        return {target.parent(), target.row()};
    }

    if(const QModelIndex currentParent = current.parent();
       currentParent.isValid() && tree->indexBelow(lastVisibleDescendant(tree, currentParent)) == target) {
        return {currentParent.parent(), currentParent.row() + 1};
    }

    if(isEmptyGroup(tree, target) || (tree->isExpanded(target) && tree->model()->rowCount(target) > 0))
        return {target, 0};

    return {target.parent(), target.row() + 1};
}

static QModelIndex organiserMovedIndexAfterDrop(QTreeView* tree, const QModelIndex& sourceParent, int sourceRow,
                                                const OrganiserDropTarget& dropTarget)
{
    if(!tree || !tree->model() || sourceRow < 0)
        return {};

    const QModelIndex finalParent = dropTarget.parent;
    int finalRow                  = dropTarget.row;
    if(sourceParent == finalParent && sourceRow < finalRow)
        --finalRow;

    const int rowCount = tree->model()->rowCount(finalParent);
    if(rowCount <= 0)
        return {};

    finalRow = std::clamp(finalRow, 0, rowCount - 1);
    return tree->model()->index(finalRow, 0, finalParent);
}

VimHandler::VimHandler(QObject* parent)
    : QObject{parent}
    , m_viewLocator{new ViewLocator(this)}
    , m_spatialNavigator{new SpatialNavigator(this)}
{
    m_pendingTimeoutTimer.setSingleShot(true);
    connect(&m_pendingTimeoutTimer, &QTimer::timeout, this, [this]() {
        if(!m_pendingConfigSequence.isEmpty() && m_pendingConfigFallback.has_value()) {
            const auto fallback = *m_pendingConfigFallback;
            qCDebug(VIM_LOG) << "Pending input timeout expired; executing fallback action" << fallback.actionName;
            clearPendingInputState();
            executeAction(fallback);
            return;
        }

        qCDebug(VIM_LOG) << "Pending input timeout expired; clearing pending state";
        clearPendingInputState();
    });

    if(qApp) {
        QObject::connect(qApp, &QApplication::focusChanged, this, [this](QWidget* /*old*/, QWidget* now) {
            auto* view = enclosingView(now);
            updateLastPlaylistView(view);
            tryRestorePendingPlaylistState(view);
        });
    }

    m_actions.registerAll();
    qCDebug(VIM_LOG) << "VimHandler created";
}

VimHandler::~VimHandler()
{
    delete m_filterBar;
    delete m_searchBar;
}

VimHandler::Mode VimHandler::mode() const
{
    return m_mode;
}

bool VimHandler::eventFilter(QObject* watched, QEvent* event)
{
    auto* focusView = enclosingView(QApplication::focusWidget());
    if(!focusView) {
        if(auto* widget = qobject_cast<QWidget*>(watched))
            focusView = enclosingView(widget);
    }

    updateLastPlaylistView(focusView);
    tryRestorePendingPlaylistState(focusView);

    if(event->type() == QEvent::FocusIn && focusView && viewContext(focusView) == ViewContext::PlaylistView
       && m_playlistHandler && m_observedSelectedPlaylistId.isValid()) {
        if(const auto stateIt = m_playlistCursorStates.constFind(m_observedSelectedPlaylistId);
           stateIt != m_playlistCursorStates.cend()) {
            if(auto* playlist = m_playlistHandler->playlistById(m_observedSelectedPlaylistId)) {
                qCDebug(VIM_LOG) << "eventFilter: FocusIn restoring selected playlist state for" << playlist->name();
                applyPlaylistCursorState(focusView, playlist, stateIt.value());
            }
        }
    }

    if(m_suppressFilter)
        return false;

    if(shouldSkipBindings(watched))
        return false;

    const auto type = event->type();

    if((type == QEvent::ShortcutOverride || type == QEvent::KeyPress) && organiserEditorActive(watched))
        return false;

    // Qt sends ShortcutOverride before it would activate a matching QShortcut.
    // If the receiver (or its filter) returns true here, Qt skips the shortcut
    // and delivers the normal KeyPress instead. We only claim keys that vim
    // actually handles so unrecognised shortcuts pass through normally.
    if(type == QEvent::ShortcutOverride) {
        auto* kev = static_cast<QKeyEvent*>(event);
        qCDebug(VIM_LOG) << "eventFilter ShortcutOverride: key=" << kev->key() << "text=" << kev->text()
                         << "mods=" << kev->modifiers() << "mode=" << static_cast<int>(m_mode)
                         << "watched=" << watched->metaObject()->className();
        bool claim = false;
        if(m_mode == Mode::Normal || m_mode == Mode::Visual || m_mode == Mode::Insert)
            claim = wouldHandleFromConfig(kev, m_mode);
        if(claim) {
            kev->accept();
            return true;
        }
        return false;
    }

    if(type != QEvent::KeyPress)
        return false;

    return handleKeyPress(static_cast<QKeyEvent*>(event));
}

bool VimHandler::handleKeyPress(QKeyEvent* ev)
{
    switch(m_mode) {
        case Mode::Insert:
            return dispatchFromConfig(ev, Mode::Insert);
        case Mode::Normal:
            return dispatchFromConfig(ev, Mode::Normal);
        case Mode::Visual:
            return dispatchFromConfig(ev, Mode::Visual);
        case Mode::Filter:
            return false;
        case Mode::Search:
            return false;
    }
    return false;
}

bool VimHandler::shouldSkipBindings(QObject* watched) const
{
    if(watched && isEditableInputObject(watched))
        return true;

    for(QObject* candidate :
        {static_cast<QObject*>(QApplication::focusWidget()), static_cast<QObject*>(QApplication::activeModalWidget()),
         static_cast<QObject*>(QApplication::activeWindow())}) {
        if(candidate && isEditableInputObject(candidate))
            return true;
    }

    if(m_useVimMotionsInSettings)
        return false;

    if(watched && hasSettingsDialogAncestor(watched))
        return true;

    for(QObject* candidate :
        {static_cast<QObject*>(QApplication::focusWidget()), static_cast<QObject*>(QApplication::activeModalWidget()),
         static_cast<QObject*>(QApplication::activeWindow())}) {
        if(candidate && hasSettingsDialogAncestor(candidate))
            return true;
    }

    return false;
}

bool VimHandler::hasPendingInput() const
{
    return !m_pendingKey.isNull() || !m_pendingConfigSequence.isEmpty() || m_pendingMarkOp != PendingMarkOp::None;
}

// ---------------------------------------------------------------------------
// Mode transitions
// ---------------------------------------------------------------------------

void VimHandler::enterNormal()
{
    qCInfo(VIM_LOG) << "Mode → Normal (from" << static_cast<int>(m_mode) << ")";
    if(m_mode == Mode::Visual) {
        // Collapse the visual selection to the cursor row so the paste target
        // remains highlighted after leaving Visual mode.
        auto* view = m_viewLocator->activeView();
        if(view && view->model() && view->selectionModel()) {
            const int row         = qMax(0, m_visualCursor);
            const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
            const QModelIndex idx = view->model()->index(row, col);
            qCDebug(VIM_LOG) << "enterNormal (from Visual): collapsing selection to row" << row << "col" << col;
            view->selectionModel()->setCurrentIndex(idx.isValid() ? idx : view->currentIndex(),
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    m_mode = Mode::Normal;
    clearPendingInputState();
    m_count        = 0;
    m_visualAnchor = -1;
    m_visualCursor = -1;
    emit modeChanged(m_mode);
}

void VimHandler::enterInsert()
{
    qCInfo(VIM_LOG) << "Mode → Insert";
    m_mode = Mode::Insert;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);
}

void VimHandler::enterVisual()
{
    m_mode = Mode::Visual;
    clearPendingInputState();
    auto* view = m_viewLocator->activeView();
    if(view && view->currentIndex().isValid()) {
        m_visualAnchor = view->currentIndex().row();
        m_visualCursor = m_visualAnchor;
    }
    else {
        m_visualAnchor = 0;
        m_visualCursor = 0;
    }
    qCInfo(VIM_LOG) << "Mode → Visual, anchor/cursor seeded at row" << m_visualAnchor;
    updateVisualSelection();
    emit modeChanged(m_mode);
}

void VimHandler::selectAll()
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model() || !view->selectionModel()) {
        qCWarning(VIM_LOG) << "selectAll: no active view / model / selectionModel";
        return;
    }

    const int last = view->model()->rowCount() - 1;
    if(last < 0) {
        qCDebug(VIM_LOG) << "selectAll: view is empty";
        return;
    }

    m_mode = Mode::Visual;
    clearPendingInputState();
    m_count        = 0;
    m_visualAnchor = 0;
    m_visualCursor = last;
    qCInfo(VIM_LOG) << "Mode → Visual, select all rows [0," << last << "]";
    updateVisualSelection();
    emit modeChanged(m_mode);
}

// ---------------------------------------------------------------------------
// Cursor navigation
// ---------------------------------------------------------------------------

void VimHandler::moveCursor(int delta)
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveCursor: no active view or model";
        return;
    }

    if(auto* tree = asTreeView(view)) {
        treeMoveCursor(tree, delta);
        return;
    }

    const int last = view->model()->rowCount() - 1;
    if(last < 0) {
        qCDebug(VIM_LOG) << "moveCursor: view is empty";
        return;
    }
    const int col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int row  = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int dest = std::clamp(row + delta, 0, last);
    qCDebug(VIM_LOG) << "moveCursor: row" << row << "->" << dest << "(delta=" << delta << ", last=" << last << ")"
                     << "view=" << view->metaObject()->className();
    view->selectionModel()->setCurrentIndex(view->model()->index(dest, col),
                                            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::jumpToFirst()
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model() || view->model()->rowCount() == 0) {
        qCWarning(VIM_LOG) << "jumpToFirst: no active view or model is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col     = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    qCDebug(VIM_LOG) << "jumpToFirst: row" << fromRow << "-> 0";
    view->selectionModel()->setCurrentIndex(view->model()->index(0, col),
                                            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::jumpToLast()
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "jumpToLast: no active view or model";
        return;
    }

    if(auto* tree = asTreeView(view)) {
        QModelIndex cur = tree->model()->index(0, 0);
        if(!cur.isValid())
            return;
        while(true) {
            const QModelIndex next = tree->indexBelow(cur);
            if(!next.isValid())
                break;
            cur = next;
        }
        qCDebug(VIM_LOG) << "jumpToLast (tree): last visible item row=" << cur.row();
        tree->selectionModel()->setCurrentIndex(cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        return;
    }

    const int last = view->model()->rowCount() - 1;
    if(last < 0) {
        qCDebug(VIM_LOG) << "jumpToLast: view is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col     = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    qCDebug(VIM_LOG) << "jumpToLast: row" << fromRow << "->" << last;
    view->selectionModel()->setCurrentIndex(view->model()->index(last, col),
                                            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::jumpToRow(int row)
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "jumpToRow: no active view or model";
        return;
    }
    const int last = view->model()->rowCount() - 1;
    if(last < 0) {
        qCDebug(VIM_LOG) << "jumpToRow: view is empty";
        return;
    }
    const int fromRow = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    const int col     = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int dest    = std::clamp(row, 0, last);
    qCDebug(VIM_LOG) << "jumpToRow: requested=" << row << "clamped=" << dest << "from=" << fromRow;
    view->selectionModel()->setCurrentIndex(view->model()->index(dest, col),
                                            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

int VimHandler::halfPageDelta() const
{
    auto* view = m_viewLocator->activeView();
    if(!view)
        return 1;
    const QModelIndex cur = view->currentIndex();
    int itemH             = cur.isValid() ? view->visualRect(cur).height() : 0;
    if(itemH <= 0)
        itemH = 20;
    return qMax(1, view->height() / itemH / 2);
}

void VimHandler::moveCursorHalfPage(int direction)
{
    const int halfPage = halfPageDelta();
    qCDebug(VIM_LOG) << "moveCursorHalfPage: direction=" << direction << "halfPage=" << halfPage
                     << "effective delta=" << (direction * halfPage);
    moveCursor(direction * halfPage);
}

void VimHandler::activateCurrentRow()
{
    auto* view = m_viewLocator->activeView();
    if(!view) {
        qCWarning(VIM_LOG) << "activateCurrentRow: no active view";
        return;
    }
    const QModelIndex idx = view->currentIndex();
    if(!idx.isValid()) {
        qCWarning(VIM_LOG) << "activateCurrentRow: no valid current index";
        return;
    }
    qCDebug(VIM_LOG) << "activateCurrentRow: row=" << idx.row() << "view=" << view->metaObject()->className();
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    m_suppressFilter = true;
    QCoreApplication::sendEvent(view, &ev);
    m_suppressFilter = false;
}

// ---------------------------------------------------------------------------
// Tree navigation (PlaylistOrganiser)
// ---------------------------------------------------------------------------

void VimHandler::treeMoveCursor(QTreeView* tree, int delta)
{
    QModelIndex cur = tree->currentIndex();
    if(!cur.isValid())
        cur = tree->model()->index(0, 0);
    if(!cur.isValid())
        return;

    const int steps = std::abs(delta);
    for(int i = 0; i < steps; ++i) {
        const QModelIndex next = (delta > 0) ? tree->indexBelow(cur) : tree->indexAbove(cur);
        if(!next.isValid())
            break;
        cur = next;
    }
    qCDebug(VIM_LOG) << "treeMoveCursor: delta=" << delta << "→ row=" << cur.row();
    tree->selectionModel()->setCurrentIndex(cur, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::treeMoveSibling(int delta)
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if(!tree)
        return;

    const QModelIndex cur = tree->currentIndex();
    if(!cur.isValid())
        return;

    const int targetRow = cur.row() + delta;
    if(targetRow < 0)
        return;

    const QModelIndex sibling = tree->model()->index(targetRow, 0, cur.parent());
    if(!sibling.isValid())
        return;

    qCDebug(VIM_LOG) << "treeMoveSibling: delta=" << delta << "row" << cur.row() << "->" << targetRow;
    tree->selectionModel()->setCurrentIndex(sibling, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::treeOpenOrDescend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if(!tree)
        return;

    const QModelIndex cur = tree->currentIndex();
    if(!cur.isValid())
        return;

    if(!tree->isExpanded(cur)) {
        qCDebug(VIM_LOG) << "treeOpenOrDescend: expanding row=" << cur.row();
        tree->expand(cur);
    }
    else {
        const QModelIndex child = tree->model()->index(0, 0, cur);
        if(child.isValid()) {
            qCDebug(VIM_LOG) << "treeOpenOrDescend: descending to first child";
            tree->selectionModel()->setCurrentIndex(child,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        else {
            qCDebug(VIM_LOG) << "treeOpenOrDescend: already expanded leaf, no-op";
        }
    }
}

void VimHandler::treeCloseOrAscend()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if(!tree)
        return;

    const QModelIndex cur = tree->currentIndex();
    if(!cur.isValid())
        return;

    if(tree->isExpanded(cur)) {
        qCDebug(VIM_LOG) << "treeCloseOrAscend: collapsing row=" << cur.row();
        tree->collapse(cur);
    }
    else {
        const QModelIndex parent = cur.parent();
        if(parent.isValid()) {
            qCDebug(VIM_LOG) << "treeCloseOrAscend: ascending to parent";
            tree->selectionModel()->setCurrentIndex(parent,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        else {
            qCDebug(VIM_LOG) << "treeCloseOrAscend: already at root, no-op";
        }
    }
}

void VimHandler::organiserCreatePlaylist()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if(viewContext(tree) != ViewContext::PlaylistOrganiser || !tree || !tree->model())
        return;

    QModelIndex parent = tree->currentIndex();
    if(parent.isValid() && !tree->model()->hasChildren(parent))
        parent = parent.parent();

    const int insertRow = tree->model()->rowCount(parent);
    triggerCurrentContextAction(Fooyin::Id(Constants::Actions::NewPlaylist));
    scheduleOrganiserInsertedSelection(tree, parent, insertRow);
}

void VimHandler::organiserCreateGroup()
{
    auto* tree = asTreeView(m_viewLocator->activeView());
    if(viewContext(tree) != ViewContext::PlaylistOrganiser || !tree || !tree->model())
        return;

    QModelIndex parent = tree->currentIndex();
    if(parent.isValid() && !tree->model()->hasChildren(parent))
        parent = parent.parent();

    const int insertRow = tree->model()->rowCount(parent);
    triggerCurrentContextAction(Fooyin::Id("PlaylistOrganiser.NewGroup"));
    scheduleOrganiserInsertedSelection(tree, parent, insertRow);
}

// ---------------------------------------------------------------------------
// Visual selection
// ---------------------------------------------------------------------------

void VimHandler::updateVisualSelection()
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model() || !view->selectionModel()) {
        qCWarning(VIM_LOG) << "updateVisualSelection: no active view / model / selectionModel";
        return;
    }
    const int cols = view->model()->columnCount();
    const int top  = qMin(m_visualAnchor, m_visualCursor);
    const int bot  = qMax(m_visualAnchor, m_visualCursor);
    qCDebug(VIM_LOG) << "updateVisualSelection: rows [" << top << "," << bot << "]"
                     << "anchor=" << m_visualAnchor << "cursor=" << m_visualCursor << "cols=" << cols;
    QItemSelection sel;
    sel.select(view->model()->index(top, 0), view->model()->index(bot, qMax(0, cols - 1)));
    view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    // Use NoUpdate so moving the current-item indicator does not implicitly
    // clear the multi-row selection we just applied.
    const int cursorCol = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    view->selectionModel()->setCurrentIndex(view->model()->index(m_visualCursor, cursorCol),
                                            QItemSelectionModel::NoUpdate);
}

// ---------------------------------------------------------------------------
// PlaylistHandler wiring
// ---------------------------------------------------------------------------

void VimHandler::setPlaylistHandler(Fooyin::PlaylistHandler* handler)
{
    qCDebug(VIM_LOG) << "setPlaylistHandler:" << (handler ? "set" : "cleared");
    m_playlistHandler = handler;
}

void VimHandler::setTrackSelectionController(Fooyin::TrackSelectionController* controller)
{
    qCDebug(VIM_LOG) << "setTrackSelectionController:" << (controller ? "set" : "cleared");
    m_trackSelectionController = controller;
}

void VimHandler::setCurrentPlaylistController(Fooyin::CurrentPlaylistController* controller)
{
    qCDebug(VIM_LOG) << "setCurrentPlaylistController:" << (controller ? "set" : "cleared");

    if(m_playlistSelectionChangedConnection)
        QObject::disconnect(m_playlistSelectionChangedConnection);

    m_currentPlaylistController  = controller;
    m_observedSelectedPlaylistId = {};

    if(!m_currentPlaylistController)
        return;

    m_observedSelectedPlaylistId = m_currentPlaylistController->currentPlaylistId();
    m_playlistSelectionChangedConnection
        = QObject::connect(m_currentPlaylistController, &Fooyin::CurrentPlaylistController::currentPlaylistChanged,
                           this, [this](Fooyin::Playlist* previous, Fooyin::Playlist* current) {
                               qCDebug(VIM_LOG) << "playlistSelectionChanged: previous="
                                                << (previous ? previous->name() : QStringLiteral("<null>"))
                                                << "current=" << (current ? current->name() : QStringLiteral("<null>"))
                                                << "activeViewContext=" << static_cast<int>(activeViewContext());
                               savePlaylistCursorState(previous);
                               m_observedSelectedPlaylistId = current ? current->id() : Fooyin::UId{};
                               restorePlaylistCursorState(current);
                           });
}

void VimHandler::setActionManager(Fooyin::ActionManager* manager)
{
    qCDebug(VIM_LOG) << "setActionManager:" << (manager ? "set" : "cleared");
    m_actionManager = manager;
}

void VimHandler::beginSetMark()
{
    qCDebug(VIM_LOG) << "beginSetMark";
    setPendingMarkOp(PendingMarkOp::Set);
    m_count = 0;
}

void VimHandler::beginJumpToMark()
{
    qCDebug(VIM_LOG) << "beginJumpToMark";
    setPendingMarkOp(PendingMarkOp::Jump);
    m_count = 0;
}

bool VimHandler::handlePendingMarkOp(QKeyEvent* ev)
{
    if(m_pendingMarkOp == PendingMarkOp::None)
        return false;

    if(ev->key() == Qt::Key_Escape && ev->modifiers() == Qt::NoModifier) {
        qCDebug(VIM_LOG) << "handlePendingMarkOp: cancelled";
        clearPendingInputState();
        return true;
    }

    const QChar ch = ev->text().isEmpty() ? QChar{} : ev->text().front();
    if(ev->modifiers() == Qt::NoModifier && !ch.isNull() && ch.isLower()) {
        const PendingMarkOp op = m_pendingMarkOp;
        clearPendingInputState();
        if(op == PendingMarkOp::Set)
            setLocalMark(ch);
        else
            jumpToLocalMark(ch);
        return true;
    }

    qCDebug(VIM_LOG) << "handlePendingMarkOp: ignored non-lowercase completion";
    clearPendingInputState();
    return true;
}

void VimHandler::focusNowPlaying()
{
    qCDebug(VIM_LOG) << "focusNowPlaying";
    if(!m_actionManager) {
        qCWarning(VIM_LOG) << "focusNowPlaying: no ActionManager";
        return;
    }

    Fooyin::Command* cmd = m_actionManager->command(Fooyin::Id(Constants::Actions::ShowNowPlaying));
    if(!cmd || !cmd->action()) {
        qCWarning(VIM_LOG) << "focusNowPlaying: ShowNowPlaying action not found";
        return;
    }

    cmd->action()->trigger();
}

void VimHandler::nextPlaylist()
{
    changePlaylistByOffset((m_count > 0 || m_dispatchCount > 0) ? currentCount() : 1);
}

void VimHandler::previousPlaylist()
{
    changePlaylistByOffset(-((m_count > 0 || m_dispatchCount > 0) ? currentCount() : 1));
}

void VimHandler::copyAfterCurrentPlaying()
{
    insertSelectionAfterCurrentPlaying(false);
}

void VimHandler::moveAfterCurrentPlaying()
{
    insertSelectionAfterCurrentPlaying(true);
}

void VimHandler::triggerFooyinAction(const QStringView& actionId)
{
    if(actionId.isEmpty()) {
        qCWarning(VIM_LOG) << "triggerFooyinAction: empty action id";
        return;
    }

    triggerCurrentContextAction(Fooyin::Id(actionId.toString()));
}

Fooyin::Playlist* VimHandler::selectedPlaylist() const
{
    if(!m_playlistHandler)
        return nullptr;

    auto* view = m_viewLocator->activeView();
    if(viewContext(view) != ViewContext::PlaylistView)
        return nullptr;

    if(m_trackSelectionController) {
        if(const auto* selection = m_trackSelectionController->selectedSelection();
           selection && selection->playlistId && selection->playlistId->isValid()) {
            if(auto* playlist = m_playlistHandler->playlistById(*selection->playlistId)) {
                qCDebug(VIM_LOG) << "selectedPlaylist: from TrackSelection =" << playlist->name();
                return playlist;
            }
        }
    }

    return nullptr;
}

Fooyin::UId VimHandler::currentTrackEntryId() const
{
    if(activeViewContext() != ViewContext::PlaylistView)
        return {};

    if(auto* playlist = selectedPlaylist()) {
        if(m_trackSelectionController) {
            if(const auto* selection = m_trackSelectionController->selectedSelection();
               selection && selection->playlistId && *selection->playlistId == playlist->id()
               && !selection->playlistEntryIds.empty()) {
                const Fooyin::UId entryId = selection->playlistEntryIds.front();
                if(entryId.isValid())
                    return entryId;
            }
        }
    }

    auto* view     = m_viewLocator->activeView();
    auto* playlist = targetPlaylist();
    if(!view || !view->currentIndex().isValid() || !playlist)
        return {};

    if(const auto playlistTrack = playlist->playlistTrack(view->currentIndex().row()))
        return playlistTrack->entryId;

    return {};
}

std::optional<std::pair<int, int>> VimHandler::selectedTrackRowRange(Fooyin::Playlist* playlist)
{
    if(!playlist)
        return std::nullopt;

    const int trackCount = playlist->trackCount();
    if(trackCount <= 0)
        return std::nullopt;

    if(m_mode == Mode::Visual && m_visualAnchor >= 0 && m_visualCursor >= 0) {
        const int start = std::clamp(qMin(m_visualAnchor, m_visualCursor), 0, trackCount - 1);
        const int end   = std::clamp(qMax(m_visualAnchor, m_visualCursor) + 1, start + 1, trackCount);
        return std::pair{start, end};
    }

    auto* view    = m_viewLocator->activeView();
    const int row = (view && view->currentIndex().isValid()) ? view->currentIndex().row() : 0;
    if(row < 0 || row >= trackCount)
        return std::nullopt;

    const int end = std::clamp(row + currentCount(), row + 1, trackCount);
    return std::pair{row, end};
}

void VimHandler::setLocalMark(QChar mark)
{
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "setLocalMark: no playlist found";
        return;
    }

    const Fooyin::UId entryId = currentTrackEntryId();
    if(!entryId.isValid()) {
        qCWarning(VIM_LOG) << "setLocalMark: no current playlist entry";
        return;
    }

    m_localMarks[playlist->id()][mark] = entryId;
    qCDebug(VIM_LOG) << "setLocalMark:" << mark << "playlist=" << playlist->name() << "entryId=" << entryId;
}

void VimHandler::jumpToLocalMark(QChar mark)
{
    auto* playlist = targetPlaylist();
    auto* view     = m_viewLocator->activeView();
    if(!playlist || !view || !view->model() || !view->selectionModel()) {
        qCWarning(VIM_LOG) << "jumpToLocalMark: no playlist/view context";
        return;
    }

    const auto playlistMarks = m_localMarks.constFind(playlist->id());
    if(playlistMarks == m_localMarks.cend() || !playlistMarks->contains(mark)) {
        qCDebug(VIM_LOG) << "jumpToLocalMark: mark not set" << mark;
        return;
    }

    const Fooyin::UId entryId = playlistMarks->value(mark);
    const int row             = playlist->indexOfTrackEntry(entryId);
    if(row < 0) {
        qCDebug(VIM_LOG) << "jumpToLocalMark: stale mark cleared" << mark;
        m_localMarks[playlist->id()].remove(mark);
        if(m_localMarks[playlist->id()].isEmpty())
            m_localMarks.remove(playlist->id());
        return;
    }

    const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const QModelIndex idx = view->model()->index(row, col);
    if(!idx.isValid())
        return;

    if(m_mode == Mode::Visual) {
        qCDebug(VIM_LOG) << "jumpToLocalMark (visual):" << mark << "row=" << row;
        m_visualCursor = row;
        updateVisualSelection();
        view->scrollTo(idx);
        return;
    }

    view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(idx);
    qCDebug(VIM_LOG) << "jumpToLocalMark:" << mark << "row=" << row;
}

std::vector<VimClipboard::MarkTransfer> VimHandler::takeCutMarks(Fooyin::Playlist* playlist, int startRow, int endRow)
{
    std::vector<VimClipboard::MarkTransfer> transfers;
    if(!playlist || startRow >= endRow)
        return transfers;

    auto playlistIt = m_localMarks.find(playlist->id());
    if(playlistIt == m_localMarks.end())
        return transfers;

    auto& marks = playlistIt.value();
    for(auto it = marks.begin(); it != marks.end();) {
        const int row = playlist->indexOfTrackEntry(it.value());
        if(row >= startRow && row < endRow) {
            transfers.push_back({row - startRow, it.key()});
            it = marks.erase(it);
        }
        else {
            ++it;
        }
    }

    if(marks.isEmpty())
        m_localMarks.erase(playlistIt);

    return transfers;
}

Fooyin::Playlist* VimHandler::targetPlaylist() const
{
    if(!m_playlistHandler)
        return nullptr;

    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "targetPlaylist: active view is not a playlist view";
        return nullptr;
    }

    if(auto* playlist = selectedPlaylist())
        return playlist;

    if(m_currentPlaylistController && m_observedSelectedPlaylistId.isValid()) {
        if(auto* playlist = m_playlistHandler->playlistById(m_observedSelectedPlaylistId)) {
            qCDebug(VIM_LOG) << "targetPlaylist: observed selected playlist =" << playlist->name();
            return playlist;
        }
    }

    // Prefer the playlist the active view is currently displaying, identified
    // by matching its row count. This ensures yank/paste target what the user
    // sees, not whatever happens to be playing in the background.
    auto* view         = m_viewLocator->activeView();
    const int viewRows = (view && view->model()) ? view->model()->rowCount() : -1;

    if(viewRows > 0) {
        for(auto* p : m_playlistHandler->playlists()) {
            if(p && p->trackCount() == viewRows) {
                qCDebug(VIM_LOG) << "targetPlaylist: matched by row count (" << viewRows << "):" << p->name();
                return p;
            }
        }
    }

    // Fall back: currently playing playlist.
    if(auto* p = m_playlistHandler->activePlaylist()) {
        qCDebug(VIM_LOG) << "targetPlaylist: active (playing):" << p->name();
        return p;
    }

    // Last resort: first available playlist.
    const auto all = m_playlistHandler->playlists();
    if(!all.empty()) {
        qCDebug(VIM_LOG) << "targetPlaylist: using first available:" << all.front()->name();
        return all.front();
    }

    qCWarning(VIM_LOG) << "targetPlaylist: no playlist found";
    return nullptr;
}

void VimHandler::changePlaylistByOffset(const int delta)
{
    if(delta == 0) {
        return;
    }

    if(!m_playlistHandler || !m_currentPlaylistController) {
        qCWarning(VIM_LOG) << "changePlaylistByOffset: missing playlist services";
        return;
    }

    const Fooyin::UId currentId = m_currentPlaylistController->currentPlaylistId();
    if(!currentId.isValid()) {
        qCWarning(VIM_LOG) << "changePlaylistByOffset: no selected playlist";
        return;
    }

    auto* currentPlaylist = m_playlistHandler->playlistById(currentId);
    if(!currentPlaylist) {
        qCWarning(VIM_LOG) << "changePlaylistByOffset: selected playlist not found";
        return;
    }

    if(auto* organiserTree = findPlaylistOrganiserTree(*this)) {
        std::vector<Fooyin::Playlist*> organiserPlaylists;
        appendOrganiserPlaylists(organiserTree->model(), {}, organiserPlaylists);

        const auto currentIt = std::find(organiserPlaylists.cbegin(), organiserPlaylists.cend(), currentPlaylist);
        if(currentIt != organiserPlaylists.cend()) {
            const int currentSequenceIndex = static_cast<int>(std::distance(organiserPlaylists.cbegin(), currentIt));
            const int targetSequenceIndex
                = std::clamp(currentSequenceIndex + delta, 0, static_cast<int>(organiserPlaylists.size()) - 1);
            if(targetSequenceIndex == currentSequenceIndex) {
                qCDebug(VIM_LOG) << "changePlaylistByOffset: already at organiser boundary index"
                                 << currentSequenceIndex;
                return;
            }

            if(auto* targetPlaylist = organiserPlaylists[static_cast<size_t>(targetSequenceIndex)]) {
                qCDebug(VIM_LOG) << "changePlaylistByOffset organiser:" << currentPlaylist->name() << "->"
                                 << targetPlaylist->name() << "delta=" << delta;
                m_currentPlaylistController->changeCurrentPlaylist(targetPlaylist->id());
                return;
            }
        }
    }

    const int currentIndex = currentPlaylist->index();
    const int targetIndex  = std::clamp(currentIndex + delta, 0, m_playlistHandler->playlistCount() - 1);
    if(targetIndex == currentIndex) {
        qCDebug(VIM_LOG) << "changePlaylistByOffset: already at boundary index" << currentIndex;
        return;
    }

    if(auto* targetPlaylist = m_playlistHandler->playlistByIndex(targetIndex)) {
        qCDebug(VIM_LOG) << "changePlaylistByOffset:" << currentPlaylist->name() << "->" << targetPlaylist->name()
                         << "delta=" << delta;
        m_currentPlaylistController->changeCurrentPlaylist(targetPlaylist->id());
        return;
    }

    qCWarning(VIM_LOG) << "changePlaylistByOffset: target playlist missing at index" << targetIndex;
}

void VimHandler::insertSelectionAfterCurrentPlaying(const bool move)
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                         << ": ignored outside playlist view";
        return;
    }

    auto* view = m_viewLocator->activeView();
    if(!view || !m_playlistHandler) {
        qCWarning(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                           << ": no active view or no PlaylistHandler";
        return;
    }

    auto* sourcePlaylist      = targetPlaylist();
    auto* destinationPlaylist = m_playlistHandler->activePlaylist();
    if(!sourcePlaylist || !destinationPlaylist) {
        qCWarning(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                           << ": source or playing playlist missing";
        return;
    }

    const auto rowRange = selectedTrackRowRange(sourcePlaylist);
    if(!rowRange) {
        qCWarning(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                           << ": no valid selected rows";
        return;
    }

    const int playingIndex = destinationPlaylist->currentTrackIndex();
    if(playingIndex < 0) {
        qCWarning(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                           << ": no current playing track";
        return;
    }

    const int sourceStart = rowRange->first;
    const int sourceEnd   = rowRange->second;
    const int rowCount    = sourceEnd - sourceStart;
    if(rowCount <= 0) {
        return;
    }

    if(move && sourcePlaylist == destinationPlaylist && playingIndex >= sourceStart && playingIndex < sourceEnd) {
        qCDebug(VIM_LOG) << "moveAfterCurrentPlaying: selection already contains the playing track; no-op";
        return;
    }

    const int originalRow = view->currentIndex().isValid() ? view->currentIndex().row() : sourceStart;
    const int col         = view->currentIndex().isValid() ? view->currentIndex().column() : 0;

    const Fooyin::PlaylistTrackList sourceBefore = sourcePlaylist->playlistTracks();
    const Fooyin::PlaylistTrackList selectedEntries(sourceBefore.begin() + sourceStart,
                                                    sourceBefore.begin() + sourceEnd);
    Fooyin::PlaylistTrackList sourceAfter = sourceBefore;
    Fooyin::UId restoreEntryId;
    Fooyin::UId cursorBeforeEntryId;
    if(originalRow >= 0 && originalRow < static_cast<int>(sourceBefore.size())) {
        cursorBeforeEntryId = sourceBefore[static_cast<size_t>(originalRow)].entryId;
    }

    const Fooyin::TrackList selectedTracks = Fooyin::PlaylistTrack::toTracks(selectedEntries);
    const bool samePlaylist                = sourcePlaylist == destinationPlaylist;
    const Fooyin::PlaylistTrackList destinationBefore
        = samePlaylist ? sourceBefore : destinationPlaylist->playlistTracks();
    Fooyin::PlaylistTrackList destinationAfter = destinationBefore;

    std::vector<VimClipboard::MarkTransfer> cutMarks;
    if(move && !samePlaylist) {
        cutMarks = takeCutMarks(sourcePlaylist, sourceStart, sourceEnd);
    }
    if(move) {
        sourceAfter.erase(sourceAfter.begin() + sourceStart, sourceAfter.begin() + sourceEnd);
        if(!sourceAfter.empty()) {
            const int restoreIndex = std::min(sourceStart, static_cast<int>(sourceAfter.size()) - 1);
            restoreEntryId         = sourceAfter[static_cast<size_t>(restoreIndex)].entryId;
        }
        if(samePlaylist) {
            destinationAfter = sourceAfter;
        }
    }

    auto insertedEntries = move && samePlaylist
                             ? selectedEntries
                             : Fooyin::PlaylistTrack::fromTracks(selectedTracks, destinationPlaylist->id());
    for(const auto& transfer : cutMarks) {
        if(transfer.offset >= 0 && transfer.offset < static_cast<int>(insertedEntries.size())) {
            m_localMarks[destinationPlaylist->id()][transfer.mark]
                = insertedEntries[static_cast<size_t>(transfer.offset)].entryId;
        }
    }

    int adjustedPlayingIndex = playingIndex;
    if(move && samePlaylist && sourceStart <= playingIndex) {
        adjustedPlayingIndex -= rowCount;
    }
    const int insertPos = std::clamp(adjustedPlayingIndex + 1, 0, static_cast<int>(destinationAfter.size()));
    destinationAfter.insert(destinationAfter.begin() + insertPos, insertedEntries.begin(), insertedEntries.end());

    std::vector<PlaylistSnapshot> beforeSnapshots;
    std::vector<PlaylistSnapshot> afterSnapshots;

    beforeSnapshots.push_back({sourcePlaylist->id(), sourceBefore});
    if(!samePlaylist) {
        beforeSnapshots.push_back({destinationPlaylist->id(), destinationBefore});
    }

    if(samePlaylist) {
        afterSnapshots.push_back({sourcePlaylist->id(), destinationAfter});
    }
    else {
        if(move) {
            afterSnapshots.push_back({sourcePlaylist->id(), sourceAfter});
        }
        afterSnapshots.push_back({destinationPlaylist->id(), destinationAfter});
    }

    qCDebug(VIM_LOG) << (move ? "moveAfterCurrentPlaying" : "copyAfterCurrentPlaying")
                     << "sourcePlaylist=" << sourcePlaylist->name()
                     << "destinationPlaylist=" << destinationPlaylist->name() << "sourceRows=[" << sourceStart << ","
                     << (sourceEnd - 1) << "] insertPos=" << insertPos << "samePlaylist=" << samePlaylist
                     << "cursorBeforeEntryId=" << cursorBeforeEntryId << "restoreEntryId=" << restoreEntryId;

    applyPlaylistSnapshots(afterSnapshots);

    const int rowCountBefore = static_cast<int>(sourceBefore.size());
    const int rowCountAfter
        = samePlaylist ? static_cast<int>(destinationAfter.size()) : static_cast<int>(sourceAfter.size());
    int restoreRow = rowCountAfter > 0 ? qMin(originalRow, rowCountAfter - 1) : -1;
    if(move) {
        if(!restoreEntryId.isValid()) {
            restoreRow = -1;
        }
        else {
            const auto findRowByEntryId = [&restoreEntryId](const Fooyin::PlaylistTrackList& tracks) {
                for(int i = 0; i < static_cast<int>(tracks.size()); ++i) {
                    if(tracks[static_cast<size_t>(i)].entryId == restoreEntryId)
                        return i;
                }
                return -1;
            };

            restoreRow = samePlaylist ? findRowByEntryId(destinationAfter) : findRowByEntryId(sourceAfter);
        }
    }

    pushUndoEntry(std::move(beforeSnapshots), std::move(afterSnapshots), originalRow, restoreRow, col, rowCountBefore,
                  rowCountAfter, std::move(cursorBeforeEntryId), std::move(restoreEntryId));

    if(m_mode == Mode::Visual) {
        enterNormal();
    }

    if(restoreRow >= 0) {
        const Fooyin::UId restorePlaylistId = sourcePlaylist->id();
        scheduleEntryRestore(view, restorePlaylistId, restoreEntryId, restoreRow, col, rowCountAfter);
    }
}

void VimHandler::scheduleIndexRestore(QAbstractItemView* view, int row, int col, int expectedRowCount)
{
    if(!view || !view->model() || !view->selectionModel())
        return;

    QPointer<QAbstractItemView> viewPtr{view};

    qCDebug(VIM_LOG) << "scheduleIndexRestore: scheduling restore to row=" << row << "col=" << col
                     << "expectedRowCount=" << expectedRowCount << "view=" << view->metaObject()->className();

    auto tryRestore = [viewPtr, row, col]() -> bool {
        if(!viewPtr || !viewPtr->model() || !viewPtr->selectionModel())
            return false;
        if(viewPtr->model()->rowCount() <= row)
            return false;
        const QModelIndex idx = viewPtr->model()->index(row, col);
        if(!idx.isValid())
            return false;
        const int currentRow = viewPtr->currentIndex().isValid() ? viewPtr->currentIndex().row() : -1;
        qCDebug(VIM_LOG) << "scheduleIndexRestore: cursor restoring row" << row << "(current was" << currentRow << ")";
        viewPtr->selectionModel()->setCurrentIndex(idx,
                                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        qCDebug(VIM_LOG) << "scheduleIndexRestore: cursor restored to row" << row;
        return true;
    };

    // Attempt 1: after the sync in-memory modelReset's handlers complete.
    QTimer::singleShot(0, this, [tryRestore]() { tryRestore(); });

    // Attempt 2: fooyin often defers the model reset (resetModelThrottled) and
    // populates the model asynchronously via a populator thread.  The model emits
    // modelReset (0 rows), then rowsInserted as groups are populated.  Wait for
    // rowCount to reach expectedRowCount before restoring.
    auto restoreAfterPopulation = [viewPtr, tryRestore, expectedRowCount]() {
        if(!viewPtr || !viewPtr->model())
            return;
        qCDebug(VIM_LOG) << "scheduleIndexRestore: modelReset caught, waiting for rowsInserted"
                         << "target rowCount=" << expectedRowCount;

        auto guard = std::make_shared<bool>(false);
        QObject::connect(viewPtr->model(), &QAbstractItemModel::rowsInserted, viewPtr,
                         [viewPtr, tryRestore, expectedRowCount, guard]() {
                             if(!viewPtr || !viewPtr->model() || *guard)
                                 return;
                             const int rc = viewPtr->model()->rowCount();
                             if(rc < expectedRowCount)
                                 return;
                             qCDebug(VIM_LOG)
                                 << "scheduleIndexRestore: rowsInserted, rowCount=" << rc << "view currentIndex row="
                                 << (viewPtr->currentIndex().isValid() ? QString::number(viewPtr->currentIndex().row())
                                                                       : QStringLiteral("invalid"));
                             tryRestore();
                             *guard = true;
                         });
    };

    QObject::connect(view->model(), &QAbstractItemModel::modelReset, view->model(), restoreAfterPopulation,
                     Qt::SingleShotConnection);
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void VimHandler::pushUndoEntry(Fooyin::UId playlistId, Fooyin::PlaylistTrackList before,
                               Fooyin::PlaylistTrackList after, int cursorBefore, int cursorAfter, int col)
{
    const int rowCountBefore = static_cast<int>(before.size());
    const int rowCountAfter  = static_cast<int>(after.size());
    std::vector<PlaylistSnapshot> beforeSnapshots;
    std::vector<PlaylistSnapshot> afterSnapshots;
    beforeSnapshots.push_back({playlistId, std::move(before)});
    afterSnapshots.push_back({playlistId, std::move(after)});
    pushUndoEntry(std::move(beforeSnapshots), std::move(afterSnapshots), cursorBefore, cursorAfter, col, rowCountBefore,
                  rowCountAfter);
}

void VimHandler::pushUndoEntry(std::vector<PlaylistSnapshot> before, std::vector<PlaylistSnapshot> after,
                               const int cursorBefore, const int cursorAfter, const int col, const int rowCountBefore,
                               const int rowCountAfter, Fooyin::UId cursorBeforeEntryId, Fooyin::UId cursorAfterEntryId)
{
    if(m_undoIndex + 1 < static_cast<int>(m_undoStack.size()))
        m_undoStack.resize(static_cast<size_t>(m_undoIndex + 1));
    m_undoStack.push_back({std::move(before), std::move(after), cursorBefore, cursorAfter, col, rowCountBefore,
                           rowCountAfter, std::move(cursorBeforeEntryId), std::move(cursorAfterEntryId)});
    m_undoIndex = static_cast<int>(m_undoStack.size()) - 1;
    qCDebug(VIM_LOG) << "pushUndoEntry: undoIndex=" << m_undoIndex << "stackSize=" << m_undoStack.size();
}

void VimHandler::applyPlaylistSnapshots(const std::vector<PlaylistSnapshot>& snapshots) const
{
    if(!m_playlistHandler)
        return;

    for(const auto& snapshot : snapshots) {
        m_playlistHandler->replacePlaylistTracks(snapshot.playlistId, snapshot.tracks,
                                                 Fooyin::PlaylistTrackChangeSource::History);
    }
}

void VimHandler::scheduleEntryRestore(QAbstractItemView* view, const Fooyin::UId& playlistId,
                                      const Fooyin::UId& entryId, const int fallbackRow, const int col,
                                      const int expectedRowCount)
{
    if(!view || !view->model() || !view->selectionModel())
        return;

    if(!entryId.isValid() || !m_playlistHandler) {
        scheduleIndexRestore(view, fallbackRow, col, expectedRowCount);
        return;
    }

    auto resolveRow = [this, playlistId, entryId, fallbackRow]() {
        if(auto* playlist = m_playlistHandler->playlistById(playlistId)) {
            const int row = playlist->indexOfTrackEntry(entryId);
            if(row >= 0)
                return row;
        }
        return fallbackRow;
    };

    QPointer<QAbstractItemView> viewPtr{view};

    auto tryRestore = [viewPtr, resolveRow, col]() -> bool {
        if(!viewPtr || !viewPtr->model() || !viewPtr->selectionModel())
            return false;

        const int row = resolveRow();
        if(row < 0 || viewPtr->model()->rowCount() <= row)
            return false;

        const QModelIndex idx = viewPtr->model()->index(row, col);
        if(!idx.isValid())
            return false;

        const int currentRow = viewPtr->currentIndex().isValid() ? viewPtr->currentIndex().row() : -1;
        qCDebug(VIM_LOG) << "scheduleEntryRestore: cursor restoring row" << row << "(current was" << currentRow << ")";
        viewPtr->selectionModel()->setCurrentIndex(idx,
                                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        qCDebug(VIM_LOG) << "scheduleEntryRestore: cursor restored to row" << row;
        return true;
    };

    qCDebug(VIM_LOG) << "scheduleEntryRestore: playlistId=" << playlistId << "entryId=" << entryId
                     << "resolvedRow=" << resolveRow() << "fallbackRow=" << fallbackRow
                     << "expectedRowCount=" << expectedRowCount;

    QTimer::singleShot(0, this, [tryRestore]() { tryRestore(); });

    auto restoreAfterPopulation = [viewPtr, tryRestore, expectedRowCount]() {
        if(!viewPtr || !viewPtr->model())
            return;

        qCDebug(VIM_LOG) << "scheduleEntryRestore: modelReset caught, waiting for rowsInserted"
                         << "target rowCount=" << expectedRowCount;

        // Some model resets repopulate synchronously before any rowsInserted
        // signals are emitted. Try again on the next event-loop turn first.
        QTimer::singleShot(0, viewPtr, [viewPtr, tryRestore, expectedRowCount]() {
            if(!viewPtr || !viewPtr->model())
                return;

            const int rc = viewPtr->model()->rowCount();
            qCDebug(VIM_LOG) << "scheduleEntryRestore: post-reset retry, rowCount=" << rc;
            if(rc >= expectedRowCount)
                tryRestore();
        });

        if(viewPtr->model()->rowCount() >= expectedRowCount) {
            qCDebug(VIM_LOG) << "scheduleEntryRestore: model already repopulated after reset";
            tryRestore();
            return;
        }

        auto guard = std::make_shared<bool>(false);
        QObject::connect(viewPtr->model(), &QAbstractItemModel::rowsInserted, viewPtr,
                         [viewPtr, tryRestore, expectedRowCount, guard]() {
                             if(!viewPtr || !viewPtr->model() || *guard)
                                 return;

                             const int rc = viewPtr->model()->rowCount();
                             if(rc < expectedRowCount)
                                 return;

                             qCDebug(VIM_LOG) << "scheduleEntryRestore: rowsInserted, rowCount=" << rc;
                             tryRestore();
                             *guard = true;
                         });
    };

    QObject::connect(view->model(), &QAbstractItemModel::modelReset, view->model(), restoreAfterPopulation,
                     Qt::SingleShotConnection);
}

void VimHandler::undo()
{
    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "undo: no PlaylistHandler";
        return;
    }
    if(m_undoIndex < 0) {
        qCDebug(VIM_LOG) << "undo: stack empty";
        return;
    }
    const UndoEntry& entry = m_undoStack[static_cast<size_t>(m_undoIndex)];
    qCDebug(VIM_LOG) << "undo: index=" << m_undoIndex << "playlistCount=" << entry.before.size()
                     << "cursorBefore=" << entry.cursorBefore;
    applyPlaylistSnapshots(entry.before);
    auto* view = m_viewLocator->activeView();
    if(view && entry.cursorBefore >= 0) {
        const Fooyin::UId playlistId = !entry.before.empty() ? entry.before.front().playlistId : Fooyin::UId{};
        scheduleEntryRestore(view, playlistId, entry.cursorBeforeEntryId, entry.cursorBefore, entry.col,
                             entry.rowCountBefore);
    }
    --m_undoIndex;
}

void VimHandler::redo()
{
    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "redo: no PlaylistHandler";
        return;
    }
    if(m_undoIndex + 1 >= static_cast<int>(m_undoStack.size())) {
        qCDebug(VIM_LOG) << "redo: nothing to redo";
        return;
    }
    ++m_undoIndex;
    const UndoEntry& entry = m_undoStack[static_cast<size_t>(m_undoIndex)];
    qCDebug(VIM_LOG) << "redo: index=" << m_undoIndex << "playlistCount=" << entry.after.size()
                     << "cursorAfter=" << entry.cursorAfter;
    applyPlaylistSnapshots(entry.after);
    auto* view = m_viewLocator->activeView();
    if(view && entry.cursorAfter >= 0) {
        const Fooyin::UId playlistId = !entry.after.empty() ? entry.after.front().playlistId : Fooyin::UId{};
        scheduleEntryRestore(view, playlistId, entry.cursorAfterEntryId, entry.cursorAfter, entry.col,
                             entry.rowCountAfter);
    }
}

// ---------------------------------------------------------------------------
// Yank / delete / paste  (playlist views only; no-op for other view types)
// ---------------------------------------------------------------------------

void VimHandler::yankRows(int count)
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "yankRows: ignored outside playlist view";
        return;
    }

    auto* view = m_viewLocator->activeView();
    if(!view || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "yankRows: no active view or no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "yankRows: no playlist found";
        return;
    }
    const int row         = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const auto& all       = playlist->tracks();
    const int end         = std::clamp(row + count, 0, static_cast<int>(all.size()));
    const int actualCount = end - row;
    qCDebug(VIM_LOG) << "yankRows: playlist=" << playlist->name() << "startRow=" << row << "requestedCount=" << count
                     << "actualCount=" << actualCount;
    m_clipboard.yank(Fooyin::TrackList(all.begin() + row, all.begin() + end));

    // Re-assert the selection so the cursor row stays highlighted as the paste target.
    if(view->selectionModel() && view->currentIndex().isValid())
        view->selectionModel()->setCurrentIndex(view->currentIndex(),
                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VimHandler::deleteRows(int count)
{
    if(activeViewContext() == ViewContext::PlaylistOrganiser) {
        qCDebug(VIM_LOG) << "deleteRows: routing delete to playlist organiser";
        triggerCurrentContextAction(Fooyin::Id(Constants::Actions::Remove));
        return;
    }

    auto* view = m_viewLocator->activeView();
    if(!view || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "deleteRows: no active view or no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "deleteRows: no playlist found";
        return;
    }
    const int row          = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col          = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int beforeCount  = playlist->trackCount();
    const int end          = std::min(row + count, beforeCount);
    const int numDeleted   = end - row;
    const int expectedRows = beforeCount - numDeleted;
    const int restoreRow   = (expectedRows > 0) ? qMin(row, expectedRows - 1) : 0;

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    const auto cutMarks                      = takeCutMarks(playlist, row, end);

    qCDebug(VIM_LOG) << "deleteRows: playlist=" << playlist->name() << "rows [" << row << "," << (end - 1) << "]"
                     << "count=" << numDeleted << "restoreRow=" << restoreRow;
    m_clipboard.cut(Fooyin::TrackList(playlist->tracks().begin() + row, playlist->tracks().begin() + end), cutMarks);
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(numDeleted));
    for(int i = row; i < end; ++i)
        indices.push_back(i);
    m_playlistHandler->removePlaylistTracks(playlist->id(), indices);

    Fooyin::PlaylistTrackList snapshotAfter = snapshotBefore;
    snapshotAfter.erase(snapshotAfter.begin() + row, snapshotAfter.begin() + end);
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(snapshotAfter), row, restoreRow, col);

    if(expectedRows > 0)
        scheduleIndexRestore(view, restoreRow, col, expectedRows);
}

void VimHandler::yankVisualSelection()
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "yankVisualSelection: ignored outside playlist view";
        return;
    }

    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "yankVisualSelection: no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "yankVisualSelection: no playlist found";
        return;
    }
    const int top   = qMin(m_visualAnchor, m_visualCursor);
    const int bot   = qMax(m_visualAnchor, m_visualCursor);
    const auto& all = playlist->tracks();
    const int end   = std::min(bot + 1, static_cast<int>(all.size()));
    if(top >= static_cast<int>(all.size())) {
        qCWarning(VIM_LOG) << "yankVisualSelection: selection out of range"
                           << "(top=" << top << "size=" << all.size() << ")";
        return;
    }
    qCDebug(VIM_LOG) << "yankVisualSelection: playlist=" << playlist->name() << "rows [" << top << "," << (end - 1)
                     << "]"
                     << "trackCount=" << (end - top);
    m_clipboard.yank(Fooyin::TrackList(all.begin() + top, all.begin() + end));
}

void VimHandler::deleteVisualSelection()
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "deleteVisualSelection: ignored outside playlist view";
        return;
    }

    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "deleteVisualSelection: no PlaylistHandler";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "deleteVisualSelection: no active playlist";
        return;
    }
    const int top  = qMin(m_visualAnchor, m_visualCursor);
    const int bot  = qMax(m_visualAnchor, m_visualCursor);
    const int size = playlist->trackCount();

    auto* view             = m_viewLocator->activeView();
    const int col          = (view && view->currentIndex().isValid()) ? view->currentIndex().column() : 0;
    const int numDeleted   = std::min(bot, size - 1) - top + 1;
    const int expectedRows = size - numDeleted;
    const int restoreRow   = (expectedRows > 0) ? qMin(top, expectedRows - 1) : 0;

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    const auto cutMarks                      = takeCutMarks(playlist, top, std::min(bot + 1, size));

    qCDebug(VIM_LOG) << "deleteVisualSelection: playlist=" << playlist->name() << "rows [" << top << ","
                     << qMin(bot, size - 1) << "]";
    m_clipboard.cut(Fooyin::TrackList(playlist->tracks().begin() + top, playlist->tracks().begin() + top + numDeleted),
                    cutMarks);
    std::vector<int> indices;
    for(int i = top; i <= bot && i < size; ++i)
        indices.push_back(i);
    m_playlistHandler->removePlaylistTracks(playlist->id(), indices);

    Fooyin::PlaylistTrackList snapshotAfter = snapshotBefore;
    snapshotAfter.erase(snapshotAfter.begin() + top, snapshotAfter.begin() + std::min(bot + 1, size));
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(snapshotAfter), m_visualCursor, restoreRow, col);

    enterNormal();

    if(view && expectedRows > 0) {
        scheduleIndexRestore(view, restoreRow, col, expectedRows);
    }
}

void VimHandler::pasteAfter()
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "pasteAfter: ignored outside playlist view";
        return;
    }

    if(!m_clipboard.hasData() || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "pasteAfter: clipboard empty or no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if(!view) {
        qCWarning(VIM_LOG) << "pasteAfter: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "pasteAfter: no active playlist";
        return;
    }
    const int originalRow                    = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col                            = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    const int targetRow                      = originalRow + 1;
    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all            = snapshotBefore;
    const int insertPos                      = std::clamp(targetRow, 0, static_cast<int>(all.size()));
    auto newEntries                          = Fooyin::PlaylistTrack::fromTracks(m_clipboard.tracks(), playlist->id());
    const auto markTransfers                 = m_clipboard.markTransfers();
    for(const auto& transfer : markTransfers) {
        if(transfer.offset >= 0 && transfer.offset < static_cast<int>(newEntries.size()))
            m_localMarks[playlist->id()][transfer.mark] = newEntries[static_cast<size_t>(transfer.offset)].entryId;
    }
    qCDebug(VIM_LOG) << "pasteAfter: playlist=" << playlist->name() << "originalRow=" << originalRow
                     << "insertPos=" << insertPos << "trackCount=" << newEntries.size();
    all.insert(all.begin() + insertPos, newEntries.begin(), newEntries.end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    if(!markTransfers.empty())
        m_clipboard.clearMarkTransfers();
    const int afterSize              = static_cast<int>(all.size());
    const int currentRowAfterReplace = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    qCDebug(VIM_LOG) << "pasteAfter: after replacePlaylistTracks, currentIndex row =" << currentRowAfterReplace
                     << "(expected originalRow=" << originalRow << ")";
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all), originalRow, originalRow, col);
    scheduleIndexRestore(view, originalRow, col, afterSize);
}

void VimHandler::pasteBefore()
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "pasteBefore: ignored outside playlist view";
        return;
    }

    if(!m_clipboard.hasData() || !m_playlistHandler) {
        qCWarning(VIM_LOG) << "pasteBefore: clipboard empty or no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if(!view) {
        qCWarning(VIM_LOG) << "pasteBefore: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "pasteBefore: no active playlist";
        return;
    }
    const int originalRow                    = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col                            = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all            = snapshotBefore;
    const int insertPos                      = std::clamp(originalRow, 0, static_cast<int>(all.size()));
    auto newEntries                          = Fooyin::PlaylistTrack::fromTracks(m_clipboard.tracks(), playlist->id());
    const auto markTransfers                 = m_clipboard.markTransfers();
    for(const auto& transfer : markTransfers) {
        if(transfer.offset >= 0 && transfer.offset < static_cast<int>(newEntries.size()))
            m_localMarks[playlist->id()][transfer.mark] = newEntries[static_cast<size_t>(transfer.offset)].entryId;
    }
    qCDebug(VIM_LOG) << "pasteBefore: playlist=" << playlist->name() << "originalRow=" << originalRow
                     << "insertPos=" << insertPos << "trackCount=" << newEntries.size();
    all.insert(all.begin() + insertPos, newEntries.begin(), newEntries.end());
    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    if(!markTransfers.empty())
        m_clipboard.clearMarkTransfers();
    const int afterSize              = static_cast<int>(all.size());
    const int currentRowAfterReplace = view->currentIndex().isValid() ? view->currentIndex().row() : -1;
    qCDebug(VIM_LOG) << "pasteBefore: after replacePlaylistTracks, currentIndex row =" << currentRowAfterReplace
                     << "(expected originalRow=" << originalRow << ")";
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all), originalRow, originalRow, col);
    scheduleIndexRestore(view, originalRow, col, afterSize);
}

// ---------------------------------------------------------------------------
// Row / selection move (playlist views only)
// ---------------------------------------------------------------------------

void VimHandler::moveRows(int delta)
{
    if(activeViewContext() == ViewContext::PlaylistOrganiser) {
        auto* tree = asTreeView(m_viewLocator->activeView());
        if(!tree || !tree->model() || !tree->selectionModel()) {
            qCWarning(VIM_LOG) << "moveRows: organiser has no active tree/model/selectionModel";
            return;
        }

        QModelIndex current = tree->currentIndex();
        if(!current.isValid()) {
            qCDebug(VIM_LOG) << "moveRows: organiser has no current index";
            return;
        }

        const int direction = delta < 0 ? -1 : +1;
        const int steps     = std::abs(delta);
        for(int step = 0; step < steps; ++step) {
            current = tree->currentIndex();
            if(!current.isValid())
                return;

            const QModelIndex sourceParent = current.parent();
            const int sourceRow            = current.row();
            const QPersistentModelIndex movedItem{current};

            auto* mimeData = tree->model()->mimeData({current});
            if(!mimeData) {
                qCWarning(VIM_LOG) << "moveRows: organiser mimeData unavailable";
                return;
            }

            std::unique_ptr<QMimeData> dragData{mimeData};
            QModelIndex candidate = direction < 0 ? tree->indexAbove(current) : tree->indexBelow(current);
            bool moved            = false;

            if(direction > 0 && current.parent().isValid()
               && current.row() == tree->model()->rowCount(current.parent()) - 1) {
                const QModelIndex currentParent = current.parent();
                const auto dropTarget           = OrganiserDropTarget{currentParent.parent(), currentParent.row() + 1};
                qCDebug(VIM_LOG) << "moveRows: organiser last-child move-out dropParent="
                                 << organiserIndexPath(dropTarget.parent) << "dropRow=" << dropTarget.row
                                 << "source=" << organiserIndexPath(current);
                if(tree->model()->canDropMimeData(dragData.get(), Qt::MoveAction, dropTarget.row, 0, dropTarget.parent)
                   && tree->model()->dropMimeData(dragData.get(), Qt::MoveAction, dropTarget.row, 0,
                                                  dropTarget.parent)) {
                    if(const QModelIndex movedIndex
                       = organiserMovedIndexAfterDrop(tree, sourceParent, sourceRow, dropTarget);
                       movedIndex.isValid()) {
                        tree->selectionModel()->setCurrentIndex(movedIndex, QItemSelectionModel::ClearAndSelect
                                                                                | QItemSelectionModel::Rows);
                        tree->scrollTo(movedIndex);

                        QPointer<QTreeView> treePtr{tree};
                        const QPersistentModelIndex movedPersistentIndex{movedItem};
                        const QPersistentModelIndex restoreIndex{movedIndex};
                        QTimer::singleShot(0, this, [treePtr, movedPersistentIndex, restoreIndex]() {
                            if(!treePtr || !treePtr->selectionModel())
                                return;

                            const QModelIndex targetIndex = movedPersistentIndex.isValid()
                                                              ? QModelIndex(movedPersistentIndex)
                                                              : QModelIndex(restoreIndex);
                            if(!targetIndex.isValid())
                                return;

                            treePtr->selectionModel()->setCurrentIndex(targetIndex, QItemSelectionModel::ClearAndSelect
                                                                                        | QItemSelectionModel::Rows);
                            treePtr->scrollTo(targetIndex);
                        });
                    }
                    return;
                }
            }

            while(candidate.isValid()) {
                if(direction > 0 && isAncestorIndex(current, candidate)) {
                    qCDebug(VIM_LOG) << "moveRows: organiser skipping descendant candidate"
                                     << organiserIndexPath(candidate) << "while moving" << organiserIndexPath(current);
                    candidate = tree->indexBelow(lastVisibleDescendant(tree, candidate));
                    continue;
                }

                const auto dropTarget = organiserDropTargetForVisibleMove(tree, current, candidate, direction);
                qCDebug(VIM_LOG) << "moveRows: organiser candidate=" << organiserIndexPath(candidate)
                                 << "dropParent=" << organiserIndexPath(dropTarget.parent)
                                 << "dropRow=" << dropTarget.row << "source=" << organiserIndexPath(current);
                if(tree->model()->canDropMimeData(dragData.get(), Qt::MoveAction, dropTarget.row, 0, dropTarget.parent)
                   && tree->model()->dropMimeData(dragData.get(), Qt::MoveAction, dropTarget.row, 0,
                                                  dropTarget.parent)) {
                    if(const QModelIndex movedIndex
                       = organiserMovedIndexAfterDrop(tree, sourceParent, sourceRow, dropTarget);
                       movedIndex.isValid()) {
                        tree->selectionModel()->setCurrentIndex(movedIndex, QItemSelectionModel::ClearAndSelect
                                                                                | QItemSelectionModel::Rows);
                        tree->scrollTo(movedIndex);

                        QPointer<QTreeView> treePtr{tree};
                        const QPersistentModelIndex movedPersistentIndex{movedItem};
                        const QPersistentModelIndex restoreIndex{movedIndex};
                        QTimer::singleShot(0, this, [treePtr, movedPersistentIndex, restoreIndex]() {
                            if(!treePtr || !treePtr->selectionModel())
                                return;

                            const QModelIndex targetIndex = movedPersistentIndex.isValid()
                                                              ? QModelIndex(movedPersistentIndex)
                                                              : QModelIndex(restoreIndex);
                            if(!targetIndex.isValid())
                                return;

                            treePtr->selectionModel()->setCurrentIndex(targetIndex, QItemSelectionModel::ClearAndSelect
                                                                                        | QItemSelectionModel::Rows);
                            treePtr->scrollTo(targetIndex);
                        });
                    }
                    moved = true;
                    break;
                }

                candidate = direction < 0 ? tree->indexAbove(candidate) : tree->indexBelow(candidate);
            }

            if(!moved) {
                qCDebug(VIM_LOG) << "moveRows: organiser has no valid visible-row move target";
                return;
            }
        }
        return;
    }

    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "moveRows: ignored outside playlist view";
        return;
    }

    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "moveRows: no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveRows: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "moveRows: no active playlist";
        return;
    }

    const int rowCount = view->model()->rowCount();
    if(rowCount == 0) {
        qCDebug(VIM_LOG) << "moveRows: view is empty";
        return;
    }
    const int row = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    const int col = view->currentIndex().isValid() ? view->currentIndex().column() : 0;

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all            = snapshotBefore;
    const Fooyin::PlaylistTrack moved        = all[static_cast<size_t>(row)];
    all.erase(all.begin() + row);
    const int insertPos = std::clamp(row + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveRows: playlist=" << playlist->name() << "row" << row << "->" << insertPos
                     << "(delta=" << delta << ")"
                     << "track=" << moved.track.title();
    all.insert(all.begin() + insertPos, moved);

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);
    const int afterSize = static_cast<int>(all.size());
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all), row, insertPos, col);
    scheduleIndexRestore(view, insertPos, col, afterSize);
}

void VimHandler::moveVisualSelection(int delta)
{
    if(activeViewContext() != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "moveVisualSelection: ignored outside playlist view";
        return;
    }

    if(!m_playlistHandler) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no PlaylistHandler";
        return;
    }
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no active view";
        return;
    }
    auto* playlist = targetPlaylist();
    if(!playlist) {
        qCWarning(VIM_LOG) << "moveVisualSelection: no active playlist";
        return;
    }

    const int top     = qMin(m_visualAnchor, m_visualCursor);
    const int bot     = qMax(m_visualAnchor, m_visualCursor);
    const int selSize = bot - top + 1;

    Fooyin::PlaylistTrackList snapshotBefore = playlist->playlistTracks();
    Fooyin::PlaylistTrackList all            = snapshotBefore;
    if(bot >= static_cast<int>(all.size())) {
        qCWarning(VIM_LOG) << "moveVisualSelection: selection out of range"
                           << "(bot=" << bot << "size=" << all.size() << ")";
        return;
    }

    Fooyin::PlaylistTrackList movedTracks(all.begin() + top, all.begin() + bot + 1);
    all.erase(all.begin() + top, all.begin() + bot + 1);
    const int insertPos = std::clamp(top + delta, 0, static_cast<int>(all.size()));
    qCDebug(VIM_LOG) << "moveVisualSelection: playlist=" << playlist->name() << "rows [" << top << "," << bot
                     << "] (size=" << selSize << ")"
                     << "->" << insertPos << "(delta=" << delta << ")";
    all.insert(all.begin() + insertPos, movedTracks.begin(), movedTracks.end());

    m_playlistHandler->replacePlaylistTracks(playlist->id(), all);

    const bool anchorFirst = (m_visualAnchor <= m_visualCursor);
    m_visualAnchor         = anchorFirst ? insertPos : insertPos + selSize - 1;
    m_visualCursor         = anchorFirst ? insertPos + selSize - 1 : insertPos;
    qCDebug(VIM_LOG) << "moveVisualSelection: new anchor=" << m_visualAnchor << "cursor=" << m_visualCursor;
    pushUndoEntry(playlist->id(), std::move(snapshotBefore), std::move(all), top, insertPos, 0);
    updateVisualSelection();
}

// ---------------------------------------------------------------------------
// Filter (Ctrl+I)
// ---------------------------------------------------------------------------

void VimHandler::enterFilter()
{
    auto* view = m_viewLocator->activeView();
    if(!view) {
        qCWarning(VIM_LOG) << "enterFilter: no active view";
        return;
    }

    auto* target = findEnclosingFyWidget(view);
    if(!target) {
        qCWarning(VIM_LOG) << "enterFilter: no enclosing FyWidget found";
        return;
    }
    m_filterTarget = target;

    if(!m_filterBar) {
        m_filterBar = new VimSearchBar();
        connect(m_filterBar, &VimSearchBar::textChanged, this, &VimHandler::onFilterTextChanged);
        connect(m_filterBar, &VimSearchBar::confirmed, this, &VimHandler::commitFilter);
        connect(m_filterBar, &VimSearchBar::cancelled, this, &VimHandler::cancelFilter);
    }

    m_filterBar->attachTo(view->window());
    m_filterBar->clear();
    if(!m_lastFilter.isEmpty())
        m_filterBar->prefillText(m_lastFilter);

    m_mode = Mode::Filter;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);

    m_filterBar->show();
    m_filterBar->setFocus();
    qCInfo(VIM_LOG) << "Mode → Filter";
}

void VimHandler::commitFilter()
{
    if(!m_filterBar)
        return;
    m_lastFilter = m_filterBar->text();
    m_filterBar->hide();

    if(m_filterTarget)
        m_filterTarget->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Filter committed: '" << m_lastFilter << "' → Normal";
}

void VimHandler::cancelFilter()
{
    if(m_filterTarget)
        m_filterTarget->searchEvent(Fooyin::SearchRequest{});
    m_lastFilter.clear();

    if(m_filterBar)
        m_filterBar->hide();

    if(m_filterTarget)
        m_filterTarget->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Filter cancelled → Normal";
}

void VimHandler::nextMatch()
{
    if(!m_searchMatches.empty()) {
        auto* view = m_searchView.data();
        if(!view || !view->model())
            return;

        refreshSearchMatches();
        if(m_searchMatches.empty())
            return;

        const QModelIndex currentIndex = view->currentIndex();
        const bool cursorAtLastMatch   = m_searchMatchIdx >= 0
                                    && m_searchMatchIdx < static_cast<int>(m_searchMatches.size())
                                    && m_searchMatches[static_cast<size_t>(m_searchMatchIdx)] == currentIndex;

        int nextIdx = -1;
        if(cursorAtLastMatch) {
            nextIdx = nextSearchMatchIndex(m_searchMatchIdx, static_cast<int>(m_searchMatches.size()), m_wrapScan);
        }
        else if(auto* tree = asTreeView(view)) {
            nextIdx = firstVisibleSearchMatchIndex(tree, m_searchMatches, currentIndex, m_wrapScan);
        }
        else {
            const int currentRow = currentIndex.isValid() ? currentIndex.row() : 0;
            for(int i = 0; i < static_cast<int>(m_searchMatches.size()); ++i) {
                const auto& match = m_searchMatches[static_cast<size_t>(i)];
                if(match.isValid() && match.row() >= currentRow) {
                    nextIdx = i;
                    break;
                }
            }
            if(nextIdx < 0 && m_wrapScan)
                nextIdx = 0;
        }

        if(nextIdx < 0)
            return;
        m_searchMatchIdx = nextIdx;
        qCDebug(VIM_LOG) << "nextMatch: search match" << m_searchMatchIdx
                         << searchIndexDescription(m_searchMatches[static_cast<size_t>(m_searchMatchIdx)]);
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if(!m_lastFilter.isEmpty()) {
        qCDebug(VIM_LOG) << "nextMatch: filter mode, moveCursor +1";
        moveCursor(+1);
    }
}

void VimHandler::prevMatch()
{
    if(!m_searchMatches.empty()) {
        auto* view = m_searchView.data();
        if(!view || !view->model())
            return;

        refreshSearchMatches();
        if(m_searchMatches.empty())
            return;

        const QModelIndex currentIndex = view->currentIndex();
        const bool cursorAtLastMatch   = m_searchMatchIdx >= 0
                                    && m_searchMatchIdx < static_cast<int>(m_searchMatches.size())
                                    && m_searchMatches[static_cast<size_t>(m_searchMatchIdx)] == currentIndex;

        int prevIdx = -1;
        if(cursorAtLastMatch) {
            prevIdx = prevSearchMatchIndex(m_searchMatchIdx, static_cast<int>(m_searchMatches.size()), m_wrapScan);
        }
        else if(auto* tree = asTreeView(view)) {
            for(QModelIndex index = currentIndex; index.isValid(); index = tree->indexAbove(index)) {
                if(const int matchIdx = searchMatchPosition(m_searchMatches, index); matchIdx >= 0) {
                    prevIdx = matchIdx;
                    break;
                }
            }
            if(prevIdx < 0 && m_wrapScan)
                prevIdx = static_cast<int>(m_searchMatches.size()) - 1;
        }
        else {
            const int currentRow = currentIndex.isValid() ? currentIndex.row() : 0;
            for(int i = static_cast<int>(m_searchMatches.size()) - 1; i >= 0; --i) {
                const auto& match = m_searchMatches[static_cast<size_t>(i)];
                if(match.isValid() && match.row() <= currentRow) {
                    prevIdx = i;
                    break;
                }
            }
            if(prevIdx < 0 && m_wrapScan)
                prevIdx = static_cast<int>(m_searchMatches.size()) - 1;
        }

        if(prevIdx < 0)
            return;
        m_searchMatchIdx = prevIdx;
        qCDebug(VIM_LOG) << "prevMatch: search match" << m_searchMatchIdx
                         << searchIndexDescription(m_searchMatches[static_cast<size_t>(m_searchMatchIdx)]);
        jumpToMatch(m_searchMatchIdx);
        return;
    }
    if(!m_lastFilter.isEmpty()) {
        qCDebug(VIM_LOG) << "prevMatch: filter mode, moveCursor -1";
        moveCursor(-1);
    }
}

void VimHandler::onFilterTextChanged(const QString& text)
{
    if(!m_filterTarget)
        return;
    Fooyin::SearchRequest req;
    req.text      = text;
    req.emptyMode = Fooyin::EmptySearchMode::ShowAll;
    m_filterTarget->searchEvent(req);
}

Fooyin::FyWidget* VimHandler::findEnclosingFyWidget(QAbstractItemView* view) const
{
    QWidget* w = view;
    while(w) {
        if(auto* fy = qobject_cast<Fooyin::FyWidget*>(w))
            return fy;
        w = w->parentWidget();
    }
    return nullptr;
}

bool VimHandler::organiserEditorActive(QObject* watched) const
{
    if(auto* widget = qobject_cast<QWidget*>(watched)) {
        if(auto* view = enclosingView(widget);
           viewContext(view) == ViewContext::PlaylistOrganiser && widget != view && widget != view->viewport()) {
            return true;
        }
    }

    if(auto* focus = QApplication::focusWidget(); focus) {
        if(auto* view = enclosingView(focus);
           viewContext(view) == ViewContext::PlaylistOrganiser && focus != view && focus != view->viewport()) {
            return true;
        }
    }

    return false;
}

void VimHandler::scheduleOrganiserInsertedSelection(QTreeView* tree, const QModelIndex& parent, int row)
{
    if(!tree || !tree->model() || !tree->selectionModel() || row < 0)
        return;

    QPointer<QTreeView> treePtr       = tree;
    QPersistentModelIndex parentIndex = parent;
    QTimer::singleShot(0, this, [treePtr, parentIndex, row]() {
        if(!treePtr || !treePtr->model() || !treePtr->selectionModel())
            return;

        const QModelIndex index = treePtr->model()->index(row, 0, parentIndex);
        if(!index.isValid())
            return;

        treePtr->selectionModel()->setCurrentIndex(index,
                                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        treePtr->scrollTo(index);
    });
}

VimHandler::ViewContext VimHandler::viewContext(QAbstractItemView* view) const
{
    if(!view)
        return ViewContext::None;

    if(QLatin1String(view->metaObject()->className()) == QLatin1String("Fooyin::PlaylistView"))
        return ViewContext::PlaylistView;

    if(auto* fy = findEnclosingFyWidget(view); fy && fy->layoutName() == QStringLiteral("PlaylistOrganiser"))
        return ViewContext::PlaylistOrganiser;

    return ViewContext::Other;
}

VimHandler::ViewContext VimHandler::activeViewContext() const
{
    return viewContext(m_viewLocator->activeView());
}

bool VimHandler::triggerCurrentContextAction(const Fooyin::Id& id) const
{
    if(!m_actionManager) {
        qCWarning(VIM_LOG) << "triggerCurrentContextAction: no ActionManager";
        return false;
    }

    Fooyin::Command* cmd = m_actionManager->command(id);
    if(!cmd || !cmd->action()) {
        qCWarning(VIM_LOG) << "triggerCurrentContextAction: action not found" << id.name();
        return false;
    }

    cmd->action()->trigger();
    return true;
}

void VimHandler::savePlaylistCursorState(Fooyin::Playlist* playlist)
{
    if(!playlist) {
        qCDebug(VIM_LOG) << "savePlaylistCursorState: skipped, previous playlist is null";
        return;
    }

    auto* view = playlistViewForState();
    if(!view) {
        qCDebug(VIM_LOG) << "savePlaylistCursorState: skipped for" << playlist->name()
                         << "because there is no remembered playlist view";
        return;
    }

    if(!view->model()) {
        qCDebug(VIM_LOG) << "savePlaylistCursorState: skipped for" << playlist->name()
                         << "because remembered playlist view has no model";
        return;
    }

    PlaylistCursorState state;
    state.row  = view->currentIndex().isValid() ? view->currentIndex().row() : 0;
    state.col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    state.mode = m_mode == Mode::Visual ? Mode::Visual : Mode::Normal;

    const bool preservePriorVisualState = m_preserveVisualStateOnNextPlaylistSaveId == playlist->id();
    if(const auto existing = m_playlistCursorStates.constFind(playlist->id());
       preservePriorVisualState && existing != m_playlistCursorStates.cend() && existing->mode == Mode::Visual
       && m_mode != Mode::Visual && activeViewContext() != ViewContext::PlaylistView) {
        state.mode         = existing->mode;
        state.visualAnchor = existing->visualAnchor;
        state.visualCursor = existing->visualCursor;
        qCDebug(VIM_LOG) << "savePlaylistCursorState: preserving prior visual state for" << playlist->name()
                         << "while focus is outside playlist view";
    }

    if(m_preserveVisualStateOnNextPlaylistSaveId == playlist->id())
        m_preserveVisualStateOnNextPlaylistSaveId = {};

    if(m_mode == Mode::Visual) {
        state.visualAnchor = m_visualAnchor;
        state.visualCursor = m_visualCursor;
    }

    m_playlistCursorStates.insert(playlist->id(), state);
    qCDebug(VIM_LOG) << "savePlaylistCursorState: playlist=" << playlist->name() << "row=" << state.row
                     << "col=" << state.col << "mode=" << static_cast<int>(state.mode)
                     << "anchor=" << state.visualAnchor << "cursor=" << state.visualCursor;
}

void VimHandler::restorePlaylistCursorState(Fooyin::Playlist* playlist)
{
    if(!playlist) {
        qCDebug(VIM_LOG) << "restorePlaylistCursorState: skipped, current playlist is null";
        return;
    }

    const auto it = m_playlistCursorStates.constFind(playlist->id());
    if(it == m_playlistCursorStates.cend()) {
        qCDebug(VIM_LOG) << "restorePlaylistCursorState: no saved state for" << playlist->name();
        return;
    }

    auto* view = enclosingView(QApplication::focusWidget());
    if(!view) {
        qCDebug(VIM_LOG) << "restorePlaylistCursorState: skipped for" << playlist->name()
                         << "because there is no focused playlist view yet";
        m_pendingPlaylistRestoreId = playlist->id();
        return;
    }

    if(viewContext(view) != ViewContext::PlaylistView) {
        qCDebug(VIM_LOG) << "restorePlaylistCursorState: skipped for" << playlist->name()
                         << "because focused view context is" << static_cast<int>(viewContext(view));
        m_pendingPlaylistRestoreId = playlist->id();
        return;
    }

    if(!view->model() || !view->selectionModel()) {
        qCDebug(VIM_LOG) << "restorePlaylistCursorState: skipped for" << playlist->name()
                         << "because active view is missing model or selection model";
        m_pendingPlaylistRestoreId = playlist->id();
        return;
    }

    m_pendingPlaylistRestoreId = {};

    const PlaylistCursorState state = it.value();
    QPointer<QAbstractItemView> viewPtr{view};
    const Fooyin::UId playlistId = playlist->id();
    const int expectedRowCount   = playlist->trackCount();

    auto tryRestore = [this, viewPtr, playlistId, state]() -> bool {
        if(!viewPtr || !viewPtr->model() || !viewPtr->selectionModel()) {
            qCDebug(VIM_LOG)
                << "restorePlaylistCursorState: tryRestore failed because view/model/selectionModel disappeared";
            m_pendingPlaylistRestoreId = playlistId;
            return false;
        }

        if(!m_playlistHandler || m_observedSelectedPlaylistId != playlistId) {
            qCDebug(VIM_LOG)
                << "restorePlaylistCursorState: tryRestore aborted because observed selected playlist changed"
                << "expected=" << playlistId << "actual=" << m_observedSelectedPlaylistId;
            return false;
        }

        if(auto* resolvedPlaylist = m_playlistHandler->playlistById(playlistId)) {
            applyPlaylistCursorState(viewPtr, resolvedPlaylist, state);
            return true;
        }

        qCDebug(VIM_LOG) << "restorePlaylistCursorState: tryRestore failed because playlist id could not be resolved"
                         << playlistId;
        m_pendingPlaylistRestoreId = playlistId;

        return false;
    };

    qCDebug(VIM_LOG) << "restorePlaylistCursorState: playlist=" << playlist->name()
                     << "expectedRowCount=" << expectedRowCount << "savedRow=" << state.row << "savedCol=" << state.col
                     << "savedMode=" << static_cast<int>(state.mode) << "savedAnchor=" << state.visualAnchor
                     << "savedCursor=" << state.visualCursor;

    QTimer::singleShot(0, this, [tryRestore]() { tryRestore(); });

    auto restoreAfterPopulation = [viewPtr, tryRestore, expectedRowCount]() {
        if(!viewPtr || !viewPtr->model())
            return;

        QTimer::singleShot(0, viewPtr, [viewPtr, tryRestore, expectedRowCount]() {
            if(!viewPtr || !viewPtr->model())
                return;

            if(expectedRowCount == 0 || viewPtr->model()->rowCount() >= expectedRowCount)
                tryRestore();
        });

        auto guard = std::make_shared<bool>(false);
        QObject::connect(viewPtr->model(), &QAbstractItemModel::rowsInserted, viewPtr,
                         [viewPtr, tryRestore, expectedRowCount, guard]() {
                             if(!viewPtr || !viewPtr->model() || *guard)
                                 return;

                             const int rowCount = viewPtr->model()->rowCount();
                             if(expectedRowCount > 0 && rowCount < expectedRowCount)
                                 return;

                             tryRestore();
                             *guard = true;
                         });
    };

    QObject::connect(view->model(), &QAbstractItemModel::modelReset, view->model(), restoreAfterPopulation,
                     Qt::SingleShotConnection);
}

void VimHandler::applyPlaylistCursorState(QAbstractItemView* view, Fooyin::Playlist* playlist,
                                          const PlaylistCursorState& state)
{
    if(!view || !playlist || !view->model() || !view->selectionModel())
        return;

    clearPendingInputState();
    m_count = 0;

    const int rowCount  = view->model()->rowCount();
    const Mode prevMode = m_mode;

    if(rowCount <= 0) {
        m_mode         = Mode::Normal;
        m_visualAnchor = -1;
        m_visualCursor = -1;
        view->selectionModel()->clearSelection();
        view->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);

        if(prevMode != m_mode)
            emit modeChanged(m_mode);

        qCDebug(VIM_LOG) << "applyPlaylistCursorState: playlist=" << playlist->name() << "restored empty playlist";
        return;
    }

    const int lastRow = rowCount - 1;
    const int lastCol = qMax(0, view->model()->columnCount() - 1);
    const int row     = std::clamp(state.row, 0, lastRow);
    const int col     = std::clamp(state.col, 0, lastCol);

    if(state.mode == Mode::Visual) {
        m_mode         = Mode::Visual;
        m_visualAnchor = std::clamp(state.visualAnchor, 0, lastRow);
        m_visualCursor = std::clamp(state.visualCursor, 0, lastRow);

        QItemSelection selection;
        selection.select(view->model()->index(qMin(m_visualAnchor, m_visualCursor), 0),
                         view->model()->index(qMax(m_visualAnchor, m_visualCursor), lastCol));
        view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        const QModelIndex cursorIndex = view->model()->index(m_visualCursor, col);
        if(cursorIndex.isValid()) {
            view->selectionModel()->setCurrentIndex(cursorIndex, QItemSelectionModel::NoUpdate);
            view->scrollTo(cursorIndex);
        }
    }
    else {
        m_mode         = Mode::Normal;
        m_visualAnchor = -1;
        m_visualCursor = -1;

        const QModelIndex idx = view->model()->index(row, col);
        if(idx.isValid()) {
            view->selectionModel()->setCurrentIndex(idx,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            view->scrollTo(idx);
        }
    }

    if(prevMode != m_mode)
        emit modeChanged(m_mode);

    qCDebug(VIM_LOG) << "applyPlaylistCursorState: playlist=" << playlist->name() << "row=" << row << "col=" << col
                     << "mode=" << static_cast<int>(m_mode) << "anchor=" << m_visualAnchor
                     << "cursor=" << m_visualCursor;
}

QAbstractItemView* VimHandler::playlistViewForState() const
{
    auto* activeView = m_viewLocator->activeView();
    if(activeView && viewContext(activeView) == ViewContext::PlaylistView)
        return activeView;

    if(m_lastPlaylistView && viewContext(m_lastPlaylistView) == ViewContext::PlaylistView)
        return m_lastPlaylistView;

    const auto widgets = QApplication::allWidgets();
    for(QWidget* widget : widgets) {
        auto* view = qobject_cast<QAbstractItemView*>(widget);
        if(view && view->isVisible() && viewContext(view) == ViewContext::PlaylistView)
            return view;
    }

    return nullptr;
}

void VimHandler::updateLastPlaylistView(QAbstractItemView* view)
{
    if(view && viewContext(view) == ViewContext::PlaylistView)
        m_lastPlaylistView = view;
}

void VimHandler::tryRestorePendingPlaylistState(QAbstractItemView* candidateView)
{
    if(!m_pendingPlaylistRestoreId.isValid() || !m_playlistHandler)
        return;

    auto* activeView = candidateView ? candidateView : enclosingView(QApplication::focusWidget());
    if(!activeView || viewContext(activeView) != ViewContext::PlaylistView || !activeView->model()
       || !activeView->selectionModel()) {
        return;
    }

    const auto stateIt = m_playlistCursorStates.constFind(m_pendingPlaylistRestoreId);
    if(stateIt == m_playlistCursorStates.cend())
        return;

    if(auto* playlist = m_playlistHandler->playlistById(m_pendingPlaylistRestoreId)) {
        qCDebug(VIM_LOG) << "tryRestorePendingPlaylistState: restoring pending playlist" << playlist->name();
        m_pendingPlaylistRestoreId = {};
        applyPlaylistCursorState(activeView, playlist, stateIt.value());
    }
}

// ---------------------------------------------------------------------------
// Search (/)
// ---------------------------------------------------------------------------

void VimHandler::enterSearch()
{
    auto* view = m_viewLocator->activeView();
    if(!view || !view->model()) {
        qCWarning(VIM_LOG) << "enterSearch: no active view or model";
        return;
    }

    // Save the view now — once the search bar gains focus, activeView() would
    // lose its cache and its fallback would steal focus back to the view.
    m_searchView     = view;
    m_preSearchIndex = view->currentIndex();

    if(!m_searchBar) {
        m_searchBar = new VimSearchBar();
        m_searchBar->setLabel(QStringLiteral("/"));
        connect(m_searchBar, &VimSearchBar::textChanged, this, &VimHandler::onSearchTextChanged);
        connect(m_searchBar, &VimSearchBar::confirmed, this, &VimHandler::commitSearch);
        connect(m_searchBar, &VimSearchBar::cancelled, this, &VimHandler::cancelSearch);
    }

    m_searchBar->attachTo(view->window());
    m_searchBar->clear();
    m_searchMatches.clear();
    m_searchMatchIdx = -1;

    m_mode = Mode::Search;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);

    m_searchBar->show();
    m_searchBar->setFocus();
    qCInfo(VIM_LOG) << "Mode → Search," << searchIndexDescription(m_preSearchIndex);
}

void VimHandler::commitSearch()
{
    m_lastSearchPattern = m_searchBar ? m_searchBar->text() : QString{};
    if(m_searchBar)
        m_searchBar->hide();

    if(m_searchView)
        m_searchView->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Search committed: pattern='" << m_lastSearchPattern
                    << "' matchCount=" << m_searchMatches.size();
}

void VimHandler::cancelSearch()
{
    if(m_searchBar)
        m_searchBar->hide();

    m_searchMatches.clear();
    m_searchMatchIdx = -1;
    m_lastSearchPattern.clear();

    auto* view = m_searchView.data();
    if(view && view->model() && m_preSearchIndex.isValid()) {
        const int col       = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
        QModelIndex restore = m_preSearchIndex;
        if(col > 0) {
            const QModelIndex sibling = restore.siblingAtColumn(col);
            if(sibling.isValid())
                restore = sibling;
        }
        view->selectionModel()->setCurrentIndex(restore,
                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    if(view)
        view->setFocus(Qt::OtherFocusReason);

    m_mode = Mode::Normal;
    clearPendingInputState();
    m_count = 0;
    emit modeChanged(m_mode);
    qCInfo(VIM_LOG) << "Search cancelled, cursor restored to" << searchIndexDescription(m_preSearchIndex);
}

void VimHandler::buildMatchList(const QString& pattern)
{
    m_searchMatches.clear();
    m_searchMatchIdx = -1;

    if(pattern.isEmpty())
        return;

    auto* view = m_searchView.data();
    if(!view || !view->model())
        return;

    auto* model    = view->model();
    const int cols = model->columnCount();

    const auto appendMatchIfNeeded = [&](const QModelIndex& rowIndex) {
        bool matched = false;
        QString matchedText;
        for(int col = 0; col < cols; ++col) {
            const QModelIndex cellIndex = rowIndex.siblingAtColumn(col);
            const QString cell          = searchTextForIndex(model, cellIndex);
            if(!cell.isEmpty() && cell.contains(pattern, Qt::CaseInsensitive)) {
                m_searchMatches.push_back(rowIndex);
                matched     = true;
                matchedText = cell;
                break;
            }
        }

        qCDebug(VIM_LOG) << "buildMatchList: candidate" << searchIndexDescription(rowIndex) << "matched=" << matched
                         << "text=\"" << matchedText << "\"";
    };

    if(auto* tree = asTreeView(view)) {
        QModelIndex index;
        if(model->rowCount() > 0)
            index = model->index(0, 0);

        for(; index.isValid(); index = tree->indexBelow(index))
            appendMatchIfNeeded(index);
    }
    else {
        const int rows = model->rowCount();
        for(int row = 0; row < rows; ++row)
            appendMatchIfNeeded(model->index(row, 0));
    }
    qCDebug(VIM_LOG) << "buildMatchList: pattern='" << pattern << "' matches=" << m_searchMatches.size();
}

void VimHandler::refreshSearchMatches()
{
    auto* view = m_searchView.data();
    if(!view || !view->model()) {
        m_searchMatches.clear();
        m_searchMatchIdx = -1;
        return;
    }

    m_searchMatches = orderedSearchMatches(view, m_searchMatches);

    const QModelIndex currentIndex = view->currentIndex();
    m_searchMatchIdx               = searchMatchPosition(m_searchMatches, currentIndex);
}

void VimHandler::onSearchTextChanged(const QString& text)
{
    buildMatchList(text);

    if(!m_searchMatches.empty()) {
        if(auto* tree = asTreeView(m_searchView.data()))
            m_searchMatchIdx = firstVisibleSearchMatchIndex(tree, m_searchMatches, m_preSearchIndex, m_wrapScan);
        else
            m_searchMatchIdx = firstSearchMatchIndexForRow(m_searchMatches, m_preSearchIndex.row(), m_wrapScan);

        if(m_searchMatchIdx >= 0) {
            jumpToMatch(m_searchMatchIdx);
            return;
        }

        auto* view = m_searchView.data();
        if(view && view->model() && m_preSearchIndex.isValid()) {
            const int col       = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
            QModelIndex restore = m_preSearchIndex;
            if(col > 0) {
                const QModelIndex sibling = restore.siblingAtColumn(col);
                if(sibling.isValid())
                    restore = sibling;
            }
            view->selectionModel()->setCurrentIndex(restore,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    else if(!text.isEmpty()) {
        auto* view = m_searchView.data();
        if(view && view->model() && m_preSearchIndex.isValid()) {
            const int col       = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
            QModelIndex restore = m_preSearchIndex;
            if(col > 0) {
                const QModelIndex sibling = restore.siblingAtColumn(col);
                if(sibling.isValid())
                    restore = sibling;
            }
            view->selectionModel()->setCurrentIndex(restore,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
}

void VimHandler::jumpToMatch(int idx)
{
    auto* view = m_searchView.data();
    if(!view || !view->model() || m_searchMatches.empty())
        return;
    QModelIndex mi = m_searchMatches[static_cast<size_t>(idx)];
    const int col  = view->currentIndex().isValid() ? view->currentIndex().column() : 0;
    if(col > 0) {
        const QModelIndex sibling = mi.siblingAtColumn(col);
        if(sibling.isValid())
            mi = sibling;
    }
    view->selectionModel()->setCurrentIndex(mi, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(mi);
    qCDebug(VIM_LOG) << "jumpToMatch: idx=" << idx << searchIndexDescription(mi);
}

// ---------------------------------------------------------------------------
// Config-binding implementation
// ---------------------------------------------------------------------------

void VimHandler::setSettingsManager(Fooyin::SettingsManager* manager)
{
    m_settingsManager = manager;
    if(!manager)
        return;

    if(!m_settingsBackend) {
        m_ownedSettingsBackend             = std::make_unique<VimMotionsBindingBackend>(manager, this);
        m_settingsBackend                  = m_ownedSettingsBackend.get();
        m_backendBindingsChangedConnection = QObject::connect(
            m_settingsBackend, &VimMotionsBindingBackend::bindingsChanged, this, [this]() { applyBackendBindings(); });
    }

    m_useDefaultBindings       = manager->value(QStringLiteral("VimMotions/UseDefaultBindings")).toBool();
    m_useVimMotionsInSettings  = manager->value(QStringLiteral("VimMotions/UseVimMotionsInSettings")).toBool();
    m_wrapScan                 = manager->value(QStringLiteral("VimMotions/WrapScan")).toBool();
    m_pendingSequenceTimeoutMs = qMax(0, manager->value(QStringLiteral("VimMotions/PendingSequenceTimeout")).toInt());

    using namespace Settings::VimMotions;
    manager->subscribe<UseDefaultBindings>(this, [this](bool val) {
        m_useDefaultBindings = val;
        rebuildBindings();
    });

    manager->subscribe<UseVimMotionsInSettings>(this, [this](bool val) { m_useVimMotionsInSettings = val; });

    manager->subscribe<WrapScan>(this, [this](bool val) { m_wrapScan = val; });

    manager->subscribe<PendingSequenceTimeout>(this, [this](int val) {
        m_pendingSequenceTimeoutMs = qMax(0, val);
        refreshPendingTimeout();
    });

    for(const auto& b : VimMotionsSettings::defaultBindings()) {
        manager->subscribe(QString::fromLatin1(b.key), this, [this](const QVariant&) { rebuildBindings(); });
    }

    rebuildBindings();
}

void VimHandler::setSettingsBackend(VimMotionsBindingBackend* backend)
{
    QObject::disconnect(m_backendBindingsChangedConnection);
    m_ownedSettingsBackend.reset();
    m_settingsBackend = backend;

    if(m_settingsBackend) {
        m_backendBindingsChangedConnection = QObject::connect(
            m_settingsBackend, &VimMotionsBindingBackend::bindingsChanged, this, [this]() { applyBackendBindings(); });
    }

    if(m_settingsManager) {
        rebuildBindings();
    }
}

int VimHandler::currentCount()
{
    if(m_count > 0) {
        m_hadExplicitCount = true;
        m_dispatchCount    = m_count;
        m_count            = 0;
    }
    return m_dispatchCount;
}

bool VimHandler::hadExplicitCount() const
{
    return m_hadExplicitCount;
}

void VimHandler::clearPendingState()
{
    m_count = 0;
    clearPendingInputState();
}

void VimHandler::clearPendingInputState()
{
    m_pendingKey = {};
    m_pendingConfigSequence.clear();
    m_pendingConfigFallback.reset();
    m_pendingConfigScope.reset();
    m_pendingMarkOp = PendingMarkOp::None;
    m_pendingTimeoutTimer.stop();
}

void VimHandler::setPendingKey(QChar key)
{
    clearPendingInputState();
    m_pendingKey = key;
    refreshPendingTimeout();
}

void VimHandler::setPendingMarkOp(PendingMarkOp op)
{
    clearPendingInputState();
    m_pendingMarkOp = op;
    refreshPendingTimeout();
}

void VimHandler::refreshPendingTimeout()
{
    if(m_pendingSequenceTimeoutMs <= 0 || !hasPendingInput()) {
        m_pendingTimeoutTimer.stop();
        return;
    }

    m_pendingTimeoutTimer.start(m_pendingSequenceTimeoutMs);
}

void VimHandler::moveSpatialFocus(Direction dir)
{
    auto* startView = m_viewLocator->activeView();
    if(!startView) {
        qCWarning(VIM_LOG) << "moveSpatialFocus: no active view";
        return;
    }

    if(m_mode == Mode::Visual) {
        if(viewContext(startView) == ViewContext::PlaylistView) {
            if(auto* playlist = targetPlaylist()) {
                // Preserve the playlist-local visual selection before switching
                // the global mode back to Normal on the destination widget.
                savePlaylistCursorState(playlist);
                m_preserveVisualStateOnNextPlaylistSaveId = playlist->id();
            }
        }

        qCDebug(VIM_LOG) << "moveSpatialFocus: leaving Visual mode without collapsing selection";
        m_mode = Mode::Normal;
        clearPendingInputState();
        m_count        = 0;
        m_visualAnchor = -1;
        m_visualCursor = -1;
        emit modeChanged(m_mode);
    }

    m_spatialNavigator->moveFocus(dir, startView);
    updateLastPlaylistView(m_viewLocator->activeView());
    tryRestorePendingPlaylistState(m_viewLocator->activeView());
}

void VimHandler::extendVisualCursor(int delta)
{
    auto* view     = m_viewLocator->activeView();
    const int last = (view && view->model()) ? view->model()->rowCount() - 1 : INT_MAX;
    m_visualCursor = std::clamp(m_visualCursor + delta, 0, qMax(0, last));
    updateVisualSelection();
}

void VimHandler::extendVisualToFirst()
{
    m_visualCursor = 0;
    updateVisualSelection();
}

void VimHandler::extendVisualToEnd()
{
    auto* view = m_viewLocator->activeView();
    if(view && view->model()) {
        const int last = view->model()->rowCount() - 1;
        if(last >= 0) {
            m_visualCursor = hadExplicitCount() ? std::clamp(currentCount() - 1, 0, last) : last;
            updateVisualSelection();
        }
    }
}

void VimHandler::extendVisualToRow(int row)
{
    auto* view = m_viewLocator->activeView();
    if(view && view->model()) {
        const int last = view->model()->rowCount() - 1;
        m_visualCursor = std::clamp(row, 0, qMax(0, last));
        updateVisualSelection();
    }
}

void VimHandler::extendVisualHalfPage(int direction)
{
    const int delta = halfPageDelta();
    m_visualCursor  = std::clamp(m_visualCursor + direction * delta, 0,
                                (m_viewLocator->activeView() && m_viewLocator->activeView()->model())
                                     ? qMax(0, m_viewLocator->activeView()->model()->rowCount() - 1)
                                     : 0);
    updateVisualSelection();
}

void VimHandler::swapVisualAnchor()
{
    std::swap(m_visualAnchor, m_visualCursor);
    updateVisualSelection();
}

// ---------------------------------------------------------------------------
// Binding rebuild
// ---------------------------------------------------------------------------

void VimHandler::rebuildBindings()
{
    m_configBindings.clear();
    if(!m_settingsBackend)
        return;

    m_settingsBackend->reloadBindings();
}

void VimHandler::applyBackendBindings()
{
    m_configBindings.clear();
    if(!m_settingsBackend)
        return;

    m_configBindings = convertBindings(m_settingsBackend->effectiveBindings());

    const auto bindingCountForMode = [this](Mode mode) {
        int count                 = 0;
        const auto scopedBindings = m_configBindings.value(mode);
        for(auto it = scopedBindings.cbegin(); it != scopedBindings.cend(); ++it)
            count += it.value().size();
        return count;
    };

    qCInfo(VIM_LOG) << "Rebuilt config bindings:" << bindingCountForMode(Mode::Normal) << "normal,"
                    << bindingCountForMode(Mode::Visual) << "visual," << bindingCountForMode(Mode::Insert) << "insert";
}

// ---------------------------------------------------------------------------
// Action execution
// ---------------------------------------------------------------------------

void VimHandler::executeAction(const BindingEntry& entry)
{
    auto handler = m_actions.find(entry.actionName);
    if(handler) {
        handler(*this, QStringView{entry.args});
    }
    else {
        qCWarning(VIM_LOG) << "executeAction: unknown action" << entry.actionName;
    }

    m_hadExplicitCount = false;
    m_dispatchCount    = 0;
}

// ---------------------------------------------------------------------------
// Config-driven dispatch
// ---------------------------------------------------------------------------

bool VimHandler::dispatchFromConfig(QKeyEvent* ev, Mode mode)
{
    const Qt::KeyboardModifiers mods = ev->modifiers();
    const int qtKey                  = ev->key();
    const QString text               = ev->text();
    const QChar ch                   = text.isEmpty() ? QChar{} : text.front();

    // Ignore bare modifier key presses (Alt, Ctrl, Shift, Meta) so
    // they don't consume the accumulated count.  They will be claimed
    // again as part of the full modifier+key combo.
    switch(qtKey) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            return false;
        default:
            break;
    }

    if(mode == Mode::Normal || mode == Mode::Visual) {
        if(qtKey == Qt::Key_Escape && mods == Qt::NoModifier) {
            qCDebug(VIM_LOG) << "ConfigDispatch: Esc → clear";
            clearPendingInputState();
            m_count = 0;
            if(mode == Mode::Visual)
                enterNormal();
            return true;
        }

        if((mode == Mode::Normal || mode == Mode::Visual) && handlePendingMarkOp(ev))
            return true;

        if(!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier)) {
            const int digit = ch.digitValue();
            if(m_count > 0 || digit != 0) {
                m_count = m_count * 10 + digit;
                qCDebug(VIM_LOG) << "ConfigDispatch: digit" << digit << "→ count=" << m_count;
            }
            return true;
        }
    }

    m_hadExplicitCount = m_count > 0;
    m_dispatchCount    = m_count > 0 ? m_count : 1;
    m_count            = 0;

    const BindingScope activeScope = activeBindingScope();
    const auto scopedBindings      = m_configBindings.value(mode).value(activeScope);
    const auto globalBindings      = m_configBindings.value(mode).value(BindingScope::Global);

    const auto bestSingleBinding = [ev](const QList<BindingEntry>& bindings) -> const BindingEntry* {
        const BindingEntry* best = nullptr;
        int bestModCount         = -1;

        for(const auto& binding : bindings) {
            if(binding.keys.size() != 1 || !binding.keys.front().matches(ev))
                continue;

            int modCount         = 0;
            const auto modifiers = binding.keys.front().modifiers;
            if(modifiers & Qt::ControlModifier)
                ++modCount;
            if(modifiers & Qt::AltModifier)
                ++modCount;
            if(modifiers & Qt::ShiftModifier)
                ++modCount;
            if(modifiers & Qt::MetaModifier)
                ++modCount;

            if(modCount > bestModCount) {
                bestModCount = modCount;
                best         = &binding;
            }
        }

        return best;
    };

    const auto sequencePrefixBinding = [ev](const QList<BindingEntry>& bindings) -> const BindingEntry* {
        for(const auto& binding : bindings) {
            if(binding.keys.size() >= 2 && binding.keys.front().matches(ev))
                return &binding;
        }

        return nullptr;
    };

    struct PendingSequenceMatch
    {
        const BindingEntry* complete{nullptr};
        QList<KeyCombo> nextPrefix;
    };

    const auto matchPendingSequence = [this, ev](const QList<BindingEntry>& bindings) {
        PendingSequenceMatch match;

        for(const auto& binding : bindings) {
            if(!pendingConfigPrefixMatches(binding))
                continue;
            if(binding.keys.size() <= m_pendingConfigSequence.size())
                continue;
            if(!binding.keys[m_pendingConfigSequence.size()].matches(ev))
                continue;

            const auto prefix = binding.keys.mid(0, m_pendingConfigSequence.size() + 1);
            if(binding.keys.size() == prefix.size()) {
                match.complete = &binding;
                continue;
            }

            match.nextPrefix = prefix;
            return match;
        }

        return match;
    };

    if(m_pendingConfigScope.has_value()) {
        const BindingScope pendingScope = *m_pendingConfigScope;
        const auto pendingBindings      = m_configBindings.value(mode).value(pendingScope);
        const auto pendingMatch         = matchPendingSequence(pendingBindings);
        if(!pendingMatch.nextPrefix.isEmpty()) {
            m_pendingConfigSequence = pendingMatch.nextPrefix;
            m_pendingConfigScope    = pendingScope;
            refreshPendingTimeout();
            qCDebug(VIM_LOG) << "ConfigDispatch: sequence pending length" << m_pendingConfigSequence.size();
            return true;
        }

        if(pendingMatch.complete) {
            qCDebug(VIM_LOG) << "ConfigDispatch: sequence complete" << pendingMatch.complete->actionName;
            clearPendingInputState();
            executeAction(*pendingMatch.complete);
            return true;
        }

        if(pendingScope != BindingScope::Global) {
            const auto globalMatch = matchPendingSequence(globalBindings);
            if(!globalMatch.nextPrefix.isEmpty()) {
                m_pendingConfigSequence = globalMatch.nextPrefix;
                m_pendingConfigScope    = BindingScope::Global;
                refreshPendingTimeout();
                qCDebug(VIM_LOG) << "ConfigDispatch: sequence pending length" << m_pendingConfigSequence.size();
                return true;
            }

            if(globalMatch.complete) {
                qCDebug(VIM_LOG) << "ConfigDispatch: sequence complete" << globalMatch.complete->actionName;
                clearPendingInputState();
                executeAction(*globalMatch.complete);
                return true;
            }
        }

        if(m_pendingConfigFallback.has_value()) {
            const auto fallback = *m_pendingConfigFallback;
            qCDebug(VIM_LOG) << "ConfigDispatch: sequence mismatch, using fallback" << fallback.actionName;
            clearPendingInputState();
            executeAction(fallback);
            return true;
        }

        clearPendingInputState();
        qCDebug(VIM_LOG) << "ConfigDispatch: pending sequence cleared on mismatch";
        return true;
    }

    const BindingEntry* scopedBest = bestSingleBinding(scopedBindings);
    const BindingEntry* globalBest = activeScope == BindingScope::Global ? nullptr : bestSingleBinding(globalBindings);
    const BindingEntry* scopedSequenceStart = sequencePrefixBinding(scopedBindings);
    const BindingEntry* globalSequenceStart
        = activeScope == BindingScope::Global ? nullptr : sequencePrefixBinding(globalBindings);

    if(scopedSequenceStart) {
        m_pendingConfigSequence = {scopedSequenceStart->keys.front()};
        m_pendingConfigFallback.reset();
        m_pendingConfigScope = activeScope;
        if(scopedBest) {
            m_pendingConfigFallback = *scopedBest;
        }
        else if(globalBest) {
            m_pendingConfigFallback = *globalBest;
        }
        refreshPendingTimeout();
        qCDebug(VIM_LOG) << "ConfigDispatch: sequence start length" << m_pendingConfigSequence.size()
                         << "fallback=" << (m_pendingConfigFallback ? m_pendingConfigFallback->actionName : QString{});
        return true;
    }

    if(scopedBest) {
        qCDebug(VIM_LOG) << "ConfigDispatch: action" << scopedBest->actionName << "args=" << scopedBest->args;
        executeAction(*scopedBest);
        return true;
    }

    if(globalSequenceStart) {
        m_pendingConfigSequence = {globalSequenceStart->keys.front()};
        m_pendingConfigFallback.reset();
        m_pendingConfigScope = BindingScope::Global;
        if(globalBest)
            m_pendingConfigFallback = *globalBest;
        refreshPendingTimeout();
        qCDebug(VIM_LOG) << "ConfigDispatch: sequence start length" << m_pendingConfigSequence.size()
                         << "fallback=" << (m_pendingConfigFallback ? m_pendingConfigFallback->actionName : QString{});
        return true;
    }

    if(globalBest) {
        qCDebug(VIM_LOG) << "ConfigDispatch: action" << globalBest->actionName << "args=" << globalBest->args;
        executeAction(*globalBest);
        return true;
    }

    qCDebug(VIM_LOG) << "ConfigDispatch: no binding for key=" << qtKey << "text=" << text
                     << "scope=" << static_cast<int>(activeScope);
    return false;
}

bool VimHandler::dispatchFromBindings(QKeyEvent* ev, const BindingScope scope, const QList<BindingEntry>& bindings)
{
    const QString text = ev->text();
    const QChar ch     = text.isEmpty() ? QChar{} : text.front();

    if(!m_pendingConfigSequence.isEmpty()) {
        const BindingEntry* completeMatch = nullptr;
        QList<KeyCombo> nextPrefix;

        for(const auto& b : bindings) {
            if(!pendingConfigPrefixMatches(b))
                continue;
            if(b.keys.size() <= m_pendingConfigSequence.size())
                continue;
            if(!b.keys[m_pendingConfigSequence.size()].matches(ev))
                continue;

            nextPrefix = b.keys.mid(0, m_pendingConfigSequence.size() + 1);
            if(b.keys.size() == nextPrefix.size()) {
                completeMatch = &b;
            }
            else {
                m_pendingConfigSequence = nextPrefix;
                m_pendingConfigScope    = scope;
                refreshPendingTimeout();
                qCDebug(VIM_LOG) << "ConfigDispatch: sequence pending length" << m_pendingConfigSequence.size();
                return true;
            }
        }

        if(completeMatch) {
            qCDebug(VIM_LOG) << "ConfigDispatch: sequence complete" << completeMatch->actionName;
            clearPendingInputState();
            executeAction(*completeMatch);
            return true;
        }

        if(m_pendingConfigFallback.has_value()) {
            const auto fallback = *m_pendingConfigFallback;
            qCDebug(VIM_LOG) << "ConfigDispatch: sequence mismatch, using fallback" << fallback.actionName;
            clearPendingInputState();
            executeAction(fallback);
            return true;
        }

        clearPendingInputState();
    }

    if(!m_pendingKey.isNull()) {
        QChar pending = m_pendingKey;
        clearPendingInputState();

        for(const auto& b : bindings) {
            if(b.keys.size() == 2 && b.keys[0].ch == pending && b.keys[1].matches(ev)) {
                qCDebug(VIM_LOG) << "ConfigDispatch: two-key complete '" << pending << ch << "'";
                executeAction(b);
                return true;
            }
        }
    }

    const BindingEntry* best = nullptr;
    int bestModCount         = -1;

    for(const auto& b : bindings) {
        if(b.keys.size() != 1)
            continue;
        if(b.keys.front().matches(ev)) {
            int mc = 0;
            auto m = b.keys.front().modifiers;
            if(m & Qt::ControlModifier)
                ++mc;
            if(m & Qt::AltModifier)
                ++mc;
            if(m & Qt::ShiftModifier)
                ++mc;
            if(m & Qt::MetaModifier)
                ++mc;
            if(mc > bestModCount) {
                bestModCount = mc;
                best         = &b;
            }
        }
    }

    const BindingEntry* sequencePrefix = nullptr;
    for(const auto& b : bindings) {
        if(b.keys.size() < 2)
            continue;
        if(b.keys.front().matches(ev)) {
            sequencePrefix = &b;
            break;
        }
    }

    if(sequencePrefix) {
        m_pendingConfigSequence = {sequencePrefix->keys.front()};
        m_pendingConfigFallback.reset();
        m_pendingConfigScope = scope;
        if(best)
            m_pendingConfigFallback = *best;
        refreshPendingTimeout();
        qCDebug(VIM_LOG) << "ConfigDispatch: sequence start length" << m_pendingConfigSequence.size()
                         << "fallback=" << (m_pendingConfigFallback ? m_pendingConfigFallback->actionName : QString{});
        return true;
    }

    if(!best) {
        qCDebug(VIM_LOG) << "ConfigDispatch: no binding for key=" << ev->key() << "text=" << text;
        return false;
    }

    qCDebug(VIM_LOG) << "ConfigDispatch: action" << best->actionName << "args=" << best->args;
    executeAction(*best);
    return true;
}

// ---------------------------------------------------------------------------
// Config-driven wouldHandle (ShortcutOverride predicate)
// ---------------------------------------------------------------------------

bool VimHandler::wouldHandleFromConfig(QKeyEvent* ev, Mode mode) const
{
    if(mode == Mode::Normal || mode == Mode::Visual) {
        const int qtKey = ev->key();
        const auto mods = ev->modifiers();
        if(qtKey == Qt::Key_Escape && mods == Qt::NoModifier)
            return true;

        if((mode == Mode::Normal || mode == Mode::Visual) && m_pendingMarkOp != PendingMarkOp::None)
            return true;

        const QChar ch = ev->text().isEmpty() ? QChar{} : ev->text().front();
        if(!ch.isNull() && ch.isDigit() && !(mods & ~Qt::KeypadModifier))
            return true;
    }

    if(m_pendingConfigScope.has_value()) {
        const auto pendingBindings = m_configBindings.value(mode).value(*m_pendingConfigScope);
        if(wouldHandleFromBindings(ev, pendingBindings))
            return true;
    }

    const BindingScope activeScope = activeBindingScope();
    if(wouldHandleFromBindings(ev, m_configBindings.value(mode).value(activeScope)))
        return true;

    return activeScope != BindingScope::Global
        && wouldHandleFromBindings(ev, m_configBindings.value(mode).value(BindingScope::Global));
}

bool VimHandler::wouldHandleFromBindings(QKeyEvent* ev, const QList<BindingEntry>& bindings) const
{
    if(bindings.isEmpty())
        return false;

    if(!m_pendingConfigSequence.isEmpty()) {
        for(const auto& b : bindings) {
            if(!pendingConfigPrefixMatches(b))
                continue;
            if(b.keys.size() <= m_pendingConfigSequence.size())
                continue;
            if(b.keys[m_pendingConfigSequence.size()].matches(ev))
                return true;
        }
    }

    if(!m_pendingKey.isNull()) {
        for(const auto& b : bindings) {
            if(b.keys.size() == 2 && b.keys[0].ch == m_pendingKey && b.keys[1].matches(ev))
                return true;
        }
    }

    for(const auto& b : bindings) {
        if(!b.keys.isEmpty() && b.keys.front().matches(ev))
            return true;
    }

    return false;
}

BindingScope VimHandler::bindingScopeForView(QAbstractItemView* view) const
{
    switch(viewContext(view)) {
        case ViewContext::PlaylistView:
            return BindingScope::PlaylistView;
        case ViewContext::PlaylistOrganiser:
            return BindingScope::PlaylistOrganiser;
        case ViewContext::None:
        case ViewContext::Other:
            return BindingScope::Global;
    }

    return BindingScope::Global;
}

BindingScope VimHandler::activeBindingScope() const
{
    return bindingScopeForView(m_viewLocator->activeView());
}

bool VimHandler::pendingConfigPrefixMatches(const BindingEntry& entry) const
{
    if(entry.keys.size() < m_pendingConfigSequence.size())
        return false;

    for(qsizetype i = 0; i < m_pendingConfigSequence.size(); ++i) {
        if(entry.keys[i] != m_pendingConfigSequence[i])
            return false;
    }

    return true;
}

} // namespace Fooyin::VimMotions
