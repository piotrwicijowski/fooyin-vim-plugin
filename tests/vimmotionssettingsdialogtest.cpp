#include "vimmotionssettingsdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <utils/settings/settingsmanager.h>

#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"

using namespace Fooyin::VimMotions;

class TestVimMotionsSettingsDialog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testWidgetScaffold();
    void testBindingEditorShowsHelp();
};

void TestVimMotionsSettingsDialog::testWidgetScaffold()
{
    VimMotionsSettingsDialog dialog;

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Vim Motions Settings"));

    auto* timeout = dialog.findChild<QSpinBox*>(QStringLiteral("pendingSequenceTimeout"));
    QVERIFY(timeout);
    QCOMPARE(timeout->suffix(), QStringLiteral(" ms"));
    QCOMPARE(timeout->minimum(), 0);
    QCOMPARE(timeout->value(), 0);

    auto* wrapScan = dialog.findChild<QCheckBox*>(QStringLiteral("wrapScan"));
    QVERIFY(wrapScan);
    QVERIFY(wrapScan->isChecked());

    auto* useVimMotionsInSettings = dialog.findChild<QCheckBox*>(QStringLiteral("useVimMotionsInSettings"));
    QVERIFY(useVimMotionsInSettings);
    QVERIFY(!useVimMotionsInSettings->isChecked());

    auto* useDefaultBindings = dialog.findChild<QCheckBox*>(QStringLiteral("useDefaultBindings"));
    QVERIFY(useDefaultBindings);
    QVERIFY(useDefaultBindings->isChecked());

    auto* tree = dialog.findChild<QTreeView*>(QStringLiteral("effectiveBindingsTree"));
    QVERIFY(tree);
    QCOMPARE(tree->editTriggers(), QAbstractItemView::NoEditTriggers);
    QCOMPARE(tree->selectionMode(), QAbstractItemView::SingleSelection);
    QVERIFY(tree->isSortingEnabled());
    QVERIFY(tree->model());
    QCOMPARE(tree->model()->columnCount(), 6);
    QCOMPARE(tree->model()->rowCount(), 0);
    QVERIFY(dialog.width() >= 900);
    QVERIFY(dialog.height() >= 600);

    auto* scopeBox = dialog.findChild<QComboBox*>(QStringLiteral("bindingScope"));
    QVERIFY(!scopeBox);

    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("addBinding")));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("editBinding")));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("removeBinding")));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("unmapBinding")));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("resetBinding")));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("discardBindingChanges")));

    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttons);
    QCOMPARE(buttons->standardButtons(),
             QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Reset | QDialogButtonBox::Cancel);

    QVERIFY(buttons->button(QDialogButtonBox::Ok));
    QVERIFY(buttons->button(QDialogButtonBox::Apply));
    QVERIFY(buttons->button(QDialogButtonBox::Reset));
    QVERIFY(buttons->button(QDialogButtonBox::Cancel));
}

void TestVimMotionsSettingsDialog::testBindingEditorShowsHelp()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fooyin::SettingsManager settings{tempDir.filePath(QStringLiteral("vim_settings_dialog_help.ini"))};
    VimMotionsSettings vimSettings{&settings};
    Q_UNUSED(vimSettings);
    VimMotionsBindingBackend backend{&settings};
    VimMotionsSettingsDialog dialog{&settings, &backend};

    QTimer::singleShot(0, [&dialog]() {
        auto* addButton = dialog.findChild<QPushButton*>(QStringLiteral("addBinding"));
        QVERIFY(addButton);

        QTimer::singleShot(0, []() {
            auto* editor = qobject_cast<QDialog*>(qApp->activeModalWidget());
            QVERIFY(editor);

            auto* keysHelpButton   = editor->findChild<QPushButton*>(QStringLiteral("bindingKeysHelpButton"));
            auto* actionHelpButton = editor->findChild<QPushButton*>(QStringLiteral("bindingActionHelpButton"));
            auto* buttons          = editor->findChild<QDialogButtonBox*>(QStringLiteral("bindingEditButtons"));

            QVERIFY(keysHelpButton);
            QVERIFY(actionHelpButton);

            QTimer::singleShot(0, []() {
                auto* helpDialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
                QVERIFY(helpDialog);
                QCOMPARE(helpDialog->objectName(), QStringLiteral("bindingKeysHelpDialog"));

                auto* text = helpDialog->findChild<QLabel*>(QStringLiteral("bindingKeysHelpDialogText"));
                QVERIFY(text);
                QVERIFY(text->text().contains(QStringLiteral("Ctrl+J")));
                QVERIFY(text->text().contains(QStringLiteral("g<Space>")));

                auto* closeButtons
                    = helpDialog->findChild<QDialogButtonBox*>(QStringLiteral("bindingKeysHelpDialogButtons"));
                QVERIFY(closeButtons);
                auto* closeButton = closeButtons->button(QDialogButtonBox::Close);
                QVERIFY(closeButton);
                closeButton->click();
            });
            keysHelpButton->click();

            QTimer::singleShot(0, []() {
                auto* helpDialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
                QVERIFY(helpDialog);
                QCOMPARE(helpDialog->objectName(), QStringLiteral("bindingActionHelpDialog"));

                auto* text = helpDialog->findChild<QLabel*>(QStringLiteral("bindingActionHelpDialogText"));
                QVERIFY(text);
                QVERIFY(text->text().contains(QStringLiteral("moveCursor(+/-N)")));
                QVERIFY(text->text().contains(QStringLiteral("fooyinAction(Action.Id)")));

                auto* closeButtons
                    = helpDialog->findChild<QDialogButtonBox*>(QStringLiteral("bindingActionHelpDialogButtons"));
                QVERIFY(closeButtons);
                auto* closeButton = closeButtons->button(QDialogButtonBox::Close);
                QVERIFY(closeButton);
                closeButton->click();
            });
            actionHelpButton->click();

            QVERIFY(buttons);
            auto* cancelButton = buttons->button(QDialogButtonBox::Cancel);
            QVERIFY(cancelButton);
            cancelButton->click();
        });

        addButton->click();
    });

    dialog.show();
    QTest::qWait(50);
}

QTEST_MAIN(TestVimMotionsSettingsDialog)
#include "vimmotionssettingsdialogtest.moc"
