#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>
#include <gui/plugins/pluginconfigguiplugin.h>

#include <QApplication>
#include <QMessageBox>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QtTest>

class TestPluginLoad : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadsWithCurrentInterfaces()
    {
        const QString pluginPath = qEnvironmentVariable("VIM_MOTIONS_PLUGIN_PATH");
        QVERIFY(!pluginPath.isEmpty());

        QPluginLoader loader{pluginPath};
        QCOMPARE(loader.metaData().value(QStringLiteral("IID")).toString(), QStringLiteral(FOOYIN_PLUGIN_IID));
        const auto metadata = loader.metaData().value(QStringLiteral("MetaData")).toObject();
        QCOMPARE(metadata.value(QStringLiteral("Name")).toString(), QStringLiteral("Vim Motions"));
        QVERIFY(!metadata.value(QStringLiteral("Version")).toString().isEmpty());
        const auto author = metadata.value(QStringLiteral("Author")).toString();
        QVERIFY(!author.isEmpty());
        // Match the host's installer identity rules using public Qt APIs.
        const auto identifier = (author + QStringLiteral(".") + metadata.value(QStringLiteral("Name")).toString())
                                    .simplified()
                                    .remove(QLatin1Char(' '))
                                    .toLower();
        const QRegularExpression validIdentifier{QStringLiteral("^[a-z0-9][a-z0-9._-]*$")};
        QVERIFY2(validIdentifier.match(identifier).hasMatch(), qPrintable(identifier));

        QObject* plugin = loader.instance();
        QVERIFY2(plugin, qPrintable(loader.errorString()));
        QVERIFY(qobject_cast<Fooyin::Plugin*>(plugin));
        QVERIFY(qobject_cast<Fooyin::CorePlugin*>(plugin));
        QVERIFY(qobject_cast<Fooyin::GuiPlugin*>(plugin));
        auto* configPlugin = qobject_cast<Fooyin::PluginConfigGuiPlugin*>(plugin);
        QVERIFY(configPlugin);

        {
            auto provider = configPlugin->settingsProvider();
            QVERIFY(provider);
            provider->showSettings(nullptr);

            QMessageBox* dialog{nullptr};
            for(QWidget* widget : QApplication::topLevelWidgets()) {
                if(auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                    dialog = messageBox;
                    break;
                }
            }
            QVERIFY(dialog);
            QVERIFY(dialog->isVisible());

            provider->showSettings(nullptr);
            QVERIFY(dialog->isVisible());
            dialog->close();
            QCoreApplication::processEvents();
        }

        QVERIFY(loader.unload());
    }
};

QTEST_MAIN(TestPluginLoad)

#include "pluginloadtest.moc"
