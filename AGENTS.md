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

## Formatting

After making code changes, run `clang-format` using this repository's `.clang-format` and format the touched source files before finishing the task.

## Header usage

Do not add dependencies on fooyin internal or private headers from `src/` that are not part of fooyin's installed public headers. Plugin code must build against installed fooyin headers alone. If a feature appears to require private GUI/controller types, stop and plan an upstream public API instead of including internal headers.

### Default binding documentation

When adding a new default configurable binding (in `VimMotionsSettings::defaultBindings()` in `vimmotionssettings.cpp`), the binding MUST also be added to the **Explicit default configuration** section in `README.md`. This section lists all default bindings in INI format for users who set `UseDefaultBindings=false`. Failure to add the entry there means the binding will be effectively invisible to users in that mode.

## Build and test verification

Once a feature is code-complete, build the plugin and build and run the tests:

```bash
cmake --build build
cmake -B build-test -DBUILD_TESTING=ON -G Ninja
cmake --build build-test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test -V
```

## Planning new features

When planning a new feature, the implementation plan MUST include unit tests (via `tests/bindingparsertest.cpp`) and integration tests (via `tests/bindingintegrationtest.cpp`) that cover the new functionality. Tests are a deliverable, not an afterthought.
