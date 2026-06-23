# Plan: Completely Disable Star-Pointing Flashbang / Sun Glare

## Goal

Provide a robust, user-facing way to completely disable the bright white screen wash, lightshafts, sun glow overlays, and lens flare effects that occur when the player points the camera at a mission sun/star, while preserving ordinary background sun rendering and gameplay lighting unless explicitly changed.

## Problem Summary

The visible "flashbang" effect is composed of several independent rendering paths:

1. **Sun glare intensity accumulation**
   - Implemented in `freespace2/freespace.cpp` in `game_sunspot_process(float frametime)`.
   - The global `Sun_spot` value is driven toward `Sun_spot_goal` when a non-shadowed sun/glare light is near the center of the view.
   - Normal suns compute this with a steep dot-product curve:
     - `Sun_spot_goal += (float)pow(dot, 85.0f);`

2. **Legacy full-screen additive white flash**
   - Implemented in `freespace2/freespace.cpp` in `game_flash_diminish(float frametime)`.
   - If `Sun_spot > 0`, `gr_sunglare_enabled()` is true, and lightshafts are not active, the code adds white to the screen flash color:
     - `r += fl2i(Sun_spot * 128.0f);`
     - `g += fl2i(Sun_spot * 128.0f);`
     - `b += fl2i(Sun_spot * 128.0f);`
   - The final full-screen overlay is rendered by `gr_flash()` in `code/graphics/render.cpp`.

3. **Post-processing lightshafts / sunglare**
   - Implemented in `code/graphics/opengl/gropenglpostprocessing.cpp` in `opengl_post_lightshafts()`.
   - This path is gated by both `gr_sunglare_enabled()` and `gr_lightshafts_enabled()`.
   - It uses `Sun_spot` to scale lightshaft intensity.

4. **Sun glow and lens flare sprites**
   - Implemented in `code/starfield/starfield.cpp`:
     - `stars_draw_sun_glow(int sun_n)` draws the glow bitmap.
     - `stars_draw_lens_flare(vertex* sun_vex, int sun_n)` draws flare sprites when configured.
   - These are called from `game_sunspot_process()` for visible, non-shadowed suns.
   - They are separate from the full-screen white flash and can remain visible even when full-screen glare is disabled.

5. **Table-level glare flag**
   - In `code/starfield/starfield.cpp`, `$NoGlare:` sets `sbm.glare = false` for a sun.
   - This prevents that sun from registering as a glare-capable directional light.
   - It does not, by itself, disable sun glow or lens flare rendering.

6. **Supernova override**
   - `code/graphics/post_processing.cpp` currently forces `gr_sunglare_enabled()` to return true when `supernova_stage() >= SUPERNOVA_STAGE::CLOSE`.
   - This means the existing Sunglare option does not fully suppress supernova glare.
   - If the goal is a strict accessibility setting, this override must be bypassed or made configurable.

## Current Relevant Option

There is already an in-game graphics option:

- Option key: `Graphics.Sunglare`
- Defined in: `code/graphics/post_processing.cpp`
- User-facing label: `Sunglare`
- Description: `Enables or disables glare from suns`

This option already gates some paths, but not every visible component of the star-pointing flashbang effect. The implementation should decide whether to reuse this option with stronger semantics or add a new, more explicit option.

## Recommended Design

Reuse `Graphics.Sunglare` and strengthen its meaning:

> When `Graphics.Sunglare` is off, the engine should render mission suns normally but suppress all camera-facing glare/flash/glow/flare effects caused by looking at those suns.

This avoids adding another confusing graphics setting and matches the existing option description.

Expected behavior with `Sunglare = Off`:

- No full-screen additive white flash from `Sun_spot`.
- No post-processing lightshafts from suns.
- No sun glow overlay drawn by `stars_draw_sun_glow()`.
- No lens flare drawn by `stars_draw_lens_flare()`.
- `Sun_spot` remains clamped/reset to `0.0f`.
- Mission sun bitmap itself remains visible through `stars_draw_sun()`.
- Directional lighting from suns should remain unchanged unless a separate design decision is made to disable it.
- `$NoGlare:` table behavior remains supported and continues to disable glare for individual suns.

Optional stricter behavior:

