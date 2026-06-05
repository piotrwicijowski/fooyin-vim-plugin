#include "vimhandler.h"
#include "vimmotionssettings.h"

#include <gui/dsp/dspnumericcontrol.h>
#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QSet>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>

#include <functional>

namespace Fooyin {

class PlaylistView : public QTreeView
{
    Q_OBJECT

public:
    explicit PlaylistView(QWidget* parent = nullptr)
        : QTreeView(parent)
    { }
};

} // namespace Fooyin

namespace {

using namespace Qt::StringLiterals;

constexpr auto TempoDspId = "fooyin.dsp.soundtouch.tempo";

int keyForChar(const QChar ch)
{
    if(ch.isLetter()) {
        return Qt::Key_A + (ch.toLower().unicode() - u'a');
    }
    if(ch.isDigit()) {
        return Qt::Key_0 + ch.digitValue();
    }
    switch(ch.unicode()) {
        case '[':
            return Qt::Key_BracketLeft;
        case ']':
            return Qt::Key_BracketRight;
        default:
            break;
    }
    return Qt::Key_unknown;
}

bool dispatchKey(Fooyin::VimMotions::VimHandler& handler, QObject* watched, const QChar ch)
{
    const Qt::KeyboardModifiers modifiers = ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
    QKeyEvent event(QEvent::KeyPress, keyForChar(ch), modifiers, QString(ch));
    return handler.eventFilter(watched, &event);
}

void focusView(QTreeView* view)
{
    view->show();
    view->setFocus();
    qApp->processEvents();
}

void writeFooyinConfig(const std::function<void(QSettings&)>& configure)
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir{}.mkpath(configDir);

    QSettings fileSettings(configDir + QStringLiteral("/fooyin.conf"), QSettings::IniFormat);
    fileSettings.remove(QStringLiteral("VimMotions"));
    configure(fileSettings);
    fileSettings.sync();
}

class FakeDspNumericControlService : public Fooyin::DspNumericControlService
{
public:
    struct Call
    {
        Fooyin::Engine::DspChainScope scope{Fooyin::Engine::DspChainScope::Master};
        uint64_t instanceId{0};
        double value{0.0};
        bool persist{false};
    };

    [[nodiscard]] bool supportsNumericControl(const QString& dspId) const override
    {
        return supportedIds.contains(dspId);
    }

    [[nodiscard]] std::vector<Fooyin::DspNumericTarget> targetsFor(const QString& dspId) const override
    {
        std::vector<Fooyin::DspNumericTarget> matches;
        for(const auto& target : targets) {
            if(target.dspId == dspId) {
                matches.push_back(target);
            }
        }
        return matches;
    }

    bool setValue(Fooyin::Engine::DspChainScope scope, uint64_t instanceId, double value, bool persist) override
    {
        calls.push_back(Call{.scope = scope, .instanceId = instanceId, .value = value, .persist = persist});
        return setValueResult;
    }

    QSet<QString> supportedIds;
    std::vector<Fooyin::DspNumericTarget> targets;
    std::vector<Call> calls;
    bool setValueResult{true};
};

} // namespace

using namespace Fooyin;
using namespace Fooyin::VimMotions;

class TestVimHandlerDspControl : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void adjustDspValueUpdatesSingleMatchingTarget();
    void adjustDspValueCountMultipliesDelta();
    void setDspValueIgnoresCount();
    void adjustDspValueNoOpsWhenNoTargetsMatch();
    void adjustDspValueNoOpsWhenMultipleTargetsMatch();
    void adjustDspValueRejectsInvalidArgs();
};

void TestVimHandlerDspControl::adjustDspValueUpdatesSingleMatchingTarget()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_dsp_adjust.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/]t"),
                              QStringLiteral("adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01"));
    });

    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::Master,
                                                          .instanceId   = 101,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setDspNumericControl(&dspControl);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u']'));
    QVERIFY(dispatchKey(handler, &view, u't'));
    QCOMPARE(dspControl.calls.size(), 1U);
    QCOMPARE(dspControl.calls.front().scope, Fooyin::Engine::DspChainScope::Master);
    QCOMPARE(dspControl.calls.front().instanceId, 101U);
    QCOMPARE(dspControl.calls.front().persist, true);
    QCOMPARE(dspControl.calls.front().value, 1.01);
}

