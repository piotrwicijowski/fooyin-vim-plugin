#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <Qt>

class QKeyEvent;

namespace Fooyin::VimMotions {

struct KeyCombo
{
    Qt::Key key{Qt::Key_unknown};
    QChar ch{};
    Qt::KeyboardModifiers modifiers{Qt::NoModifier};

    bool matches(QKeyEvent* ev) const;
};

inline bool operator==(const KeyCombo& a, const KeyCombo& b)
{
    return a.key == b.key && a.ch == b.ch && a.modifiers == b.modifiers;
}

inline size_t qHash(const KeyCombo& k, size_t seed = 0)
{
    return qHashMulti(seed, static_cast<quint32>(k.key), static_cast<quint32>(k.modifiers), k.ch.unicode());
}

struct BindingEntry
{
    QList<KeyCombo> keys;
    QString actionName;
    QString args;

    [[nodiscard]] bool isMultiKey() const
    {
        return keys.size() > 1;
    }
};

BindingEntry parseBindingString(const QString& keyStr, const QString& valueStr);

} // namespace Fooyin::VimMotions
