#pragma once

#include <utils/id.h>

#include <QDialog>
#include <QList>

class QLabel;
class QLineEdit;
class QListWidget;

namespace Fooyin::VimMotions {

struct PlaylistPickerEntry
{
    Fooyin::UId id;
    QString name;
    int index{0};
    int trackCount{0};
};

class VimPlaylistPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VimPlaylistPickerDialog(QWidget* parent = nullptr);

    void setPlaylists(QList<PlaylistPickerEntry> entries, const Fooyin::UId& currentId);
    [[nodiscard]] Fooyin::UId selectedPlaylistId() const;
    void moveSelection(qint64 delta);
    void setInputMode(bool insert);
    void accept() override;

signals:
    // The owner validates the ID and calls done(Accepted) only if it still exists.
    void playlistRequested(const Fooyin::UId& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuildResults(bool preserveSelection);

    QLineEdit* m_input;
    QListWidget* m_results;
    QLabel* m_emptyState;
    QLabel* m_modeLabel;
    QList<PlaylistPickerEntry> m_entries;
    QList<QString> m_foldedNames;
    Fooyin::UId m_currentId;
};

} // namespace Fooyin::VimMotions
