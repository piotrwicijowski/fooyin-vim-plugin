#pragma once

#include <utils/settings/settingsentry.h>

#include <QString>
#include <vector>

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::Settings::VimMotions {

Q_NAMESPACE
enum VimMotionsSetting : uint32_t
{
    UseDefaultBindings = 0 | Type::Bool,
    WrapScan,
    PendingSequenceTimeout = 0 | Type::Int,
};
Q_ENUM_NS(VimMotionsSetting)

} // namespace Fooyin::Settings::VimMotions

namespace Fooyin::VimMotions {

struct BindingDefault
{
    const char* key;
    const char* value;
};

class VimMotionsSettings
{
public:
    explicit VimMotionsSettings(SettingsManager* settingsManager);

    static const std::vector<BindingDefault>& defaultBindings();
};

} // namespace Fooyin::VimMotions