- Also suppress supernova glare/whiteout when `Sunglare = Off`.
- This is recommended if the option is treated as an accessibility setting.

## Implementation Plan

### 1. Confirm current behavior manually before edits

Run or launch a mission with a visible sun and test these settings:

1. `Sunglare = On`, `Lightshafts = On`
   - Expected current behavior: post-processing lightshaft glare appears when looking near the sun.

2. `Sunglare = On`, `Lightshafts = Off`
   - Expected current behavior: legacy full-screen white additive flash appears when looking near the sun.

3. `Sunglare = Off`
   - Expected current behavior to verify: full-screen flash and lightshafts should stop, but sun glow/lens flare may still be visible.

This establishes baseline behavior and confirms exactly which visual components remain.

### 2. Strengthen the early exit in `game_sunspot_process()`

File:

- `freespace2/freespace.cpp`

Function:

- `game_sunspot_process(float frametime)`

Add an early guard at the start of the function:

```cpp
if (!gr_sunglare_enabled()) {
    Sun_spot = 0.0f;
    Sun_drew = 0;
    Supernova_last_glare = 0.0f; // optional, recommended if suppressing supernova glare too
    return;
}
```

Rationale:

- Prevents `Sun_spot_goal` from being calculated.
- Prevents `Sun_spot` from ramping up.
- Prevents calls to `stars_draw_sun_glow()` in the normal sun path.
- Prevents calls to `stars_draw_sun_glow()` in the supernova path if supernova glare should also obey the option.
- Clears `Sun_drew` so stale sun draw state does not affect the next frame.

Caveat:

- If `gr_sunglare_enabled()` continues to force true during supernova, this guard will not suppress supernova glare. See step 3.

### 3. Remove or refine the supernova override in `gr_sunglare_enabled()`

File:

- `code/graphics/post_processing.cpp`

Function:

- `gr_sunglare_enabled()`

Current behavior:

```cpp
if (supernova_stage() >= SUPERNOVA_STAGE::CLOSE) {
    return true;
}

return graphics::SunglareOption->getValue();
```

Recommended replacement:

```cpp
return graphics::SunglareOption->getValue();
```

Rationale:

- If the user disables sunglare, the setting should be respected consistently.
- This makes the option usable for photosensitivity/accessibility.
- Supernova should not be able to re-enable an effect that the player explicitly disabled.

Alternative if preserving old cinematic behavior is desired:

- Add a separate option, e.g. `Graphics.SupernovaGlare`, defaulting to true.
- Then make supernova glare require both options:

```cpp
if (supernova_stage() >= SUPERNOVA_STAGE::CLOSE) {
    return graphics::SunglareOption->getValue() && graphics::SupernovaGlareOption->getValue();
}
```

This alternative is more work and probably unnecessary unless maintainers want supernova behavior to remain independently configurable.

### 4. Keep post-processing lightshaft gating as-is

File:

- `code/graphics/opengl/gropenglpostprocessing.cpp`

Function:

- `opengl_post_lightshafts()`

Current gate:

```cpp
if (!Game_subspace_effect && gr_sunglare_enabled() && gr_lightshafts_enabled()) {
    ...
}
```

No direct change should be necessary once `gr_sunglare_enabled()` consistently respects the option.

Rationale:

- The existing condition is correct.
- The issue is that `gr_sunglare_enabled()` can be overridden during supernova and that sun glow/flare sprites are drawn elsewhere.

### 5. Ensure legacy full-screen flash remains suppressed

File:

- `freespace2/freespace.cpp`

Function:

- `game_flash_diminish(float frametime)`

Current gate:

```cpp
if (Sun_spot > 0.0f && gr_sunglare_enabled() && !gr_lightshafts_enabled()) {
    r += fl2i(Sun_spot*128.0f);
    g += fl2i(Sun_spot*128.0f);
    b += fl2i(Sun_spot*128.0f);
}
```

This can remain unchanged if step 2 is implemented.

Optional defensive improvement:

- Explicitly clear `Sun_spot` when `gr_sunglare_enabled()` is false before the flash calculation.
- This is redundant with the early return in `game_sunspot_process()` but makes the flash path safer against future call-order changes.

Example defensive change:

```cpp
if (!gr_sunglare_enabled()) {
    Sun_spot = 0.0f;
}
```

Only add this if maintainers prefer belt-and-suspenders protection.

### 6. Decide whether lens flare should also be gated locally

File:

- `code/starfield/starfield.cpp`

Functions:

- `stars_draw_sun_glow(int sun_n)`
- `stars_draw_lens_flare(vertex* sun_vex, int sun_n)`

If the early return in `game_sunspot_process()` is implemented, these functions should no longer be called from the sunspot path while `Sunglare = Off`.

However, local guards can make the behavior more robust if future code calls these functions from another path:

```cpp
if (!gr_sunglare_enabled()) {
    return;
}
```

Considerations:

- Adding this requires including or already having access to `graphics/post_processing.h` in `code/starfield/starfield.cpp`.
- This introduces a dependency from starfield rendering to the graphics post-processing option.
- The early return in `game_sunspot_process()` may be cleaner because the glow/flare behavior is currently driven from that function.

Recommendation:

- Start with the early return in `game_sunspot_process()`.
- Add local guards only if testing reveals another path still draws glow/flare with `Sunglare = Off`.

### 7. Preserve normal sun rendering and lighting

Do not disable the following by default:

- `stars_draw_sun(show_suns)` in `code/starfield/starfield.cpp`
- Directional light creation via `light_add_directional()` in `stars_draw_sun()`

Rationale:

- The requested problem is the flashbang/glare when pointing at a star, not the existence of suns or their lighting.
- Removing directional sunlight could visually change missions and ships much more broadly.
- `$NoGlare:` already allows per-sun glare disabling while keeping light.

### 8. Update comments / documentation near the option

File:

- `code/graphics/post_processing.cpp`

Update or add a short comment near `SunglareOption` explaining the stronger semantics:

```cpp
// Controls camera-facing sun glare effects, including legacy fullscreen glare,
// post-processing lightshafts, sun glow overlays, and lens flares.
```

If user-facing option text is expected to be precise, consider changing:

- Current description: `Enables or disables glare from suns`
- Proposed description: `Enables or disables sun glare, glow, and lens flare effects`

Potential issue:

- The description uses localization IDs. Changing text may require updating localization resources depending on project conventions.

### 9. Add focused regression tests where feasible

Automated graphical verification may not be practical, but unit-level or integration-level assertions may be possible.

Potential test targets:

1. `gr_sunglare_enabled()` behavior
   - When `Graphics.Sunglare` is false, it should return false even during supernova stages if the supernova override is removed.

2. Sunspot state behavior
   - With `Sunglare = Off`, calling `game_sunspot_process()` should leave `Sun_spot == 0.0f`.
   - `Sun_drew` should be cleared.

Challenges:

- `game_sunspot_process()` depends on global rendering, lighting, viewer, and mission state.
- Existing test stubs may need additional setup.
- If unit testing is too invasive, rely on manual QA plus a small code-level assertion where appropriate.

### 10. Manual validation checklist

Use a mission with a visible sun/star and test all combinations below.

#### Normal mission, `Sunglare = On`, `Lightshafts = On`

Expected:

- Sun appears normally.
- Lightshaft/post-processing glare appears when looking near the sun.
- This preserves existing behavior.

#### Normal mission, `Sunglare = On`, `Lightshafts = Off`

Expected:

- Sun appears normally.
- Legacy full-screen brightening may appear when looking near the sun.
- This preserves existing behavior.

#### Normal mission, `Sunglare = Off`, `Lightshafts = On`

Expected:

- Sun bitmap still appears.
- No full-screen white additive flash.
- No post-processing lightshafts.
- No sun glow overlay.
- No lens flare sprites.
- Ship lighting from the sun remains intact.

#### Normal mission, `Sunglare = Off`, `Lightshafts = Off`

Expected:

- Same as above.
- Specifically verify the legacy flash path does not activate.

#### Mission sun with `$NoGlare:`

Expected:

- With `Sunglare = On`, this individual sun should not produce glare.
- With `Sunglare = Off`, behavior should remain no-glare globally.
- No regression in table parsing.

#### Supernova mission, `Sunglare = Off`

Expected if strict accessibility behavior is implemented:

- No supernova glare override.
- No full-screen supernova glare/whiteout from the sunspot path.
- Mission/death flow still proceeds correctly.

Expected if supernova override is intentionally preserved:

- Document that `Sunglare = Off` does not suppress supernova cinematic glare.
- This is not recommended for the stated goal of completely disabling flashbang effects.

## Files Likely Touched

Primary:

- `freespace2/freespace.cpp`
  - Add early return/reset in `game_sunspot_process()` when `gr_sunglare_enabled()` is false.
  - Optionally add defensive `Sun_spot` clearing in `game_flash_diminish()`.

- `code/graphics/post_processing.cpp`
  - Remove or refine the supernova force-enable behavior in `gr_sunglare_enabled()`.
  - Optionally update option comments or description.

Possible secondary files:

- `code/starfield/starfield.cpp`
  - Only if local guards are needed in `stars_draw_sun_glow()` / `stars_draw_lens_flare()` after testing.

- Test files under `test/`
  - Only if adding automated coverage is practical.

- Documentation/changelog files
  - Add a changelog note if required by project conventions.

## Acceptance Criteria

The change is complete when all of the following are true:

1. With `Graphics.Sunglare = Off`, pointing directly at a visible mission sun does not cause the screen to turn white or brighten globally.
2. With `Graphics.Sunglare = Off`, OpenGL lightshafts from suns are not rendered.
3. With `Graphics.Sunglare = Off`, sun glow overlays and lens flare sprites are not rendered.
4. With `Graphics.Sunglare = Off`, `Sun_spot` does not ramp up and remains/reset to `0.0f`.
5. The actual sun bitmap remains visible in the background.
6. Directional lighting from suns remains functional unless explicitly changed by a separate design decision.
7. Existing behavior with `Graphics.Sunglare = On` remains unchanged for normal missions.
8. Supernova behavior is either:
   - fully suppressed by `Graphics.Sunglare = Off`, recommended; or
   - explicitly documented as an exception, not recommended for this goal.
9. No crashes or rendering errors occur when toggling the option between missions.
10. Existing `$NoGlare:` table behavior remains compatible.

## Risks and Mitigations

### Risk: Supernova cinematic visibility changes

Removing the supernova override may reduce or eliminate intended supernova cinematic glare.

Mitigation:

- Treat this as intended when `Sunglare = Off`.
- Preserve default `Sunglare = On`, so default cinematic behavior remains unchanged.
- Optionally add a separate supernova-specific option only if maintainers object.

### Risk: Sun glow may be considered part of the visible sun art, not glare

Some missions may rely aesthetically on sun glow bitmaps.

Mitigation:

- Only suppress glow when the user explicitly disables `Sunglare`.
- Keep the core sun disk rendering intact.

### Risk: Hidden call paths still render glow/flare

If `stars_draw_sun_glow()` is called from a path other than `game_sunspot_process()`, the early return may not catch it.

Mitigation:

- Search for all call sites of `stars_draw_sun_glow()` and `stars_draw_lens_flare()` during implementation.
- Add local guards in `code/starfield/starfield.cpp` if needed.

### Risk: Option semantics become broader than the current label

`Sunglare` would also control sun glow and lens flare.

Mitigation:

- Update comments and user-facing description if localization practices allow.
- The broader behavior still matches user expectations for disabling glare from suns.

## Suggested Minimal Patch Shape

The smallest likely effective patch is:

1. In `game_sunspot_process()`:

```cpp
if (!gr_sunglare_enabled()) {
    Sun_spot = 0.0f;
    Sun_drew = 0;
    Supernova_last_glare = 0.0f;
    return;
}
```

2. In `gr_sunglare_enabled()`:

```cpp
return graphics::SunglareOption->getValue();
```

3. Test whether `stars_draw_sun_glow()` / lens flare are fully suppressed through the early return. If not, add local guards.

## Recommended Implementation Order

1. Apply the minimal patch.
2. Build the project.
3. Manually test normal sun glare with `Sunglare` on/off and `Lightshafts` on/off.
4. Test a supernova scenario if available.
5. Search call sites again for glow/flare functions.
6. Add local guards only if testing shows remaining unwanted glow/flare.
7. Update option comments/text and changelog if appropriate.