void TestVimHandlerDspControl::adjustDspValueCountMultipliesDelta()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_dsp_count.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/]t"),
                              QStringLiteral("adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01"));
    });

    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::Master,
                                                          .instanceId   = 101,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setDspNumericControl(&dspControl);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'5'));
    QVERIFY(dispatchKey(handler, &view, u']'));
    QVERIFY(dispatchKey(handler, &view, u't'));
    QCOMPARE(dspControl.calls.size(), 1U);
    QCOMPARE(dspControl.calls.front().value, 1.05);
}

void TestVimHandlerDspControl::setDspValueIgnoresCount()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_dsp_set.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/s"),
                              QStringLiteral("setDspValue:fooyin.dsp.soundtouch.tempo,1.25"));
    });

    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::Master,
                                                          .instanceId   = 101,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setDspNumericControl(&dspControl);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u'5'));
    QVERIFY(dispatchKey(handler, &view, u's'));
    QCOMPARE(dspControl.calls.size(), 1U);
    QCOMPARE(dspControl.calls.front().value, 1.25);
}

void TestVimHandlerDspControl::adjustDspValueNoOpsWhenNoTargetsMatch()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_dsp_no_target.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/]t"),
                              QStringLiteral("adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01"));
    });

    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setDspNumericControl(&dspControl);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u']'));
    QVERIFY(dispatchKey(handler, &view, u't'));
    QVERIFY(dspControl.calls.empty());
}

void TestVimHandlerDspControl::adjustDspValueNoOpsWhenMultipleTargetsMatch()
{
    SettingsManager settings{QDir::tempPath() + QStringLiteral("/fooyin_vim_dsp_multiple_targets.ini")};
    VimMotionsSettings vimSettings(&settings);
    Q_UNUSED(vimSettings)
    settings.set(QStringLiteral("VimMotions/UseDefaultBindings"), false);

    writeFooyinConfig([](QSettings& fileSettings) {
        fileSettings.setValue(QStringLiteral("VimMotions/Bindings/Global/Normal/]t"),
                              QStringLiteral("adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01"));
    });

    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::Master,
                                                          .instanceId   = 101,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo A"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::PerTrack,
                                                          .instanceId   = 202,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo B"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});

    VimHandler handler;
    handler.setSettingsManager(&settings);
    handler.setDspNumericControl(&dspControl);

    PlaylistView view;
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("A")));
    view.setModel(&model);
    view.setCurrentIndex(model.index(0, 0));
    focusView(&view);

    QVERIFY(dispatchKey(handler, &view, u']'));
    QVERIFY(dispatchKey(handler, &view, u't'));
    QVERIFY(dspControl.calls.empty());
}

void TestVimHandlerDspControl::adjustDspValueRejectsInvalidArgs()
{
    FakeDspNumericControlService dspControl;
    dspControl.supportedIds.insert(QString::fromLatin1(TempoDspId));
    dspControl.targets.push_back(Fooyin::DspNumericTarget{.scope        = Fooyin::Engine::DspChainScope::Master,
                                                          .instanceId   = 101,
                                                          .dspId        = QString::fromLatin1(TempoDspId),
                                                          .name         = QStringLiteral("Tempo"),
                                                          .enabled      = true,
                                                          .supportsLive = true,
                                                          .value        = 1.0});

    VimHandler handler;
    handler.setDspNumericControl(&dspControl);

    handler.adjustDspValue(u"fooyin.dsp.soundtouch.tempo"_s);
    handler.setDspValue(u"fooyin.dsp.soundtouch.tempo,not-a-number"_s);

    QVERIFY(dspControl.calls.empty());
}

QTEST_MAIN(TestVimHandlerDspControl)
#include "vimhandlerdspcontroltest.moc"
