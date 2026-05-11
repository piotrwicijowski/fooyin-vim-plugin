#pragma once

#include "vimbindingparser.h"
#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::VimMotions {

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

class VimMotionsBindingBackend
{
public:
    explicit VimMotionsBindingBackend(SettingsManager* settingsManager);

    [[nodiscard]] bool settingsUiEnabled() const;
    [[nodiscard]] bool useDefaultBindings() const;
    [[nodiscard]] bool wrapScan() const;
    [[nodiscard]] int pendingSequenceTimeout() const;

    void reloadBindings();

    [[nodiscard]] const QHash<BindingMode, QList<BindingEntry>>& effectiveBindings() const;

private:
    [[nodiscard]] QHash<BindingMode, QList<BindingEntry>> loadBindings() const;
    [[nodiscard]] static std::optional<BindingMode> modeFromString(const QString& mode);

    SettingsManager* m_settingsManager{nullptr};
    QHash<BindingMode, QList<BindingEntry>> m_effectiveBindings;
};

} // namespace Fooyin::VimMotions
