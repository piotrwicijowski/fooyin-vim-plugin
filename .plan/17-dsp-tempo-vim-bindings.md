# DSP Tempo Vim Bindings Plan

## Goal

Add vim actions and bindings that can incrementally adjust and directly set the SoundTouch tempo multiplier through the new upstream public DSP numeric-control API.

The initial user-facing target is `fooyin.dsp.soundtouch.tempo`, but the binding/action design should stay generic enough to support other numeric DSP parameters exposed through the same upstream API.

## Dependency

This plan depends on the upstream fooyin work described in:

- `/fooyin/.plan/07-public-dsp-numeric-control-api.md`

The vim plugin should not implement this feature by depending on fooyin private headers or by reimplementing SoundTouch serialisation locally.

## User-facing behaviour

### Supported operations

The feature should support two binding styles:

1. incremental adjustment
2. absolute set

Recommended action names:

1. `adjustDspValue`
2. `setDspValue`

Recommended examples:

```ini
[VimMotions]
Bindings\Global\Normal\+ = adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01
Bindings\Global\Normal\- = adjustDspValue:fooyin.dsp.soundtouch.tempo,-0.01
Bindings\Global\Normal\gt = setDspValue:fooyin.dsp.soundtouch.tempo,1.00
Bindings\Global\Normal\gT = setDspValue:fooyin.dsp.soundtouch.tempo,1.25
```

### Count semantics

Counts should apply only to incremental actions.

Examples:

1. `5+` with `adjustDspValue:..., +0.01` applies `+0.05`
2. `3-` with `adjustDspValue:..., -0.01` applies `-0.03`
3. `setDspValue` ignores count and uses the explicit target value from the action arguments

### Ambiguity semantics

For the first version, keep instance targeting strict and predictable.

Resolution policy:

1. if there are no matching DSP instances, log a warning and do nothing
2. if there is exactly one matching numeric target, operate on it
3. if there are multiple matching targets, log a warning and do nothing

This avoids silently changing multiple DSP chain elements at once.

An explicit selector syntax can be a follow-up feature if real-world multi-instance use becomes important.

## Why this shape

The plugin already has a flexible config/action system:

1. action strings already support arbitrary args after `:`
2. multi-key bindings already work
3. counts are already part of `VimHandler` dispatch

So the smallest plugin-side change is to add a small pair of new actions and route them to a public DSP control service.

## Plugin implementation plan

### 1. Extend plugin initialisation to accept the new fooyin service

Add a stored pointer for the new public `DspNumericControlService` and pass it into `VimHandler` during GUI initialisation.

Likely files:

1. `src/vimmotionsplugin.h`
2. `src/vimmotionsplugin.cpp`
3. `src/vimhandler.h`
4. `src/vimhandler.cpp`

### 2. Add handler API for numeric DSP control

Add focused `VimHandler` methods, for example:

1. `adjustDspValue(const QStringView& args)` helper path or equivalent parsed helper methods
2. `setDspValue(const QStringView& args)` helper path or equivalent parsed helper methods

Keep the actual parsing and execution logic in `VimHandler`, not in `VimActions`, so `VimActions` remains a thin registration table.

### 3. Register new actions in `VimActions::registerAll()`

Add:

1. `adjustDspValue`
2. `setDspValue`

The handler lambdas should forward raw action args into `VimHandler`.

### 4. Parse action arguments in the smallest viable format

For the first version, parse comma-separated args:

1. `adjustDspValue:<dspId>,<delta>`
2. `setDspValue:<dspId>,<value>`

Examples:

1. `adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01`
2. `setDspValue:fooyin.dsp.soundtouch.tempo,1.25`

Validation rules:

1. empty `dspId` is invalid
2. missing or non-numeric value is invalid
3. extra fields are invalid for the first version
4. invalid input logs a warning and becomes a no-op

### 5. Resolve targets through the new public service

Execution flow for both actions:

1. confirm `m_dspNumericControl` is available
2. call `supportsNumericControl(dspId)` and/or `targetsFor(dspId)`
3. apply the ambiguity rules above
4. compute the desired value
5. call `setValue(scope, instanceId, desiredValue, true)`

