# fooyin-vim-plugin

This is the development repository for the fooyin vim-mode plugin.

## Workspace layout

Inside the devcontainer:

- `/workspace` — this repository (the plugin source you work on)
- `/fooyin` — fooyin main application source, mounted read-only for reference

The fooyin source at `/fooyin` is the host directory `../fooyin` relative to this repo.

## Devcontainer

Configuration lives in `.devcontainer/`. The container is launched with devpod-cli.

## README maintenance

When making changes to features, key bindings, or modes, update `README.md` to match. The README documents all modes, bindings, and behaviour — keep it in sync with the code.

### Default binding documentation

When adding a new default configurable binding (in `VimMotionsSettings::defaultBindings()` in `vimmotionssettings.cpp`), the binding MUST also be added to the **Explicit default configuration** section in `README.md`. This section lists all default bindings in INI format for users who set `UseDefaultBindings=false`. Failure to add the entry there means the binding will be effectively invisible to users in that mode.
