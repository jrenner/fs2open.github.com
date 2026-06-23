# In-Mission Tactical Map Implementation Plan

## Goal

Add a separate, bindable in-mission tactical map screen for FreeSpace Open that pauses gameplay while open, displays all active ships on a grid as icons with vertical drop lines, and lets the player click ships to view information and optionally target/select them.

## Desired User Experience

- Player presses a configurable hotkey during gameplay.
- The game enters a separate tactical map screen/state.
- Mission simulation is paused while the map is open:
  - mission time does not advance
  - ships/weapons do not move
  - AI does not update
  - normal gameplay input is suspended
- A mouse cursor is shown.
- The screen displays a tactical grid with all active ships.
- Each ship is represented by an IFF/team-colored icon.
- Each ship has a drop line to the grid plane, showing its height/altitude relative to the tactical plane.
- The player can hover/click ship icons.
- Clicking a ship shows detailed information in a side panel.
- Optionally, clicking a ship sets it as the player's current target.
- Pressing the same hotkey again, or pressing Esc, closes the tactical map and resumes gameplay.

## Assumptions

1. Initial implementation is for single-player/local gameplay only.
2. Multiplayer support should be disabled or deferred because pausing a networked mission has host/client authority implications.
3. “Show all ships” means all currently active ship objects in the mission, not merely radar-visible ships.
4. Sensor-respecting visibility can be added later as an option.
5. The first implementation should not require new art assets; it should use existing radar icons when available and simple fallback shapes otherwise.
6. The tactical map should be implemented as a new game state, not as a HUD gauge and not as an extension of the existing radar.

## Existing Systems to Reuse or Reference

### Pause screen

Relevant files:

- `code/missionui/missionpause.h`
- `code/missionui/missionpause.cpp`
- `freespace2/freespace.cpp`
- `code/gamesequence/gamesequence.h`
- `code/gamesequence/gamesequence.cpp`

The existing pause screen demonstrates:

- how to push a paused gameplay state
- how to call `game_stop_time()` and `game_start_time()`
- how to pause/unpause sounds
- how to show the mouse cursor
- how to draw a full-screen UI while gameplay is paused
- how to return to the previous state with `GS_EVENT_PREVIOUS_STATE`

Useful functions/patterns:

- `pause_init()`
- `pause_do()`
- `pause_close()`
- `pause_set_type()` / `pause_get_type()`
- `gameseq_push_state(GS_STATE_GAME_PAUSED)`
- `gameseq_pop_state()` via `GS_EVENT_PREVIOUS_STATE`

### Control bindings

Relevant files:

- `code/controlconfig/controlsconfig.h`
- `code/controlconfig/controlsconfigcommon.cpp`
- `code/io/keycontrol.cpp`

The control system already supports bindable gameplay actions through `IoActionId` and the control-config builder.

The tactical map should be added as a normal configurable action, similar to:

- `TOGGLE_HUD`
- `TOGGLE_PHOTO_MODE`
- `TIME_SPEED_UP`
- `TIME_SLOW_DOWN`

### Radar ship icons

Relevant files:

- `code/radar/radarsetup.h`
- `code/radar/radarsetup.cpp`
- `code/radar/radar.cpp`
- `code/ship/ship.cpp`
- `code/ship/ship.h`

Existing ship class table fields:

- `$Radar Image 2D:`
- `$Radar Color Image 2D:`
- `$Radar Image Size:`
- `$3D Radar Blip Size Multiplier:`

Relevant ship info fields:

- `ship_info::radar_image_2d_idx`
- `ship_info::radar_color_image_2d_idx`
- `ship_info::radar_image_size`
- `ship_info::radar_projection_size_mult`

The tactical map should reuse these where possible.

### Briefing map

Relevant files:

- `code/mission/missionbriefcommon.h`
- `code/mission/missionbriefcommon.cpp`
- `code/missionui/missionbrief.cpp`
- `code/mission/missionparse.cpp`

The briefing map already supports:

- grid rendering
- map icons
- icon lines
- clicking icons
- closeup/info popups

However, it is stage-authored and pre-mission, not a live in-mission tactical map. It should be used as a reference, not as the primary implementation.

### FRED drop-line rendering

Relevant files:

- `code/mission/missiongrid.h`
- `code/mission/missiongrid.cpp`
- `fred2/fredrender.cpp`

FRED has useful reference logic for drawing object elevation/drop lines to a grid plane:

- `render_model_x()`
- `render_model_x_htl()`

These functions project an object onto the active grid plane, draw a line from the grid point to the object, and draw an X marker at the grid projection. The tactical map should implement a 2D equivalent of this behavior.

## Architecture Overview

The implementation should introduce a new mission UI module and a new game state:

- New event: `GS_EVENT_TACTICAL_MAP`
- New state: `GS_STATE_TACTICAL_MAP`
- New module:
  - `code/missionui/missiontacticalmap.h`
  - `code/missionui/missiontacticalmap.cpp`

High-level flow:

1. Player presses the tactical map control binding.
2. Input handling posts `GS_EVENT_TACTICAL_MAP`.
3. Game sequence pushes `GS_STATE_TACTICAL_MAP`.
4. Entering the state stops game time and initializes tactical map UI.
5. Each frame, tactical map UI collects active ships, renders the map, and handles mouse/keyboard input.
6. Player presses Esc or the same tactical map binding.
7. Tactical map posts `GS_EVENT_PREVIOUS_STATE`.
8. Leaving the state resumes time, destroys UI state, restores cursor, and unpauses audio.

## Detailed Implementation Steps

## 1. Add New Game Event and State

### Files

- `code/gamesequence/gamesequence.h`
- `code/gamesequence/gamesequence.cpp`
- `freespace2/freespace.cpp`

### Work

Add a new event enum value:

```cpp
GS_EVENT_TACTICAL_MAP
```

Add a new state enum value:

```cpp
GS_STATE_TACTICAL_MAP
```

Update the event/state name arrays in `code/gamesequence/gamesequence.cpp` so debugging/log output has readable names:

```cpp
"GS_EVENT_TACTICAL_MAP"
"GS_STATE_TACTICAL_MAP"
```

In the main event handler in `freespace2/freespace.cpp`, add handling for the event:

```cpp
case GS_EVENT_TACTICAL_MAP:
    gameseq_push_state(GS_STATE_TACTICAL_MAP);
    break;
```

### State lifecycle integration

In `game_leave_state()`:

- Treat `GS_STATE_TACTICAL_MAP` as an in-mission state that should not end the mission.
- Add it to the list with states like:
  - `GS_STATE_GAME_PAUSED`
  - `GS_STATE_OPTIONS_MENU`
  - `GS_STATE_MISSION_LOG_SCROLLBACK`
  - `GS_STATE_GAMEPLAY_HELP`

Expected logic:

```cpp
case GS_STATE_TACTICAL_MAP:
    end_mission = 0;
    break;
```

When entering `GS_STATE_TACTICAL_MAP`:

```cpp
case GS_STATE_TACTICAL_MAP:
    game_stop_time();
    tactical_map_init();
    break;
```

When leaving `GS_STATE_TACTICAL_MAP`:

```cpp
case GS_STATE_TACTICAL_MAP:
    game_start_time();
    tactical_map_close();
    break;
```

During frame processing:

```cpp
case GS_STATE_TACTICAL_MAP:
    tactical_map_do(flFrametime);
    break;
```

Do not call `game_frame()` in this state. The map is intended to fully pause simulation.

## 2. Add Bindable Control Action

### Files

- `code/controlconfig/controlsconfig.h`
- `code/controlconfig/controlsconfigcommon.cpp`
- `code/io/keycontrol.cpp`

### Work

Add a new `IoActionId` near HUD/computer controls:

```cpp
TACTICAL_MAP_TOGGLE
```

