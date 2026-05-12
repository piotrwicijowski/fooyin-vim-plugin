#include "vimmotionsbindingbackend.h"

#include "vimmotionssettings.h"

#include <utils/settings/settingsmanager.h>

#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

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

QString modeText(BindingMode mode)
{
    switch(mode) {
        case BindingMode::Normal:
            return QStringLiteral("Normal");
        case BindingMode::Visual:
            return QStringLiteral("Visual");
        case BindingMode::Insert:
            return QStringLiteral("Insert");
    }

    return QStringLiteral("Normal");
}

int modeOrder(BindingMode mode)
{
    switch(mode) {
        case BindingMode::Normal:
            return 0;
        case BindingMode::Visual:
            return 1;
        case BindingMode::Insert:
            return 2;
    }

    return 0;
}

QString fullBindingKey(BindingMode mode, const QString& keys)
{
    return QStringLiteral("VimMotions/Bindings/") + modeText(mode) + u'/' + keys;
}

QString bindingValue(const QString& actionName, const QString& args)
{
    if(actionName.isEmpty())
        return {};

    if(args.isEmpty())
        return actionName;

    return actionName + u':' + args;
}

bool isValidBindingEntry(const BindingEntry& entry)
{
    if(entry.actionName.isEmpty() || entry.keys.isEmpty())
        return false;

    for(const auto& combo : entry.keys) {
        if(combo.key == Qt::Key_unknown && combo.ch.isNull())
            return false;
    }

    return true;
}

bool isValidBinding(const QString& keys, const QString& actionName, const QString& args)
{
    return isValidBindingEntry(parseBindingString(keys, bindingValue(actionName, args)));
}

QString customBindingsFilePath(SettingsManager* settingsManager)
{
    if(auto* settings = settingsFile(settingsManager))
        return settings->fileName();

    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/fooyin.conf");
}

void loadCustomBindingFile(QSettings& settings, QHash<QString, QString>* customValues)
{
    settings.beginGroup(QStringLiteral("VimMotions"));
    const QStringList allKeys = settings.allKeys();

    for(const QString& key : allKeys) {
        if(key.startsWith(QStringLiteral("Bindings/")))
            customValues->insert(QStringLiteral("VimMotions/") + key, settings.value(key).toString());
    }

    settings.endGroup();
}

QHash<QString, QString> loadCustomBindingValues(SettingsManager* settingsManager)
{
    QHash<QString, QString> customValues;

    if(auto* settings = settingsFile(settingsManager))
        loadCustomBindingFile(*settings, &customValues);

    const QString appConfigPath
        = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/fooyin.conf");
    if(!settingsFile(settingsManager) || settingsFile(settingsManager)->fileName() != appConfigPath) {
        QSettings appConfigSettings{appConfigPath, QSettings::IniFormat};
        loadCustomBindingFile(appConfigSettings, &customValues);
    }

    return customValues;
}

BindingDefinition* findDefinition(QList<BindingDefinition>& definitions, BindingMode mode, const QString& keys)
{
    const auto it = std::find_if(definitions.begin(), definitions.end(), [mode, &keys](const auto& definition) {
        return definition.mode == mode && definition.keys == keys;
    });
    return it == definitions.end() ? nullptr : &(*it);
}

const BindingDefinition* findDefinition(const QList<BindingDefinition>& definitions, BindingMode mode,
                                        const QString& keys)
{
    const auto it = std::find_if(definitions.begin(), definitions.end(), [mode, &keys](const auto& definition) {
        return definition.mode == mode && definition.keys == keys;
    });
    return it == definitions.end() ? nullptr : &(*it);
}

void sortDefinitions(QList<BindingDefinition>& definitions)
{
    std::sort(definitions.begin(), definitions.end(), [](const auto& left, const auto& right) {
        const int leftOrder  = modeOrder(left.mode);
        const int rightOrder = modeOrder(right.mode);
        if(leftOrder != rightOrder)
            return leftOrder < rightOrder;
        return left.keys < right.keys;
    });
}

