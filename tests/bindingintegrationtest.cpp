#include "vimbindingparser.h"

#include <QFile>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Fooyin::VimMotions;

// Tests the config-file-to-bindings pipeline used by rebuildBindings():
// read from QSettings, skip empty values, skip defaults, parse each entry.
class TestConfigFileBindings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_filePath;

    // Simulates the rebuildBindings() logic:
    // - reads all keys under [VimMotions] with "Bindings/" prefix
    // - skips if key is in skipDefaults (full key = "VimMotions/Bindings/...")
    // - skips empty values when skipEmpty=true
    // - parses via parseBindingString
    // Returns formatted entries: "[Mode] keyCombo = actionName"
    QStringList loadParsedBindings(const QSet<QString>& skipDefaults = {},
                                   bool skipEmpty = false)
    {
        static const QString group = QStringLiteral("VimMotions");

        QSettings settings(m_filePath, QSettings::IniFormat);
        settings.beginGroup(group);
        const QStringList allKeys = settings.allKeys();
        QStringList result;

        for (const QString& key : allKeys) {
            if (!key.startsWith(QStringLiteral("Bindings/"))) continue;

            const QString fullKey = group + u'/' + key;
            if (skipDefaults.contains(fullKey)) continue;

            const QString val = settings.value(key).toString();
            if (skipEmpty && val.isEmpty()) continue;

            auto entry = parseBindingString(
                key.section(u'/', -1), val);
            if (entry.actionName.isEmpty()) continue;

            result.append(u'[' + key.section(u'/', 1, 1)
                          + QStringLiteral("] ") + key.section(u'/', -1)
                          + QStringLiteral(" = ") + entry.actionName);
        }
        settings.endGroup();
        return result;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        m_filePath = m_tempDir.filePath(QStringLiteral("test.conf"));

        QFile file(m_filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(
            "[VimMotions]\n"
            "Bindings\\Normal\\j=moveCursor:+1\n"
            "Bindings\\Normal\\k=moveCursor:-1\n"
            "Bindings\\Normal\\dd=\n"
            "Bindings\\Normal\\d=deleteRows\n"
            "Bindings\\Normal\\f=deleteRows\n"
            "Bindings\\Visual\\j=extendCursor:+1\n"
        );
        file.close();
    }

    // All non-empty, well-formed bindings should be parsed (dd= is skipped
    // because it produces an empty actionName)
    void testAllBindings()
    {
        auto bindings = loadParsedBindings({}, false);
        QCOMPARE(bindings.size(), 5);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] j = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] k = moveCursor")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Visual] j = extendCursor")));
    }

    // Empty values should be skipped when skipEmpty=true
    void testSkipEmptyValues()
    {
        auto bindings = loadParsedBindings({}, true);
        QCOMPARE(bindings.size(), 5); // same as without skipEmpty for this file
    }

    // UseDefaultBindings=false: skip default keys
    void testSkipDefaults()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));

        auto bindings = loadParsedBindings(defaults, true);

        // j should be skipped (it's in defaults),
        // leaving k, d, f, and Visual/j
        QCOMPARE(bindings.size(), 4);
    }

    // Skip all known keys: no custom bindings remain
    void testAllSkipped()
    {
        QSet<QString> skipAll;
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/dd"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/d"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Normal/f"));
        skipAll.insert(QStringLiteral("VimMotions/Bindings/Visual/j"));

        auto bindings = loadParsedBindings(skipAll, true);
        QCOMPARE(bindings.size(), 0);
    }

    // Only non-default keys (d, f) survive when defaults are skipped
    void testNonDefaultKeysOnly()
    {
        QSet<QString> defaults;
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/j"));
        defaults.insert(QStringLiteral("VimMotions/Bindings/Normal/k"));

        auto bindings = loadParsedBindings(defaults, true);

        // j, k skipped (defaults); d, f parsed; Visual/j not in defaults
        QCOMPARE(bindings.size(), 3);
        QVERIFY(bindings.contains(QStringLiteral("[Normal] d = deleteRows")));
        QVERIFY(bindings.contains(QStringLiteral("[Normal] f = deleteRows")));
    }
};

QTEST_MAIN(TestConfigFileBindings)
#include "bindingintegrationtest.moc"