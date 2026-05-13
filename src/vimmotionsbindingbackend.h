#pragma once

#include "vimbindingparser.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::VimMotions {

enum class BindingScope
{
    Global,
    PlaylistView,
    PlaylistOrganiser,
};

inline size_t qHash(BindingScope key, size_t seed = 0)
{
    return std::hash<int>{}(static_cast<int>(key)) ^ seed;
}

enum class BindingMode
{
    Normal,
    Visual,
    Insert,
};

inline size_t qHash(BindingMode key, size_t seed = 0)
{
    return std::hash<int>{}(static_cast<int>(key)) ^ seed;
}

enum class BindingRowSource
{
    Default,
    Custom,
    CustomOverride,
};

enum class BindingRowStatus
{
    Active,
    Disabled,
    Unmapped,
};

struct BindingDefinition
{
    BindingScope scope{BindingScope::Global};
    BindingMode mode{BindingMode::Normal};
    QString keys;
    QString defaultValue;
    std::optional<QString> customValue;

    [[nodiscard]] bool isDefaultBinding() const
    {
        return !defaultValue.isEmpty();
    }
};

struct BindingRow
{
    BindingScope scope{BindingScope::Global};
    BindingMode mode{BindingMode::Normal};
    QString keys;
    QString actionName;
    QString args;
    BindingRowSource source{BindingRowSource::Default};
    BindingRowStatus status{BindingRowStatus::Active};
};

using ScopedBindingEntries = QHash<BindingScope, QList<BindingEntry>>;
using EffectiveBindings    = QHash<BindingMode, ScopedBindingEntries>;

class VimMotionsBindingBackend : public QObject
{
    Q_OBJECT

public:
    explicit VimMotionsBindingBackend(SettingsManager* settingsManager, QObject* parent = nullptr);

    [[nodiscard]] bool settingsUiEnabled() const;
    [[nodiscard]] bool useDefaultBindings() const;
    [[nodiscard]] bool wrapScan() const;
    [[nodiscard]] int pendingSequenceTimeout() const;

    void reloadBindings();

    [[nodiscard]] const EffectiveBindings& effectiveBindings() const;
    [[nodiscard]] QList<BindingDefinition> bindingDefinitions() const;
    [[nodiscard]] QList<BindingDefinition> defaultBindingDefinitions() const;
    [[nodiscard]] QList<BindingRow> bindingRows(const QList<BindingDefinition>& definitions,
                                                bool useDefaultBindings) const;

    [[nodiscard]] bool addCustomBinding(QList<BindingDefinition>& definitions, BindingScope scope, BindingMode mode,
                                        const QString& keys, const QString& actionName, const QString& args) const;
    [[nodiscard]] bool updateCustomBinding(QList<BindingDefinition>& definitions, BindingScope originalScope,
                                           BindingMode originalMode, const QString& originalKeys, BindingScope scope,
                                           BindingMode mode, const QString& keys, const QString& actionName,
                                           const QString& args) const;
    [[nodiscard]] bool removeCustomBinding(QList<BindingDefinition>& definitions, BindingScope scope, BindingMode mode,
                                           const QString& keys) const;
    [[nodiscard]] bool resetBinding(QList<BindingDefinition>& definitions, BindingScope scope, BindingMode mode,
                                    const QString& keys) const;
    [[nodiscard]] bool unmapBinding(QList<BindingDefinition>& definitions, BindingScope scope, BindingMode mode,
                                    const QString& keys) const;
    [[nodiscard]] bool saveBindingDefinitions(const QList<BindingDefinition>& definitions);

signals:
    void bindingsChanged();

private:
    [[nodiscard]] EffectiveBindings loadBindings() const;
    [[nodiscard]] static std::optional<BindingMode> modeFromString(const QString& mode);
    [[nodiscard]] static std::optional<BindingScope> scopeFromString(const QString& scope);

    SettingsManager* m_settingsManager{nullptr};
    EffectiveBindings m_effectiveBindings;
};

} // namespace Fooyin::VimMotions
