#include "vimbindingparser.h"

#include <optional>

#include <QKeyEvent>
#include <QStringList>

namespace Fooyin::VimMotions {

namespace {

constexpr Qt::KeyboardModifiers kRelevantMods
    = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

struct NamedKeyEntry
{
    const char* name;
    Qt::Key key;
};

constexpr NamedKeyEntry kNamedKeys[] = {
    {"Esc", Qt::Key_Escape}, {"CR", Qt::Key_Return},     {"Enter", Qt::Key_Enter},
    {"Tab", Qt::Key_Tab},    {"BS", Qt::Key_Backspace},  {"Space", Qt::Key_Space},
    {"Del", Qt::Key_Delete}, {"Insert", Qt::Key_Insert}, {"Home", Qt::Key_Home},
    {"End", Qt::Key_End},    {"PageUp", Qt::Key_PageUp}, {"PageDown", Qt::Key_PageDown},
    {"Left", Qt::Key_Left},  {"Right", Qt::Key_Right},   {"Up", Qt::Key_Up},
    {"Down", Qt::Key_Down},  {"Slash", Qt::Key_Slash},   {"Bslash", Qt::Key_Backslash},
    {"Bar", Qt::Key_Bar},    {"Lt", Qt::Key_Less},
};

Qt::Key charToKey(QChar ch)
{
    const ushort uc = ch.unicode();
    if((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9')) {
        return static_cast<Qt::Key>(uc);
    }
    switch(uc) {
        case ';':
            return Qt::Key_Semicolon;
        case '/':
            return Qt::Key_Slash;
        case '.':
            return Qt::Key_Period;
        case ',':
            return Qt::Key_Comma;
        case ' ':
            return Qt::Key_Space;
        case '-':
            return Qt::Key_Minus;
        case '=':
            return Qt::Key_Equal;
        case '[':
            return Qt::Key_BracketLeft;
        case ']':
            return Qt::Key_BracketRight;
        case '(':
            return Qt::Key_ParenLeft;
        case ')':
            return Qt::Key_ParenRight;
        case '!':
            return Qt::Key_Exclam;
        case '@':
            return Qt::Key_At;
        case '#':
            return Qt::Key_NumberSign;
        case '$':
            return Qt::Key_Dollar;
        case '%':
            return Qt::Key_Percent;
        case '^':
            return Qt::Key_AsciiCircum;
        case '&':
            return Qt::Key_Ampersand;
        case '*':
            return Qt::Key_Asterisk;
        case '\'':
            return Qt::Key_Apostrophe;
        case '"':
            return Qt::Key_QuoteDbl;
        case ':':
            return Qt::Key_Colon;
        case '<':
            return Qt::Key_Less;
        case '>':
            return Qt::Key_Greater;
        case '?':
            return Qt::Key_Question;
        case '\\':
            return Qt::Key_Backslash;
        case '|':
            return Qt::Key_Bar;
        case '`':
            return Qt::Key_QuoteLeft;
        case '~':
            return Qt::Key_AsciiTilde;
        case '{':
            return Qt::Key_BraceLeft;
        case '}':
            return Qt::Key_BraceRight;
        case '_':
            return Qt::Key_Underscore;
        case '+':
            return Qt::Key_Plus;
        default:
            return Qt::Key_unknown;
    }
}

Qt::KeyboardModifiers parseModifier(const QString& name)
{
    if(name == QLatin1String("Ctrl"))
        return Qt::ControlModifier;
    if(name == QLatin1String("Alt"))
        return Qt::AltModifier;
    if(name == QLatin1String("Shift"))
        return Qt::ShiftModifier;
    if(name == QLatin1String("Meta"))
        return Qt::MetaModifier;
    return Qt::NoModifier;
}

std::optional<Qt::Key> lookupNamedKey(const QString& name)
{
    for(const auto& entry : kNamedKeys) {
        if(name == QLatin1String(entry.name))
            return entry.key;
    }
    return std::nullopt;
}

QStringList splitOutsideAngles(const QString& s, QChar separator)
{
    QStringList parts;
    QString current;
    int depth = 0;

    for(const QChar ch : s) {
        if(ch == u'<')
            ++depth;
        else if(ch == u'>' && depth > 0)
            --depth;

        if(ch == separator && depth == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }

        current += ch;
    }

    parts.push_back(current);
    return parts;
}

QStringList tokenizeKeySequence(const QString& s)
{
    QStringList tokens;
    QString current;
    bool inSpecial = false;

    for(const QChar ch : s) {
        if(inSpecial) {
            current += ch;
            if(ch == u'>') {
                tokens.push_back(current);
                current.clear();
                inSpecial = false;
            }
            continue;
        }

        if(ch == u'<') {
            if(!current.isEmpty())
                return {};
            current += ch;
            inSpecial = true;
            continue;
        }

        tokens.push_back(QString{ch});
    }

    if(inSpecial)
        return {};

    return tokens;
}

KeyCombo parseSingleKeyToken(const QString& s)
{
    KeyCombo combo;
    combo.modifiers = Qt::NoModifier;

    QString keyName = s;
    if(s.startsWith(u'<') && s.endsWith(u'>') && s.size() >= 3)
        keyName = s.mid(1, s.size() - 2);

    if(auto k = lookupNamedKey(keyName)) {
        combo.key = *k;
        return combo;
    }

    if(s.length() == 1) {
        const QChar c = s[0];
        combo.key     = charToKey(c);
        combo.ch      = c;
        return combo;
    }

    combo.key = Qt::Key_unknown;
    return combo;
}

} // namespace

bool KeyCombo::matches(QKeyEvent* ev) const
{
    const auto evMods = ev->modifiers() & kRelevantMods;

    if(modifiers != Qt::NoModifier) {
        if((evMods & modifiers) != modifiers)
            return false;
        if(ev->key() == key)
            return true;
        if(!ch.isNull()) {
            const QString text = ev->text();
            if(!text.isEmpty())
                return text.front().toUpper() == ch.toUpper();
        }
        return false;
    }

    if(evMods != Qt::NoModifier) {
        if(evMods == Qt::ShiftModifier && !ch.isNull()) {
            const QString text = ev->text();
            if(!text.isEmpty() && text.front() == ch)
                return true;
        }
        return false;
    }

    if(!ch.isNull()) {
        const QString text = ev->text();
        return !text.isEmpty() && text.front() == ch;
    }
    return ev->key() == key;
}

BindingEntry parseBindingString(const QString& keyStr, const QString& valueStr)
{
    BindingEntry entry;

    const int colonIdx = valueStr.indexOf(u':');
    if(colonIdx >= 0) {
        entry.actionName = valueStr.left(colonIdx);
        entry.args       = valueStr.mid(colonIdx + 1);
    }
    else {
        entry.actionName = valueStr;
    }

    const QStringList parts = splitOutsideAngles(keyStr, u'+');

    if(parts.size() > 1) {
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        for(int i = 0; i < parts.size() - 1; ++i)
            mods |= parseModifier(parts[i].trimmed());

        const QString baseKey = parts.last().trimmed();
        KeyCombo combo        = parseSingleKeyToken(baseKey);
        combo.modifiers       = mods;
        entry.keys.push_back(combo);
    }
    else {
        const QStringList tokens = tokenizeKeySequence(keyStr);
        for(const QString& token : tokens)
            entry.keys.push_back(parseSingleKeyToken(token));
    }

    return entry;
}

} // namespace Fooyin::VimMotions