The exact placement matters because control IDs are serialized and mapped. It should be added carefully near other modern appended controls, likely after photo mode controls or near HUD controls, avoiding renumbering assumptions where possible.

Add a builder entry in `control_config_common_init_bindings()`:

```cpp
(TACTICAL_MAP_TOGGLE, KEY_ALTED | KEY_M, -1, COMPUTER_TAB, <xstr-id>, "Toggle Tactical Map", CC_TYPE_TRIGGER)
```

Default key recommendation:

- `Alt+M`, if it is not already in conflict.
- If conflict risk is high, make it unbound by default:

```cpp
(TACTICAL_MAP_TOGGLE, -1, -1, COMPUTER_TAB, <xstr-id>, "Toggle Tactical Map", CC_TYPE_TRIGGER)
```

Update all action maps in `controlsconfigcommon.cpp`:

- display/action string map
- `ADD_ENUM_TO_ACTION_MAP(...)` section
- any compatibility action-name aliases if needed

### Input handling

In `code/io/keycontrol.cpp`, add handling in the control action switch where `TOGGLE_HUD` and `TOGGLE_PHOTO_MODE` are handled:

```cpp
case TACTICAL_MAP_TOGGLE:
    if (!(Game_mode & GM_MULTIPLAYER)) {
        gameseq_post_event(GS_EVENT_TACTICAL_MAP);
    } else {
        gamesnd_play_error_beep();
        HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Tactical map is unavailable in multiplayer", -1));
    }
    break;
```

If the control system does not dispatch gameplay actions while in the tactical map state, the tactical map state itself should directly check for Esc and the bound control during `tactical_map_do()`.

## 3. Create Tactical Map Module

### Files

Add:

- `code/missionui/missiontacticalmap.h`
- `code/missionui/missiontacticalmap.cpp`

Register the new `.cpp` in the build system, likely in the source list/CMake configuration used for mission UI files.

### Header API

```cpp
#pragma once

void tactical_map_init();
void tactical_map_do(float frametime);
void tactical_map_close();
bool tactical_map_is_active();
```

### Module responsibilities

The module should own:

- active/inactive state flag
- `UI_WINDOW`
- cursor state
- saved screen/background state if used
- map pan and zoom
- selected contact
- hovered contact
- current frame contact list
- rendering colors/layout constants

## 4. Tactical Map Internal Data Structures

### Contact identity

Use object number plus object signature to avoid stale references.

```cpp
struct tactical_map_contact_id {
    int objnum = -1;
    int signature = -1;
};
```

### Contact record

```cpp
struct tactical_map_contact {
    tactical_map_contact_id id;

    int shipnum = -1;
    int ship_class = -1;
    int team = -1;

    vec3d world_pos = vmd_zero_vector;

    SCP_string display_name;
    SCP_string ship_name;
    SCP_string class_name;
    SCP_string team_name;

    float hull_pct = 0.0f;
    float shield_pct = -1.0f;
    float distance_from_player = 0.0f;

    int radar_image_2d = -1;
    int radar_color_image_2d = -1;
    int icon_size = -1;

    color iff_color;

    int icon_x = 0;
    int icon_y = 0;
    int ground_x = 0;
    int ground_y = 0;
    int hit_radius = 8;

    bool is_player = false;
    bool is_current_target = false;
    bool is_dying = false;
};
```

### Map view state

```cpp
struct tactical_map_view {
    float zoom = 1.0f;
    float vertical_scale = 0.05f;
    vec3d origin = vmd_zero_vector;
    int pan_x = 0;
    int pan_y = 0;
    bool auto_fit_pending = true;
};
```

### Layout state

Define a main map rect and info panel rect:

```cpp
struct tactical_map_rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
```

Suggested layout:

- top bar: 32 px
- bottom help bar: 28 px
- right info panel: 260-340 px depending on resolution
- map area: remaining space

## 5. Initialization and Shutdown

### `tactical_map_init()`

Do:

1. Guard against double init.
2. Disable in unsupported states/modes if necessary.
3. Pause sounds:

```cpp
weapon_pause_sounds();
audiostream_pause_all();
message_pause_all();
```

4. Create UI window:

```cpp
Tactical_map_window.create(0, 0, gr_screen.max_w_unscaled, gr_screen.max_h_unscaled, 0);
```

5. Push/show cursor:

```cpp
io::mouse::CursorManager::get()->pushStatus();
io::mouse::CursorManager::get()->showCursor(true);
```

6. Initialize selection/hover:

```cpp
Selected_contact = {};
Hovered_contact = {};
```

7. Set `auto_fit_pending = true` so the first frame fits all ships.
8. Set active flag.

### `tactical_map_close()`

Do:

1. Guard if not active.
2. Destroy UI window.
3. Release any saved screen/background resources.
4. Pop cursor state:

```cpp
io::mouse::CursorManager::get()->popStatus();
```

5. Unpause sounds:

```cpp
weapon_unpause_sounds();
audiostream_unpause_all();
message_resume_all();
```

6. Clear contact list and selected/hovered state.
7. Set active flag false.

### Audio caveat

If tactical map can be entered while another system already paused audio, avoid double-unpause. The initial implementation can mirror `missionpause.cpp`, but future hardening should track whether this module performed each pause.

## 6. Frame Loop

### `tactical_map_do(float frametime)`

Frame order:

1. Process UI window/key input.
2. Collect current contacts.
3. Validate selected contact.
4. Auto-fit map if needed.
5. Update mouse hover.
6. Handle clicks/keyboard.
7. Render background.
8. Render grid.
9. Render contacts/drop lines.
10. Render info panel.
11. Draw UI window/cursor.
12. Flip frame.

Pseudo-code:

```cpp
void tactical_map_do(float frametime)
{
    int key = Tactical_map_window.process() & ~KEY_DEBUGGED;

    collect_contacts();
    validate_selection();

    if (View.auto_fit_pending) {
        auto_fit_contacts();
        View.auto_fit_pending = false;
    }

    handle_input(key);

    gr_reset_clip();
    render_background();
    render_title_bar();
    render_map_area();
    render_info_panel();
    render_help_bar();

    Tactical_map_window.draw();
    gr_flip();
}
```

## 7. Contact Collection

### Source of ships

Iterate `obj_used_list`.

Include objects where:

- `objp->type == OBJ_SHIP`
- instance is valid
- not `Object::Object_Flags::Should_be_dead`
- ship/object has not been fully deleted

Potentially include `OBJ_START` if useful, though during live mission these are usually not relevant.

### Data extraction

For each contact:

- `object* objp`
- `ship* shipp = &Ships[objp->instance]`
- `ship_info* sip = &Ship_info[shipp->ship_info_index]`

Fields:

- object number: `OBJ_INDEX(objp)`
- signature: `objp->signature`
- world position: `objp->pos`
- team: `shipp->team`
- class: `shipp->ship_info_index`
- ship name: `shipp->ship_name`
- display name: use existing display-name helper if available; otherwise ship name
- class display: `sip->get_display_name()` if suitable
- distance from player: `vm_vec_dist(&objp->pos, &Player_obj->pos)`
- IFF color: `iff_get_color_by_team_and_object(shipp->team, Player_ship->team, 1, objp)`
- radar icon fields from `sip`

### Hull percentage

Compute roughly:

```cpp
float max_hull = Ship_info[shipp->ship_info_index].initial_hull_strength;
float hull_pct = max_hull > 0.0f ? objp->hull_strength / max_hull : 0.0f;
```

Clamp to `[0, 1]`.

If a more canonical hull max helper exists, prefer that.

### Shield percentage

If object shields exist:

- Use object shield helpers if available.
- Otherwise sum shield quadrant values and divide by max shield strength.

If unavailable/no shields:

```cpp
shield_pct = -1.0f;
```

Display “N/A”.

## 8. Map Projection

### Coordinate system

Use world X/Z as the flat tactical plane:

- world X -> map horizontal axis
- world Z -> map vertical axis on screen
- world Y -> altitude/elevation offset

Projection formula:

```cpp
float rel_x = contact.world_pos.xyz.x - View.origin.xyz.x;
float rel_z = contact.world_pos.xyz.z - View.origin.xyz.z;
float rel_y = contact.world_pos.xyz.y - View.origin.xyz.y;

contact.ground_x = map_rect.x + map_rect.w / 2 + View.pan_x + fl2i(rel_x * View.zoom);
contact.ground_y = map_rect.y + map_rect.h / 2 + View.pan_y - fl2i(rel_z * View.zoom);

contact.icon_x = contact.ground_x;
contact.icon_y = contact.ground_y - fl2i(rel_y * View.zoom * View.vertical_scale);
```

### Auto-fit

On open/reset:

1. Find min/max X and Z of all contacts.
2. Compute center:

```cpp
origin.x = (min_x + max_x) / 2
origin.z = (min_z + max_z) / 2
origin.y = 0 or average_y
```

3. Compute required zoom:

```cpp
zoom_x = usable_map_width / (max_x - min_x + padding)
zoom_z = usable_map_height / (max_z - min_z + padding)
zoom = min(zoom_x, zoom_z)
```

4. Clamp zoom to min/max.
5. Reset pan to zero.

### Manual controls

Recommended MVP controls:

- Mouse wheel or `+/-`: zoom in/out
- Arrow keys: pan
- `R`: reset view/autofit
- Optional: right mouse drag to pan

## 9. Grid Rendering

Render the grid inside the map area with clipping enabled.

### Determine grid spacing

Use dynamic spacing based on zoom so grid lines remain readable.

Example world spacings:

- 100
- 250
- 500
- 1000
- 2500
- 5000
- 10000

Choose the spacing whose projected pixel distance is around 40-100 pixels.

```cpp
float desired_px = 64.0f;
float world_spacing = choose_grid_spacing(desired_px / View.zoom);
float pixel_spacing = world_spacing * View.zoom;
```

### Minor/major lines

- Draw minor lines dim.
- Draw every 5th line brighter.
- Draw X=0/Z=0 axes with distinct color if visible.

### Labels

Optional but useful:

- Show scale label in top-left of map area: `Grid: 1 km`
- Show coordinate labels on major grid lines if not too cluttered.

## 10. Drop Line Rendering

For each contact:

1. Draw line from elevated icon point to ground point:

```cpp
gr_line(contact.icon_x, contact.icon_y, contact.ground_x, contact.ground_y, GR_RESIZE_NONE);
```

2. Draw ground marker:

```cpp
gr_line(ground_x - 4, ground_y - 4, ground_x + 4, ground_y + 4);
gr_line(ground_x - 4, ground_y + 4, ground_x + 4, ground_y - 4);
```

3. Use dimmer color for drop line, brighter color for selected/hovered.

Altitude sign:

- If `rel_y > 0`, icon appears above its ground point.
- If `rel_y < 0`, icon appears below its ground point.
- This gives a readable 2.5D view while still using a 2D map.

## 11. Icon Rendering

### Preferred icon source

Use radar 2D icon fields from ship class:

```cpp
sip->radar_image_2d_idx
sip->radar_color_image_2d_idx
sip->radar_image_size
```

If a ship has a radar icon:

- Draw it centered at `icon_x/icon_y`.
- Use `radar_image_size`, or a tactical-map-specific default.
- If only monochrome/color image is available, tint with IFF color where possible.

### Fallback icons

If no bitmap exists, draw simple shapes:

- small ship/fighter: triangle
- bomber: diamond
- cargo/support: square
- cruiser/capital: rectangle
- unknown: circle/question mark

Fallback classification can use `ship_info` helpers:

- `sip->is_small_ship()`
- `sip->is_big_ship()`
- `sip->is_huge_ship()`
- cargo/navbuoy flags

### Highlight states

Draw in this order:

1. Ground drop lines and markers.
2. Non-selected ship icons.
3. Current target ring.
4. Hover ring.
5. Selected ring.
6. Labels.

