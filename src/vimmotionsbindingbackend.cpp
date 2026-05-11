#include "vimmotionsbindingbackend.h"

#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace Fooyin::VimMotions {

namespace {

QSettings* settingsFile(SettingsManager* settingsManager)
{
    return settingsManager ? settingsManager->findChild<QSettings*>() : nullptr;
}

std::optional<BindingMode> modeFromString(const QString& mode)
{
    if(mode == QStringLiteral("Normal"))
        return BindingMode::Normal;
    if(mode == QStringLiteral("Visual"))
        return BindingMode::Visual;
    if(mode == QStringLiteral("Insert"))
        return BindingMode::Insert;

    return std::nullopt;
}

void loadCustomBindingFile(QSettings& settings, const QSet<QString>& seenDefaultKeys,
                           QHash<BindingMode, QList<BindingEntry>>& bindings)
{
    settings.beginGroup(QStringLiteral("VimMotions"));
    const QStringList allKeys = settings.allKeys();

    for(const QString& key : allKeys) {
        if(!key.startsWith(QStringLiteral("Bindings/")))
            continue;

        const QString fullKey = QStringLiteral("VimMotions/") + key;
        if(seenDefaultKeys.contains(fullKey))
            continue;

        const QString value = settings.value(key).toString();
        if(value.isEmpty())
            continue;

        const QStringList parts = fullKey.split(u'/');
        if(parts.size() < 4)
            continue;

        const auto mode = modeFromString(parts[parts.size() - 2]);
        if(!mode)
            continue;

        bindings[*mode].push_back(parseBindingString(parts.last(), value));
    }

    settings.endGroup();
}

} // namespace

VimMotionsBindingBackend::VimMotionsBindingBackend(SettingsManager* settingsManager)
    : m_settingsManager{settingsManager}
{
    reloadBindings();
}

bool VimMotionsBindingBackend::settingsUiEnabled() const
{
    return m_settingsManager && m_settingsManager->value<Settings::VimMotions::EnableSettingsUi>();
}

bool VimMotionsBindingBackend::useDefaultBindings() const
{
    return m_settingsManager && m_settingsManager->value<Settings::VimMotions::UseDefaultBindings>();
}

bool VimMotionsBindingBackend::wrapScan() const
{
    return m_settingsManager && m_settingsManager->value<Settings::VimMotions::WrapScan>();
}

int VimMotionsBindingBackend::pendingSequenceTimeout() const
{
    if(!m_settingsManager)
        return 0;

    return qMax(0, m_settingsManager->value<Settings::VimMotions::PendingSequenceTimeout>());
}

void VimMotionsBindingBackend::reloadBindings()
{
    m_effectiveBindings = loadBindings();
}

const QHash<BindingMode, QList<BindingEntry>>& VimMotionsBindingBackend::effectiveBindings() const
{
    return m_effectiveBindings;
}

QHash<BindingMode, QList<BindingEntry>> VimMotionsBindingBackend::loadBindings() const
{
    QHash<BindingMode, QList<BindingEntry>> bindings;

    if(!m_settingsManager)
        return bindings;

    const bool includeDefaults = useDefaultBindings();
    QSet<QString> seenDefaultKeys;

    for(const auto& binding : VimMotionsSettings::defaultBindings()) {
        const QString fullKey = QString::fromLatin1(binding.key);
        seenDefaultKeys.insert(fullKey);

        if(!includeDefaults && !m_settingsManager->fileContains(fullKey))
            continue;

        const QString value = m_settingsManager->value(fullKey).toString();
        if(value.isEmpty())
            continue;

        const QStringList parts = fullKey.split(u'/');
        if(parts.size() < 4)
            continue;

        const auto mode = modeFromString(parts[parts.size() - 2]);
        if(!mode)
            continue;

        bindings[*mode].push_back(parseBindingString(parts.last(), value));
    }

    if(auto* settings = settingsFile(m_settingsManager)) {
        loadCustomBindingFile(*settings, seenDefaultKeys, bindings);
    }

    const QString appConfigPath
        = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/fooyin.conf");
    if(!settingsFile(m_settingsManager) || settingsFile(m_settingsManager)->fileName() != appConfigPath) {
        QSettings appConfigSettings{appConfigPath, QSettings::IniFormat};
        loadCustomBindingFile(appConfigSettings, seenDefaultKeys, bindings);
    }

    return bindings;
}

std::optional<BindingMode> VimMotionsBindingBackend::modeFromString(const QString& mode)
{
    return ::Fooyin::VimMotions::modeFromString(mode);
}

} // namespace Fooyin::VimMotions
