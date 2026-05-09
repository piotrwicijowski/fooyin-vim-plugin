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