Highlight ideas:

- current player target: crosshair or pulsing ring
- hovered contact: thin white outline
- selected contact: thicker bright outline
- player ship: distinct marker

## 12. Mouse Interaction

### Hover detection

After projecting contacts to screen:

```cpp
int dx = mx - contact.icon_x;
int dy = my - contact.icon_y;
if (dx * dx + dy * dy <= contact.hit_radius * contact.hit_radius) {
    hovered = contact.id;
}
```

If multiple contacts overlap, choose the nearest by screen distance. If tied, choose the one with smaller world distance to player or the one drawn last.

### Click handling

Use mouse button press count or release events to avoid repeated selection every frame.

On left click inside map area:

- If hovering a contact:
  - store selected contact ID
  - play selection sound
  - optionally call target selection logic
- If not hovering:
  - clear selected contact or keep previous selection, depending on desired UX

Recommended MVP:

- Single click selects and displays info.
- Double click or Enter targets and closes later if desired.
- Or single click both selects and targets immediately if that is preferred.

### Set player target

When clicking a valid ship, optionally set it as player target:

```cpp
set_target_objnum(Player_ai, objnum);
```

Include:

```cpp
#include "ai/ai.h"
```

Before calling:

- verify `Player_ai != nullptr`
- verify object exists and signature matches
- verify object is targetable if targetability should be respected

If “all ships” includes normally untargetable/stealth ships, decide whether clicking them should only show info or also target them. Recommended MVP: show info for all, but only call `set_target_objnum()` if normal targeting rules allow it, unless a debug/omniscient option is enabled.

## 13. Keyboard Interaction Inside Map

Handle directly in `tactical_map_do()`:

- Esc: close map
- Tactical map toggle binding: close map
- `R`: reset/autofit
- `+`/`-`: zoom
- arrows/WASD: pan
- Enter: target selected contact
- Tab/Shift+Tab: cycle contacts

Closing:

```cpp
gameseq_post_event(GS_EVENT_PREVIOUS_STATE);
```

## 14. Info Panel Rendering

### Panel contents

For selected contact, display:

- ship display name
- ship class display name
- team/IFF name
- hull percentage
- shield percentage or N/A
- distance from player
- coordinates X/Y/Z
- altitude relative to map plane
- current target marker if it is the player target
- status:
  - Player
  - Dying
  - Departing/warping if easy to detect

Example panel:

```text
GTD Aquitaine
Hecate-class destroyer
IFF: Friendly
Hull: 87%
Shields: N/A
Distance: 4280 m
Position:
  X: 1200
  Y: -340
  Z: 9800
Status: Current target
```

For hovered contact but no selected contact, either:

- show hover details, or
- keep selected details and show a compact tooltip near cursor.

Recommended MVP:

- Info panel shows selected contact.
- Hover shows compact name tooltip.

### Text rendering

Use existing `gr_string`, `gr_printf`, `gr_get_string_size`, and clipping. Use `font::set_font(font::FONT1)` unless a UI font convention indicates otherwise.

## 15. Multiplayer Policy

### MVP behavior

Disable in multiplayer:

- If player presses tactical map key in multiplayer, play error beep and show a HUD message.
- Do not open local pause-only tactical map.

Reason:

- `GS_STATE_GAME_PAUSED` in multiplayer has dedicated network handling.
- Local-only pause would desync the player from the mission.
- Host-authoritative pause needs `multi_pause_request()` integration and UI coordination.

### Future multiplayer support

Potential later behavior:

- Host or team captain can request tactical pause.
- All clients enter tactical map or pause overlay.
- Non-host clients can inspect but maybe not issue target changes/orders.
- Use existing `multi_pause` infrastructure.

This should be a separate feature after single-player is stable.

## 16. Sensor Visibility Policy

### MVP behavior

Show all active ships.

### Later options

Add configuration:

- Show all ships
- Show radar-visible ships only
- Show friendly ships + radar-visible hostile ships
- Show only ships visible to player team/AWACS