The plugin should not clamp locally if the upstream provider/service already owns clamping semantics.

The plugin may still use optional pre-checks against `minValue` and `maxValue` only for clearer logging, but fooyin should remain the final authority.

### 6. Apply count handling only for incremental updates

For `adjustDspValue`:

1. base delta comes from args
2. multiplier is `currentCount()` when an explicit count was provided, otherwise `1`
3. new value is `target.value + (delta * multiplier)`

For `setDspValue`:

1. ignore count
2. preserve existing count-reset behaviour after action execution

### 7. Keep the scope generic, but document the initial intended DSP

The implementation should not hardcode the SoundTouch tempo DSP id anywhere except in documentation/examples/tests.

The generic action design keeps the plugin reusable if fooyin later exposes other numeric DSP controls.

### 8. Update user documentation

Update `README.md` to document:

1. the two new action names
2. argument syntax
3. count behaviour for incremental changes
4. ambiguity behaviour when multiple DSP instances match
5. example bindings for tempo multiplier increase, decrease, and reset

Do not add default bindings unless there is a strong product decision to do so. If no defaults are added, the README should present this as an advanced optional configuration feature.

## Testing plan

Tests are part of the deliverable.

### Required unit tests

Add parser coverage in `tests/bindingparsertest.cpp` for:

1. `adjustDspValue:fooyin.dsp.soundtouch.tempo,+0.01`
2. `adjustDspValue:fooyin.dsp.soundtouch.tempo,-0.05`
3. `setDspValue:fooyin.dsp.soundtouch.tempo,1.25`

These tests verify that the existing binding parser preserves the new action names and raw argument strings correctly.

### Required integration/config tests

Add coverage in `tests/bindingintegrationtest.cpp` for:

1. settings round-trip for bindings using `adjustDspValue`
2. settings round-trip for bindings using `setDspValue`
3. settings UI acceptance and persistence of argument strings containing DSP ids and numeric values

This satisfies the required config/integration coverage path for new binding functionality.

### Runtime handler tests

Add focused execution coverage by extending an existing `VimHandler` test harness or adding a small dedicated test with a fake `DspNumericControlService`.

Cover at least:

1. single matching target receives the expected persisted update
2. incremental action uses the target's current value plus delta
3. explicit counts multiply the delta
4. `setDspValue` ignores counts
5. no target results in no update
6. multiple matching targets result in no update
7. invalid args result in no update

Keep the fake service minimal: it only needs to return targets and record the last `setValue(...)` request.

## Verification

Once implementation is complete:

```bash
cmake -B build-agent -G Ninja
cmake --build build-agent
cmake -B build-test-agent -DBUILD_TESTING=ON -G Ninja
cmake --build build-test-agent
QT_QPA_PLATFORM=offscreen ctest --test-dir build-test-agent -V
```

## Risks

1. trying to support multi-instance selection in the first version will complicate action syntax and tests without proving demand
2. unclear logging may make no-op ambiguity cases hard for users to diagnose
3. duplicating clamping rules in the plugin could drift from fooyin provider behaviour
4. adding default bindings too early could create surprising global shortcut conflicts

## Suggested implementation order

1. land the upstream fooyin numeric DSP control API
2. plumb the new service into `VimMotionsPlugin` and `VimHandler`
3. add `adjustDspValue` and `setDspValue` actions
4. implement strict single-target resolution and count semantics
5. add `bindingparsertest.cpp` coverage
6. add `bindingintegrationtest.cpp` coverage
7. add focused runtime handler tests
8. update `README.md` with examples and behaviour notes

## Deliverables

1. plugin consumption of the new public fooyin DSP numeric-control service
2. new vim actions for incremental and absolute numeric DSP updates
3. README documentation for configuration and behaviour
4. parser tests in `tests/bindingparsertest.cpp`
5. integration/config tests in `tests/bindingintegrationtest.cpp`
6. runtime execution tests for target resolution and count handling
