#include "vimbindingparser.h"
#include "vimmotionsbindingbackend.h"
#include "vimmotionssettings.h"
#include "vimmotionssettingsdialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include <utils/settings/settingsmanager.h>

using namespace Fooyin::VimMotions;

class TestConfigFileBindings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_filePath;

    QStringList loadParsedBindings(const QSet<QString>& skipDefaults = {}, bool skipEmpty = false)
    {
        static const QString group = QStringLiteral("VimMotions");

        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.beginGroup(group);
        const QStringList allKeys = settings.allKeys();
        QStringList result;

        for(const QString& key : allKeys) {
            if(!key.startsWith(QStringLiteral("Bindings/")))
                continue;

            const QString fullKey = group + u'/' + key;
            if(skipDefaults.contains(fullKey))
                continue;

            const QString val = settings.value(key).toString();
            if(skipEmpty && val.isEmpty())
                continue;

            auto entry = parseBindingString(key.section(u'/', -1), val);
            if(entry.actionName.isEmpty())
                continue;

            result.append(u'[' + key.section(u'/', 1, 1) + QStringLiteral("] ") + key.section(u'/', -1)
                          + QStringLiteral(" = ") + entry.actionName);
        }
        settings.endGroup();
        return result;
    }

    int loadPendingSequenceTimeout() const
    {
        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("VimMotions"));
        const int timeout = settings.value(QStringLiteral("PendingSequenceTimeout"), 0).toInt();
        settings.endGroup();
        return timeout;
    }

    static bool hasBinding(const QList<BindingEntry>& bindings, QChar key, const QString& action)
    {
        for(const auto& binding : bindings) {
            if(binding.actionName != action || binding.keys.size() != 1)
                continue;

            if(binding.keys.front().ch == key)
                return true;
        }

        return false;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        m_filePath = m_tempDir.filePath(QStringLiteral("test.conf"));

        QFile file(m_filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[VimMotions]\n"
                   "Bindings\\Normal\\j=moveCursor:+1\n"
                   "Bindings\\Normal\\k=moveCursor:-1\n"
                   "Bindings\\Normal\\m=beginSetMark\n"
                   "Bindings\\Normal\\'=beginJumpToMark\n"
                   "Bindings\\Normal\\`=beginJumpToMark\n"
                   "Bindings\\Normal\\a=organiserCreatePlaylist\n"
                   "Bindings\\Normal\\A=organiserCreateGroup\n"
                   "Bindings\\Normal\\dd=\n"
                   "Bindings\\Normal\\d=deleteRows\n"
                   "Bindings\\Normal\\f=deleteRows\n"
                   "Bindings\\Normal\\Ctrl+A=selectAll\n"
                   "Bindings\\Normal\\<Space>=fooyinAction:Playback.Next\n"
                   "Bindings\\Normal\\g<Space>=focusNowPlaying\n"
                   "Bindings\\Normal\\yc=copyAfterCurrentPlaying\n"
                   "Bindings\\Normal\\dc=moveAfterCurrentPlaying\n"
                   "Bindings\\Visual\\j=extendCursor:+1\n"
                   "Bindings\\Visual\\v=leaveVisualMode\n"
                   "Bindings\\Visual\\Ctrl+J=spatialMoveFocus:down\n");
        file.close();
    }

    void testAllBindings()
    {
        auto bindings = loadParsedBindings({}, false);
        QCOMPARE(bindings.size(), 17);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] j = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] k = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] j = extendCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] Ctrl+J = spatialMoveFocus")));
    }

    void testSkipEmptyValues()
    {
        auto bindings = loadParsedBindings({}, true);
        QCOMPARE(bindings.size(), 17);
    }

    void testSkipDefaults()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 16);
    }

    void testAllSkipped()
    {
        QSet<QString> skipAll;
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/m"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/'"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/`"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/a"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/dd"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/d"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/f"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/Ctrl+A"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/g<Space>"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/yc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/dc"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/v"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/Ctrl+J"));

        auto bindings = loadParsedBindings(skipAll, true);
        QCOMPARE(bindings.size(), 0);
    }

    void testNonDefaultKeysOnly()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));

        auto bindings = loadParsedBindings(defaults, true);

        QCOMPARE(bindings.size(), 15);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] Ctrl+A = selectAll")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] <Space> = fooyinAction")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] g<Space> = focusNowPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] yc = copyAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] dc = moveAfterCurrentPlaying")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] m = beginSetMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ' = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] ` = beginJumpToMark")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] a = organiserCreatePlaylist")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] A = organiserCreateGroup")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] v = leaveVisualMode")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] Ctrl+J = spatialMoveFocus")));
    }

    void testPendingSequenceTimeoutDefaultsToZero()
    {
        QCOMPARE(loadPendingSequenceTimeout(), 0);
    }

    void testPendingSequenceTimeoutReadsMilliseconds()
    {
        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("VimMotions/PendingSequenceTimeout"), 250);
        settings.sync();
        QCOMPARE(loadPendingSequenceTimeout(), 250);
    }

    void testSettingsUiFlagDefaultsOff()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::EnableSettingsUi>(), false);
    }

    void testBindingBackendReloadsCustomBindings()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.fileSet(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        const auto initialBindings = backend.effectiveBindings().value(BindingMode::Normal);
        QVERIFY(hasBinding(initialBindings, QChar(u'z'), QStringLiteral("focusNowPlaying")));
        QVERIFY(hasBinding(initialBindings, QChar(u'j'), QStringLiteral("moveCursor")));

        QCOMPARE(settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false), true);
        backend.reloadBindings();

        const auto reloadedBindings = backend.effectiveBindings().value(BindingMode::Normal);
        QVERIFY(hasBinding(reloadedBindings, QChar(u'z'), QStringLiteral("focusNowPlaying")));
        QVERIFY(!hasBinding(reloadedBindings, QChar(u'j'), QStringLiteral("moveCursor")));
    }

    void testSettingsDialogLoadsAppliesAndResets()
    {
        Fooyin::SettingsManager settings{m_tempDir.filePath(QStringLiteral("vim_settings_dialog.ini"))};
        VimMotionsSettings vimSettings{&settings};
        Q_UNUSED(vimSettings);

        settings.set<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(120);
        settings.set<Fooyin::Settings::VimMotions::WrapScan>(false);
        settings.set<Fooyin::Settings::VimMotions::UseDefaultBindings>(false);
        settings.fileSet(QStringLiteral("VimMotions/Bindings/Normal/z"), QStringLiteral("focusNowPlaying"));

        VimMotionsBindingBackend backend{&settings};
        VimMotionsSettingsDialog dialog{&settings, &backend};

        auto* timeout = dialog.findChild<QSpinBox*>(QStringLiteral("pendingSequenceTimeout"));
        QVERIFY(timeout);
        QCOMPARE(timeout->value(), 120);

        auto* wrapScan = dialog.findChild<QCheckBox*>(QStringLiteral("wrapScan"));
        QVERIFY(wrapScan);
        QVERIFY(!wrapScan->isChecked());

        auto* useDefaultBindings = dialog.findChild<QCheckBox*>(QStringLiteral("useDefaultBindings"));
        QVERIFY(useDefaultBindings);
        QVERIFY(!useDefaultBindings->isChecked());

        timeout->setValue(450);
        wrapScan->setChecked(true);
        useDefaultBindings->setChecked(true);

        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons);
        auto* applyButton = buttons->button(QDialogButtonBox::Apply);
        QVERIFY(applyButton);
        applyButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 450);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(backend.pendingSequenceTimeout(), 450);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'j'),
                           QStringLiteral("moveCursor")));

        timeout->setValue(30);
        wrapScan->setChecked(false);
        useDefaultBindings->setChecked(false);

        auto* resetButton = buttons->button(QDialogButtonBox::Reset);
        QVERIFY(resetButton);
        resetButton->click();

        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::PendingSequenceTimeout>(), 0);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::WrapScan>(), true);
        QCOMPARE(settings.value<Fooyin::Settings::VimMotions::UseDefaultBindings>(), true);
        QCOMPARE(timeout->value(), 0);
        QVERIFY(wrapScan->isChecked());
        QVERIFY(useDefaultBindings->isChecked());
        QCOMPARE(backend.pendingSequenceTimeout(), 0);
        QCOMPARE(backend.wrapScan(), true);
        QCOMPARE(backend.useDefaultBindings(), true);
        QVERIFY(hasBinding(backend.effectiveBindings().value(BindingMode::Normal), QChar(u'j'),
                           QStringLiteral("moveCursor")));
    }
};

QTEST_MAIN(TestConfigFileBindings)
#include "bindingintegrationtest.moc"