Potential implementation uses existing radar visibility logic:

- `radar_is_visible(object*)`
- AWACS helpers
- stealth/hidden sensor flags

Caveat:

- `radar_is_visible()` is radar/range oriented and may filter by current radar range, which may not be desirable for a strategic tactical map.

## 17. Mission/Mod Configuration Options

Optional post-MVP additions:

### Mission custom data

Use mission custom data keys such as:

```text
$tactical-map-enabled: true
$tactical-map-visibility: all | sensors | friendly-plus-sensors
$tactical-map-can-target: true
$tactical-map-show-labels: true
```

### Table options

Possible mod/table settings:

- default tactical map icon size
- map colors
- grid colors
- vertical scale
- whether labels are always shown
- whether selection sets player target

### Scripting hooks

Potential hooks:

- `On Tactical Map Open`
- `On Tactical Map Close`
- `On Tactical Map Contact Selected`
- contact visibility override hook
- contact info extension hook

These are not required for MVP.

## 18. Build Integration

After adding `missiontacticalmap.cpp`, update whichever build list includes mission UI sources. Likely places:

- root or code CMake files
- source group lists

Search for existing entries like:

- `missionpause.cpp`
- `missionbrief.cpp`
- `missiondebrief.cpp`

Add the new source adjacent to `missionpause.cpp`.

## 19. Localization/XSTR

For quick MVP, strings can initially use `XSTR(..., -1)` or plain strings if acceptable for development.

For production-quality implementation, assign string IDs for:

- “Toggle Tactical Map”
- “Tactical Map”
- “Tactical map is unavailable in multiplayer”
- “No ships”
- “Hull”
- “Shields”
- “Distance”
- “Position”
- “Close”
- help bar strings

Update localization resources according to existing project conventions.

## 20. Edge Cases

Handle these safely:

### No ships

- Show grid centered on origin or player position.
- Info panel says “No contacts”.

### Player ship missing/dead

- Avoid using `Player_obj` or `Player_ship` without null checks.
- Use origin fallback if player object is invalid.

### Selected ship destroyed while map is open

- Check object signature every frame.
- Clear selection if invalid.

### Very large coordinate spread

- Clamp zoom minimum.
- Show warning/scale label if contacts are extremely spread out.

### Overlapping icons

- Hover nearest icon.
- Later: add decluttering/list selection.

### Fullscreen/resolution scaling

- Use unscaled screen coordinates consistently.
- Use clipping for map area.

### Paused while already paused

- Tactical map should only open from gameplay initially.
- Avoid opening from normal pause unless explicitly designed.

## 21. Acceptance Criteria

### Build and startup

- Project builds successfully with the new files.
- No missing enum/action-map assertions occur.
- Control config opens without errors.

### Binding

- “Toggle Tactical Map” appears in control configuration.
- The binding can be assigned.
- The binding persists across pilot/config reloads.

### Opening and closing

- Pressing the bound key during gameplay opens the tactical map.
- Pressing Esc closes it.
- Pressing the bound key again closes it.
- Returning to gameplay resumes normal controls.

### Pause behavior

While the map is open:

- mission time does not advance
- ship positions do not change
- weapons do not move
- AI does not act
- gameplay sounds/music are paused

After closing:

- time resumes
- sounds/music resume
- gameplay controls work normally

### Map rendering

- All active ships are shown.
- Each ship has an icon.
- Icons are IFF/team-colored.
- Each ship has a drop line to the grid plane.
- Grid is visible and scales with zoom.
- Current player target is highlighted.

### Interaction

- Moving mouse over an icon highlights it.
- Clicking an icon selects it.
- The side panel shows that ship’s info.
- If target selection is enabled, clicked ship becomes the player target.
- Selection remains safe if the ship disappears.

### Unsupported multiplayer behavior

- In multiplayer, pressing the binding does not open the map.
- Player receives a clear error message or beep.

## 22. Testing Plan

