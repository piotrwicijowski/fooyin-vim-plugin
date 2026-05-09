#pragma once

#include <QHash>
#include <QString>
#include <QStringView>
#include <functional>

namespace Fooyin::VimMotions {

class VimHandler;

class VimActions
{
public:
    using HandlerFn = std::function<void(VimHandler&, const QStringView& args)>;

    void registerAction(const QString& name, HandlerFn handler);

    [[nodiscard]] HandlerFn find(const QString& name) const;

    // Registers all built-in actions (the default set)
    void registerAll();

private:
    QHash<QString, HandlerFn> m_actions;
};

} // namespace Fooyin::VimMotions
