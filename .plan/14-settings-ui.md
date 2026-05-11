# Vim Motions Settings UI

## Goal

Implement a settings UI for the vim plugin that:

- is guarded by a hidden runtime feature flag so work can land across multiple commits without shipping an incomplete feature
- integrates with fooyin's existing plugin settings flow
- applies changes without requiring an application restart
- includes:
  - `PendingSequenceTimeout` numeric input with `ms` suffix
  - `WrapScan` checkbox
  - `UseDefaultBindings` checkbox
  - placeholder bindings tree view

## Confirmed Decisions

- Integration target: `Settings -> Plugins -> Vim Motions -> Configure`
- Feature flag strategy: hidden runtime flag, disabled by default
- Apply timing: changes only need to take effect on dialog `OK` / `Apply`
- Placeholder tree assumption: show current effective bindings read-only rather than an empty placeholder

## Constraints

- Use fooyin public plugin APIs only; do not depend on fooyin private/internal headers
- Keep the project buildable and runnable after every stage
- Prefer dormant/stubbed code over partially exposed UI
- Update `README.md` when the feature becomes intentionally user-visible
- Run `clang-format` on touched source files during implementation stages
- Build and test after code-complete stages:

```bash
cmake --build build
cmake -B build-test -DBUILD_TESTING=ON -G Ninja
cmake --build build-test
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test -V
```

## Architecture Notes

- The public fooyin integration point for this plugin is `PluginConfigGuiPlugin` plus `PluginSettingsProvider`.
- The current plugin already uses typed settings for `WrapScan`, `UseDefaultBindings`, and `PendingSequenceTimeout`.
- The current binding reload path is incomplete for runtime updates because custom non-default bindings are still discovered by scanning `fooyin.conf` during rebuild logic.
- The implementation should separate UI concerns from binding persistence/reload logic so the dialog does not own config parsing.

## Stage 1: Feature-Gated Entry Point

Add the dormant UI entry seam without exposing unfinished functionality.

Deliverables:

- Add a hidden runtime flag such as `VimMotions/EnableSettingsUi=false`
- Extend the plugin to implement `PluginConfigGuiPlugin`
- Register a minimal `PluginSettingsProvider`
- When the flag is disabled, keep the feature inaccessible or return a harmless stub dialog/message path
- Add minimal scaffolding classes/files for the future dialog

Acceptance criteria:

- Plugin builds and loads unchanged with the flag off
- No user-visible half-finished settings UI is exposed by default

## Stage 2: Settings/Bindings Backend Refactor

Extract and normalize config loading so runtime apply is reliable.

Deliverables:

- Introduce a small backend/service layer for vim settings and binding persistence
- Move effective binding load/rebuild responsibilities out of direct ad hoc UI code paths
- Provide one explicit reload entry point that can be called after settings apply
- Preserve existing typed `SettingsManager` subscriptions for:
  - `WrapScan`
  - `UseDefaultBindings`
  - `PendingSequenceTimeout`
- Refactor custom binding discovery so non-default bindings can also refresh in-process

Acceptance criteria:

- Effective bindings can be rebuilt after settings changes without restarting fooyin
- No behavior regressions for existing startup-loaded bindings

## Stage 3: Dialog Skeleton

Build the real dialog shell behind the feature flag.

Deliverables:

- Create a settings dialog opened by the plugin settings provider
- Add controls for:
  - `PendingSequenceTimeout` via `QSpinBox` with `ms` suffix
  - `WrapScan` checkbox
  - `UseDefaultBindings` checkbox
  - read-only placeholder `QTreeView`
- Keep the tree non-editable in this stage
- Prefer standard public Qt widgets unless a suitable public fooyin widget exists

Acceptance criteria:

- Dialog opens successfully when the feature flag is enabled
- The dialog remains dormant when the feature flag is disabled
- No settings are persisted yet beyond safe scaffold behavior

## Stage 4: Load / Apply / Reset Wiring

Wire the dialog to real settings persistence and runtime reload.

