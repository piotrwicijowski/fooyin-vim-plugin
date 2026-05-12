#include "vimmotionssettingsdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QTreeView>

using namespace Fooyin::VimMotions;

class TestVimMotionsSettingsDialog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testWidgetScaffold();
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
    QVERIFY(tree->model());
    QCOMPARE(tree->model()->columnCount(), 5);
    QCOMPARE(tree->model()->rowCount(), 0);

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

QTEST_MAIN(TestVimMotionsSettingsDialog)
#include "vimmotionssettingsdialogtest.moc"
