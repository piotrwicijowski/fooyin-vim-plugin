#include "vimplaylistpickerdialog.h"
#include "playlistfuzzymatcher.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace Fooyin::VimMotions {
namespace {

// A selected result should look selected even though the input owns focus.
class PickerDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if(option->state & QStyle::State_Selected)
            option->state |= QStyle::State_Active;
    }
};

} // namespace

VimPlaylistPickerDialog::VimPlaylistPickerDialog(QWidget* parent)
    : QDialog{parent}
    , m_input{new QLineEdit(this)}
    , m_results{new QListWidget(this)}
    , m_emptyState{new QLabel(this)}
    , m_modeLabel{new QLabel(this)}
{
    setObjectName(QStringLiteral("vimPlaylistPicker"));
    setWindowTitle(tr("Find Playlist"));
    setWindowModality(Qt::WindowModal);
    resize(540, 400);
    m_input->setObjectName(QStringLiteral("playlistPickerInput"));
    m_input->setPlaceholderText(tr("Find playlist…"));
    m_results->setObjectName(QStringLiteral("playlistPickerResults"));
    m_results->setFocusPolicy(Qt::NoFocus);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setItemDelegate(new PickerDelegate(m_results));
    m_emptyState->setObjectName(QStringLiteral("playlistPickerEmptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_input);
    layout->addWidget(m_results, 1);
    layout->addWidget(m_emptyState, 1);
    m_modeLabel->setObjectName(QStringLiteral("playlistPickerMode"));
    layout->addWidget(m_modeLabel);
    setInputMode(true);
    setFocusProxy(m_input);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::textChanged, this, [this]() { rebuildResults(false); });
    connect(m_results, &QListWidget::itemClicked, this, [this]() { m_input->setFocus(); });
    rebuildResults(false);
}

void VimPlaylistPickerDialog::setPlaylists(QList<PlaylistPickerEntry> entries, const Fooyin::UId& currentId)
{
    m_entries   = std::move(entries);
    m_currentId = currentId;
    m_foldedNames.clear();
    for(const auto& entry : m_entries)
        m_foldedNames.append(entry.name.toCaseFolded());
    rebuildResults(true);
}

Fooyin::UId VimPlaylistPickerDialog::selectedPlaylistId() const
{
    const auto* item = m_results->currentItem();
    return item ? item->data(Qt::UserRole).value<Fooyin::UId>() : Fooyin::UId{};
}

void VimPlaylistPickerDialog::rebuildResults(bool preserveSelection)
{
    const auto previousId = preserveSelection ? selectedPlaylistId() : Fooyin::UId{};
    const QString query   = m_input->text().trimmed().toCaseFolded();
    struct Match
    {
        qsizetype index;
        PlaylistFuzzyScore score;
    };
    QList<Match> matches;
    for(qsizetype i = 0; i < m_entries.size(); ++i) {
        if(const auto score = playlistFuzzyScore(m_foldedNames[i], query))
            matches.append({i, *score});
    }
    std::sort(matches.begin(), matches.end(), [this](const Match& a, const Match& b) {
        if(a.score != b.score)
            return a.score > b.score;
        const auto& lhs = m_entries[a.index];
        const auto& rhs = m_entries[b.index];
        if(lhs.index != rhs.index)
            return lhs.index < rhs.index;
        return lhs.id < rhs.id;
    });

    m_results->clear();
    int selected           = 0;
    const auto preferredId = previousId.isValid() ? previousId : query.isEmpty() ? m_currentId : Fooyin::UId{};
    for(const auto& match : matches) {
        const auto& entry    = m_entries[match.index];
        const QString marker = entry.id == m_currentId ? QStringLiteral("● ") : QString{};
        auto* item
            = new QListWidgetItem(marker + entry.name + tr("  (%n tracks)", nullptr, entry.trackCount), m_results);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.id));
        item->setToolTip(entry.name + QStringLiteral("\n") + entry.id.toString());
        if(entry.id == preferredId)
            selected = m_results->count() - 1;
    }
    if(!matches.isEmpty()) {
        m_results->setCurrentRow(selected);
        m_results->scrollToItem(m_results->currentItem());
    }
    m_results->setVisible(!matches.isEmpty());
    m_emptyState->setVisible(matches.isEmpty());
    m_emptyState->setText(m_entries.isEmpty() ? tr("No playlists") : tr("No matching playlists"));
}

void VimPlaylistPickerDialog::moveSelection(qint64 delta)
{
    const int count = m_results->count();
    if(count == 0)
        return;
    m_results->setCurrentRow(static_cast<int>((m_results->currentRow() + delta % count + count) % count));
    m_results->scrollToItem(m_results->currentItem());
}

bool VimPlaylistPickerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == m_input && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto* key = static_cast<QKeyEvent*>(event);
        // Mappings are handled by VimHandler's application filter first. Never
        // let unmapped dialog keys fall back to QDialog's implicit actions.
        if(key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape
           || key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            key->accept();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void VimPlaylistPickerDialog::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Escape) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void VimPlaylistPickerDialog::setInputMode(bool insert)
{
    m_input->setReadOnly(!insert);
    m_modeLabel->setText(insert ? tr("INSERT") : tr("NORMAL"));
}

void VimPlaylistPickerDialog::accept()
{
    if(const auto id = selectedPlaylistId(); id.isValid())
        emit playlistRequested(id);
}

} // namespace Fooyin::VimMotions