BindingRow bindingRowFromValue(BindingMode mode, const QString& keys, const QString& value, BindingRowSource source,
                               BindingRowStatus status)
{
    BindingRow row;
    row.mode   = mode;
    row.keys   = keys;
    row.source = source;
    row.status = status;

    const BindingEntry entry = parseBindingString(keys, value);
    row.actionName           = entry.actionName;
    row.args                 = entry.args;
    return row;
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

QList<BindingDefinition> VimMotionsBindingBackend::bindingDefinitions() const
{
    QList<BindingDefinition> definitions = defaultBindingDefinitions();
    const auto customValues              = loadCustomBindingValues(m_settingsManager);

    for(auto it = customValues.constBegin(); it != customValues.constEnd(); ++it) {
        const QStringList parts = it.key().split(u'/');
        if(parts.size() < 4)
            continue;

        const auto mode = modeFromString(parts[parts.size() - 2]);
        if(!mode)
            continue;

        const QString keys = parts.last();
        if(auto* definition = findDefinition(definitions, *mode, keys)) {
            definition->customValue = it.value();
            continue;
        }

        BindingDefinition definition;
        definition.mode        = *mode;
        definition.keys        = keys;
        definition.customValue = it.value();
        definitions.push_back(definition);
    }

    definitions.erase(std::remove_if(definitions.begin(), definitions.end(),
                                     [](const auto& definition) {
                                         return !definition.isDefaultBinding()
                                             && (!definition.customValue.has_value()
                                                 || definition.customValue->isEmpty());
                                     }),
                      definitions.end());
    sortDefinitions(definitions);
    return definitions;
}

QList<BindingDefinition> VimMotionsBindingBackend::defaultBindingDefinitions() const
{
    QList<BindingDefinition> definitions;

    for(const auto& binding : VimMotionsSettings::defaultBindings()) {
        const QString fullKey   = QString::fromLatin1(binding.key);
        const QStringList parts = fullKey.split(u'/');
        if(parts.size() < 4)
            continue;

        const auto mode = modeFromString(parts[parts.size() - 2]);
        if(!mode)
            continue;

        BindingDefinition definition;
        definition.mode         = *mode;
        definition.keys         = parts.last();
        definition.defaultValue = QString::fromLatin1(binding.value);
        definitions.push_back(definition);
    }

    sortDefinitions(definitions);
    return definitions;
}

QList<BindingRow> VimMotionsBindingBackend::bindingRows(const QList<BindingDefinition>& definitions,
                                                        bool useDefaultBindings) const
{
    QList<BindingRow> rows;

    for(const auto& definition : definitions) {
        if(definition.isDefaultBinding()) {
            if(definition.customValue.has_value()) {
                if(definition.customValue->isEmpty()) {
                    rows.push_back(bindingRowFromValue(definition.mode, definition.keys, definition.defaultValue,
                                                       BindingRowSource::CustomOverride, BindingRowStatus::Unmapped));
                }
                else {
                    rows.push_back(bindingRowFromValue(definition.mode, definition.keys, *definition.customValue,
                                                       BindingRowSource::CustomOverride, BindingRowStatus::Active));
                }
                continue;
            }

            rows.push_back(bindingRowFromValue(
                definition.mode, definition.keys, definition.defaultValue, BindingRowSource::Default,
                useDefaultBindings ? BindingRowStatus::Active : BindingRowStatus::Disabled));
            continue;
        }

        if(!definition.customValue.has_value() || definition.customValue->isEmpty())
            continue;

        rows.push_back(bindingRowFromValue(definition.mode, definition.keys, *definition.customValue,
                                           BindingRowSource::Custom, BindingRowStatus::Active));
    }

    std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        const int leftOrder  = modeOrder(left.mode);
        const int rightOrder = modeOrder(right.mode);
        if(leftOrder != rightOrder)
            return leftOrder < rightOrder;
        return left.keys < right.keys;
    });
    return rows;
}