Deliverables:

- Load current values from `SettingsManager` and the bindings backend when the dialog opens
- Persist checkbox and timeout settings on `Apply` / `OK`
- Persist binding data through the backend, even if the tree remains read-only for now
- Trigger runtime reload after apply so changes take effect immediately
- Support `Reset` behavior for the implemented controls

Acceptance criteria:

- Changing timeout and checkboxes updates runtime behavior after `Apply` / `OK`
- Restart is not required for settings-page-driven changes

## Stage 5: Read-Only Effective Bindings Tree

Make the placeholder tree useful without committing to editing UX yet.

Deliverables:

- Populate the tree with current effective bindings
- Prefer stable columns such as:
  - `Mode`
  - `Keys`
  - `Action`
  - optional `Source`
- Refresh the tree after apply/reload
- Keep all editing affordances deferred to a later plan

Acceptance criteria:

- Users can inspect the active effective bindings from the settings dialog
- Tree contents stay in sync after apply and reload

## Stage 6: Documentation and Visibility Review

Expose and document the feature intentionally once the interim state is acceptable.

Deliverables:

- Update `README.md` to document the settings UI once it is intentionally user-visible
- Replace any restart-required wording that no longer applies to settings-page-driven changes
- Re-evaluate whether the feature flag should remain off by default until editable bindings land

Acceptance criteria:

- Documentation matches the shipped behavior
- The visibility decision is explicit rather than accidental

## Testing Plan

Implementation stages should include tests as deliverables, not follow-up work.

### Unit tests

Add or extend `tests/bindingparsertest.cpp` for:

- binding string parsing used by the tree display
- sequence forms such as `gg`, `g<Space>`, `<Slash>`
- modifier combinations
- any formatting/parsing helpers introduced for UI display or persistence

### Integration tests

Add or extend `tests/bindingintegrationtest.cpp` for:

- persistence of `PendingSequenceTimeout`
- persistence of `WrapScan`
- persistence of `UseDefaultBindings`
- custom binding persistence
- effective binding reload without restart after apply
- interactions between `UseDefaultBindings` and custom overrides/unmaps

### Optional UI-focused coverage

If dialog behavior becomes non-trivial, add a dedicated test file for:

- load/apply/reset behavior
- feature-flag-gated visibility
- placeholder tree population from backend state

## Risks and Watchpoints

- The current runtime reload gap for non-default bindings is the main technical blocker; solve this before relying on the UI.
- Avoid coupling dialog widgets directly to `fooyin.conf` parsing.
- Avoid private fooyin settings-page internals; stay on the public plugin-settings provider path.
- Keep the hidden flag default-off until the staged implementation is coherent enough to ship.

## Progress Tracking

Use this section across sessions. Update it as work lands so future sessions do not need prior chat context.

### Overall status

- Status: stage 3 complete
- Feature flag default: disabled
- Shipping state: dormant

### Stage checklist

- [x] Stage 1: Feature-gated entry point
- [x] Stage 2: Settings/bindings backend refactor
- [x] Stage 3: Dialog skeleton
- [ ] Stage 4: Load / Apply / Reset wiring
- [ ] Stage 5: Read-only effective bindings tree
- [ ] Stage 6: Documentation and visibility review

### Session log

- 2026-05-11: Plan created. No implementation started.
- 2026-05-11: Stage 1 implemented with hidden settings UI flag, config provider seam, stub dialog scaffold, and default-off test coverage.
- 2026-05-11: Stage 2 implemented with binding backend extraction, handler/plugin wiring, and reload coverage for custom bindings.
- 2026-05-11: Stage 3 implemented with a feature-gated settings dialog scaffold, placeholder bindings tree, and widget coverage.

### Notes for future sessions

- Reconfirm whether the feature flag should hide the configure action entirely or allow opening a stub dialog while disabled.
- If a later stage adds default configurable bindings, update the README explicit default configuration section as required by repo rules.
- Preserve buildable intermediate states even if some classes are placeholders.
