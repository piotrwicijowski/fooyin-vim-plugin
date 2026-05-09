# fooyin-vim-plugin

This is the development repository for the fooyin vim-mode plugin.

## Workspace layout

Inside the devcontainer:

- `/workspace` — this repository (the plugin source you work on)
- `/fooyin` — fooyin main application source, mounted read-only for reference

The fooyin source at `/fooyin` is the host directory `../fooyin` relative to this repo.

## Devcontainer

Configuration lives in `.devcontainer/`. The container is launched with devpod-cli.