### Manual test missions

Use or create simple missions with:

1. One player ship and one friendly ship.
2. Multiple hostile ships at different X/Z positions.
3. Ships at different Y elevations for drop-line verification.
4. Large capital ships and small fighters.
5. Ships with and without radar icon assets.
6. A ship that is destroyed while map is open.
7. A mission with no hostile ships.
8. A mission with a very large coordinate spread.

### Manual tests

1. Launch mission.
2. Open tactical map.
3. Verify simulation pauses.
4. Verify all ships render.
5. Verify grid and drop lines.
6. Hover each icon.
7. Click each icon.
8. Verify info panel.
9. Verify current target highlight.
10. Close map.
11. Verify gameplay resumes.
12. Reopen map after ships move/die.
13. Verify selected stale contact is cleared safely.

### Regression checks

- Normal pause still works.
- Photo mode still works.
- Controls config still works.
- Existing missions load normally.
- Multiplayer does not break from new binding.

## 23. Suggested Implementation Order

### Phase 1: State and binding skeleton

1. Add event/state enums and names.
2. Add empty tactical map module.
3. Wire state enter/do/leave in `freespace2/freespace.cpp`.
4. Add control binding and input dispatch.
5. Verify key opens/closes a blank paused screen.

### Phase 2: Basic paused screen UI

1. Add full-screen background.
2. Add title/help bars.
3. Add cursor handling.
4. Add Esc close.
5. Verify time/audio pause and resume.

### Phase 3: Contact collection and projection

1. Collect active ships.
2. Compute bounds/autofit.
3. Project X/Z/Y to screen positions.
4. Render simple dots/icons.
5. Render labels for debugging.

### Phase 4: Grid and drop lines

1. Draw dynamic grid.
2. Draw world axes.
3. Draw drop lines and ground X markers.
4. Tune vertical scale.

### Phase 5: Click selection and info panel

1. Add hover hit-testing.
2. Add click selection.
3. Add side info panel.
4. Add safe stale-selection validation.

### Phase 6: Target integration

1. On selected contact, call `set_target_objnum(Player_ai, objnum)` if valid.
2. Highlight current player target.
3. Add option or internal setting to disable target-on-click if desired.

### Phase 7: Polish

1. Use radar 2D icons.
2. Add fallback shapes by ship type.
3. Add zoom/pan/reset controls.
4. Add compact hover tooltip.
5. Add multiplayer disabled message.
6. Add localization IDs if needed.

## 24. Risks and Mitigations

### Risk: Control config enum/mapping inconsistency

Mitigation:

- Update every action map in `controlsconfigcommon.cpp`.
- Run the game/control config after adding the action.
- Watch for `CCFG_MAX` or action-map assertions.

### Risk: Incorrect pause/audio restoration

Mitigation:

- Mirror existing pause screen behavior first.
- Test opening/closing repeatedly.
- Test opening map after messages/music are playing.

### Risk: Stale object pointers

Mitigation:

- Store object numbers and signatures only.
- Validate before every use.
- Rebuild contact list every frame while map is open.

### Risk: Map clutter

Mitigation:

- MVP can still show all ships.
- Add zoom/pan/reset in the first iteration.
- Add label filtering later.

### Risk: Revealing hidden mission information

Mitigation:

- Keep MVP behavior as requested: all ships.
- Later add mission/mod option for sensor-respecting behavior.

### Risk: Multiplayer desync

Mitigation:

- Disable tactical map in multiplayer for MVP.
- Add network-aware support later as a separate design.

## 25. Future Enhancements

After MVP, possible extensions include:

- sensor-respecting visibility modes
- waypoint and objective markers
- jump node markers
- bombs/torpedoes display
- wing grouping and filters
- click-to-issue-orders for command/RTS mods
- search/filter list
- range rings around selected ship/player
- AWACS/sensor coverage overlays
- mission-designer configuration in FRED/QtFRED
- scripting hooks for custom icons/info/actions
- multiplayer host-authoritative tactical pause
