#include "vimplaylistpickerdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProxyStyle>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyleOption>
#include <QTest>

using namespace Fooyin::VimMotions;
using namespace Qt::StringLiterals;

class PickerRecordingStyle : public QProxyStyle
{
public:
    mutable bool selectedIsActive{false};

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override
    {
        if(element == CE_ItemViewItem && (option->state & State_Selected))
            selectedIsActive = option->state & State_Active;
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

class TestPlaylistPickerDialog : public QObject
{
    Q_OBJECT

    const Fooyin::UId jazz      = Fooyin::UId::create();
    const Fooyin::UId rock      = Fooyin::UId::create();
    const Fooyin::UId classical = Fooyin::UId::create();

    QList<PlaylistPickerEntry> entries() const
    {
        return {{jazz, u"Jazz"_s, 0, 12}, {rock, u"Rock Classics"_s, 1, 30}, {classical, u"Classical"_s, 2, 5}};
    }

    static QLineEdit* open(VimPlaylistPickerDialog& dialog)
    {
        dialog.open();
        dialog.activateWindow();
        dialog.setFocus();
        QCoreApplication::processEvents();
        return dialog.findChild<QLineEdit*>();
    }

private slots:
    void navigationKeepsInputFocus_data()
    {
        QTest::addColumn<int>("direction");
        QTest::newRow("next") << 1;
        QTest::newRow("previous") << -1;
    }

    void navigationKeepsInputFocus()
    {
        QFETCH(int, direction);
        VimPlaylistPickerDialog dialog;
        dialog.setPlaylists(entries(), rock);
        auto* input = open(dialog);
        QTRY_COMPARE(QApplication::focusWidget(), input);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QTest::keyClick(input, Qt::Key_A);
        input->setSelection(0, 1);
        const int caret  = input->cursorPosition();
        auto* list       = dialog.findChild<QListWidget*>();
        const int before = list->currentRow();
        dialog.moveSelection(direction);
        QCOMPARE(list->currentRow(), (before + direction + list->count()) % list->count());
        QCOMPARE(QApplication::focusWidget(), input);
        QCOMPARE(input->text(), u"a"_s);
        QCOMPARE(input->selectedText(), u"a"_s);
        QCOMPARE(input->cursorPosition(), caret);
        // The selection is still in the edit, so typing replaces it normally.
        QTest::keyClicks(input, "rckcl");
        QCOMPARE(input->text(), u"rckcl"_s);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QCOMPARE(list->count(), 1);
        dialog.moveSelection(direction);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QCOMPARE(QApplication::focusWidget(), input);
    }

    void wrapsAndKeepsInputFocus()
    {
        VimPlaylistPickerDialog dialog;
        dialog.setPlaylists(entries(), jazz);
        auto* input = open(dialog);
        dialog.moveSelection(-1);
        QCOMPARE(dialog.selectedPlaylistId(), classical);
        dialog.moveSelection(1);
        QCOMPARE(dialog.selectedPlaylistId(), jazz);
        dialog.moveSelection(1);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QCOMPARE(QApplication::focusWidget(), input);
        dialog.moveSelection(1);
        QCOMPARE(dialog.selectedPlaylistId(), classical);
        QCOMPARE(QApplication::focusWidget(), input);
    }

    void selectionIsVisibleWithoutListFocus()
    {
        VimPlaylistPickerDialog dialog;
        dialog.setPlaylists(entries(), jazz);
        auto* input = open(dialog);
        auto* list  = dialog.findChild<QListWidget*>();
        auto* style = new PickerRecordingStyle;
        style->setParent(list);
        list->setStyle(style);
        list->viewport()->grab();
        QVERIFY(!list->hasFocus());
        QVERIFY(style->selectedIsActive);
        const auto rect = list->visualItemRect(list->item(1));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QCOMPARE(QApplication::focusWidget(), input);
    }

    void refreshFilterAndEmptyStates()
    {
        VimPlaylistPickerDialog dialog;
        auto* input = open(dialog);
        auto* empty = dialog.findChild<QLabel*>(u"playlistPickerEmptyState"_s);
        QCOMPARE(empty->text(), u"No playlists"_s);
        QSignalSpy requested{&dialog, &VimPlaylistPickerDialog::playlistRequested};
        dialog.accept();
        QCOMPARE(requested.count(), 0);
        QVERIFY(dialog.isVisible());
        dialog.setPlaylists(entries(), rock);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QTest::keyClicks(input, "jzz");
        QCOMPARE(dialog.selectedPlaylistId(), jazz);
        auto updated    = entries();
        updated[0].name = u"Jazzy Nights"_s;
        std::swap(updated[0], updated[2]);
        dialog.setPlaylists(updated, rock);
        QCOMPARE(dialog.selectedPlaylistId(), jazz);
        QCOMPARE(input->text(), u"jzz"_s);
        QCOMPARE(QApplication::focusWidget(), input);
        updated.removeIf([&](const auto& entry) { return entry.id == jazz; });
        dialog.setPlaylists(updated, rock);
        QVERIFY(!dialog.selectedPlaylistId().isValid());
        QCOMPARE(empty->text(), u"No matching playlists"_s);
        dialog.moveSelection(1);
        dialog.moveSelection(-1);
        dialog.accept();
        QCOMPARE(requested.count(), 0);
        input->clear();
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        QApplication::clipboard()->setText(u"rckcl"_s);
        QTest::keyClick(input, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(dialog.selectedPlaylistId(), rock);
        dialog.accept();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.front().front().value<Fooyin::UId>(), rock);
        dialog.accept();
        QCOMPARE(requested.count(), 2);
        QSignalSpy finished{&dialog, &QDialog::finished};
        dialog.reject();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.front().front().toInt(), int(QDialog::Rejected));
    }

    void deterministicOrderAndScrolling()
    {
        VimPlaylistPickerDialog dialog;
        QList<PlaylistPickerEntry> candidates;
        for(int i = 0; i < 100; ++i)
            candidates.prepend({Fooyin::UId::create(), u"Playlist %1"_s.arg(i), i, 0});
        const auto first = candidates.back().id;
        const auto last  = candidates.front().id;
        dialog.setPlaylists(candidates, first);
        auto* input = open(dialog);
        auto* list  = dialog.findChild<QListWidget*>();
        QCOMPARE(dialog.selectedPlaylistId(), first);
        dialog.moveSelection(-1);
        QCOMPARE(dialog.selectedPlaylistId(), last);
        QVERIFY(list->verticalScrollBar()->value() > 0);
        QCOMPARE(QApplication::focusWidget(), input);
        QSignalSpy finished{&dialog, &QDialog::finished};
        dialog.close();
        QCOMPARE(finished.count(), 1);
    }

    void breaksTiesByStableId()
    {
        VimPlaylistPickerDialog dialog;
        auto a = jazz;
        auto b = rock;
        if(b < a)
            std::swap(a, b);
        dialog.setPlaylists({{b, u"Same name"_s, 0, 0}, {a, u"Same name"_s, 0, 0}}, {});
        auto* input = open(dialog);
        QTest::keyClicks(input, "same");
        QCOMPARE(dialog.selectedPlaylistId(), a);
        dialog.setPlaylists({{a, u"Same name"_s, 0, 0}, {b, u"Same name"_s, 0, 0}}, {});
        QCOMPARE(dialog.selectedPlaylistId(), a);
        dialog.moveSelection(1);
        QCOMPARE(dialog.selectedPlaylistId(), b);
    }

    void unmappedDialogKeysAreInert()
    {
        VimPlaylistPickerDialog dialog;
        dialog.setPlaylists(entries(), jazz);
        auto* input = open(dialog);
        QSignalSpy requested{&dialog, &VimPlaylistPickerDialog::playlistRequested};
        QSignalSpy finished{&dialog, &QDialog::finished};
        for(auto* target : {static_cast<QWidget*>(input), static_cast<QWidget*>(&dialog)}) {
            QTest::keyClick(target, Qt::Key_Return);
            QTest::keyClick(target, Qt::Key_Enter, Qt::KeypadModifier);
            QTest::keyClick(target, Qt::Key_Escape);
        }
        QCOMPARE(requested.count(), 0);
        QCOMPARE(finished.count(), 0);
        QVERIFY(dialog.isVisible());
        dialog.close();
        QCOMPARE(finished.count(), 1);
    }

    void localModeControlsEditing()
    {
        VimPlaylistPickerDialog dialog;
        auto* input = open(dialog);
        QTest::keyClicks(input, "jazz");
        dialog.setInputMode(false);
        QVERIFY(input->isReadOnly());
        QTest::keyClicks(input, "rock");
        QCOMPARE(input->text(), u"jazz"_s);
        QCOMPARE(dialog.findChild<QLabel*>(u"playlistPickerMode"_s)->text(), u"NORMAL"_s);
        dialog.setInputMode(true);
        QTest::keyClicks(input, "y");
        QCOMPARE(input->text(), u"jazzy"_s);
        QCOMPARE(QApplication::focusWidget(), input);
    }
};

QTEST_MAIN(TestPlaylistPickerDialog)
#include "vimplaylistpickerdialogtest.moc"