bool VimMotionsBindingBackend::addCustomBinding(QList<BindingDefinition>& definitions, BindingMode mode,
                                                const QString& keys, const QString& actionName,
                                                const QString& args) const
{
    if(!isValidBinding(keys, actionName, args) || findDefinition(definitions, mode, keys))
        return false;

    BindingDefinition definition;
    definition.mode        = mode;
    definition.keys        = keys;
    definition.customValue = bindingValue(actionName, args);
    definitions.push_back(definition);
    sortDefinitions(definitions);
    return true;
}

bool VimMotionsBindingBackend::updateCustomBinding(QList<BindingDefinition>& definitions, BindingMode originalMode,
                                                   const QString& originalKeys, BindingMode mode, const QString& keys,
                                                   const QString& actionName, const QString& args) const
{
    if(!isValidBinding(keys, actionName, args))
        return false;

    BindingDefinition* original = findDefinition(definitions, originalMode, originalKeys);
    if(!original)
        return false;

    if((originalMode != mode || originalKeys != keys) && findDefinition(definitions, mode, keys))
        return false;

    BindingDefinition updated = *original;
    updated.mode              = mode;
    updated.keys              = keys;
    updated.customValue       = bindingValue(actionName, args);

    definitions.erase(std::remove_if(definitions.begin(), definitions.end(),
                                     [originalMode, &originalKeys](const auto& def) {
                                         return def.mode == originalMode && def.keys == originalKeys;
                                     }),
                      definitions.end());
    definitions.push_back(updated);
    sortDefinitions(definitions);
    return true;
}

bool VimMotionsBindingBackend::removeCustomBinding(QList<BindingDefinition>& definitions, BindingMode mode,
                                                   const QString& keys) const
{
    const BindingDefinition* definition = findDefinition(definitions, mode, keys);
    if(!definition || definition->isDefaultBinding())
        return false;

    definitions.erase(std::remove_if(definitions.begin(), definitions.end(),
                                     [mode, &keys](const auto& def) { return def.mode == mode && def.keys == keys; }),
                      definitions.end());
    return true;
}

bool VimMotionsBindingBackend::resetBinding(QList<BindingDefinition>& definitions, BindingMode mode,
                                            const QString& keys) const
{
    BindingDefinition* definition = findDefinition(definitions, mode, keys);
    if(!definition)
        return false;

    if(definition->isDefaultBinding()) {
        definition->customValue.reset();
        sortDefinitions(definitions);
        return true;
    }

    return removeCustomBinding(definitions, mode, keys);
}

bool VimMotionsBindingBackend::unmapBinding(QList<BindingDefinition>& definitions, BindingMode mode,
                                            const QString& keys) const
{
    BindingDefinition* definition = findDefinition(definitions, mode, keys);
    if(!definition || !definition->isDefaultBinding())
        return false;

    definition->customValue = QString{};
    return true;
}

bool VimMotionsBindingBackend::saveBindingDefinitions(const QList<BindingDefinition>& definitions)
{
    QSettings settings(customBindingsFilePath(m_settingsManager), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("VimMotions"));

    const QStringList allKeys = settings.allKeys();
    for(const QString& key : allKeys) {
        if(key.startsWith(QStringLiteral("Bindings/")))
            settings.remove(key);
    }

    for(const auto& definition : definitions) {
        if(!definition.customValue.has_value())
            continue;

        settings.setValue(QStringLiteral("Bindings/") + modeText(definition.mode) + u'/' + definition.keys,
                          *definition.customValue);
    }

    settings.endGroup();
    settings.sync();
    reloadBindings();
    return settings.status() == QSettings::NoError;
}

QHash<BindingMode, QList<BindingEntry>> VimMotionsBindingBackend::loadBindings() const
{
    QHash<BindingMode, QList<BindingEntry>> bindings;

    for(const auto& row : bindingRows(bindingDefinitions(), useDefaultBindings())) {
        if(row.status != BindingRowStatus::Active)
            continue;

        bindings[row.mode].push_back(parseBindingString(row.keys, bindingValue(row.actionName, row.args)));
    }

    return bindings;
}

std::optional<BindingMode> VimMotionsBindingBackend::modeFromString(const QString& mode)
{
    return ::Fooyin::VimMotions::modeFromString(mode);
}

} // namespace Fooyin::VimMotions
