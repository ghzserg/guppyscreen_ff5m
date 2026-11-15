# Navigation bar on every screen

## Intent
Today the left tab bar (Home / Tune / Settings / Macros / Console) is the
tab-button strip of an `lv_tabview` and is visible only on those five tabs.
Every other screen (Homing, Extruder, Fans, LED, file list, print status,
Fine Tune, Limits, Retract, PID, Pro, Input Shaper, Belts, Bed mesh,
Exclude, Spoolman, Sysinfo) is a full-screen 800×480 container on
`lv_scr_act()` that covers the bar, and has its own "Back" button.

The user wants the bar on every screen, for three reasons:
1. **Stop always in one place.** Emergency stop matters in many modes, but
   today it sits in a different cell on each screen (main, Homing, Input
   Shaper, Belts, print status) and is missing on the others. It moves to
   the bottom of the bar, reachable from every screen.
2. **No Back buttons.** The bar does the navigation; the grids get the
   space back.
3. **Maybe, later: a different set of tabs per screen.** Not decided. The
   bar's tab list is data, so this can be added without a rewrite.

## Goals
- A 60 px bar on the left of every screen except the ones listed under
  "Screens that cover the bar": tab buttons from a list, Stop at the bottom.
- Bar tabs: Home, Print, Tune, Settings. **Macros and
  Console leave the bar** (rarely used, decided by the user): they open from
  two new buttons on the Settings screen and are Settings sub-screens, left
  through any tab. My call on the place: a third row in the Settings grid
  (rows `2:5:5` → `2:5:5:5`), Macros in column 0, Console in column 1.
- Every other screen lives in a 740×480 content area to the right of the bar.
- Tapping a tab (also the active one) closes whatever is open and shows that
  tab's root screen.
- A **Print** tab, always present: the file list when idle, the print status
  screen while printing. Fine Tune / Exclude / Retract opened from it are
  left by tapping Print again.
- Stop in the bar sends `printer.emergency_stop`, with the confirmation
  dialog when `/prompt_emergency_stop` is true, exactly like today's Stop
  buttons.
- The Stop buttons in the grids (main `stop`, homing `emergency`, inputshaper
  `emergency`, belts `emergency`, print status `emergency`) and all Back
  buttons are removed.

## Screens that cover the bar
Only these stay full screen over the bar (no Stop on them): the init panel
(no connection), the screen saver (screen off), and the panels built only
without `GUPPY_FF5M` (see Non-goals). Every FF5M screen, including the
Exclude map, Bed mesh and the Input Shaper graphs, goes into the content area.

## Non-goals
- Per-screen tab sets (point 3): only the Print tab is dynamic now.
- Configuring the bar from `guppyconfig.json` (width, order, hidden tabs).
- Panels compiled only without `GUPPY_FF5M` (wifi, TMC tune/status, power,
  printer select): left as they are — full screen over the bar, with their
  Back button. They are not built by `make test` or for the printer.

## Anti-goals
- A screen left through the bar that keeps memory its Back button used to
  free (Input Shaper graphs, 1.5 MB each decoded; Belts graph; file list
  thumbnail).
- A Klipper prompt (`prompt_panel`) lost or hidden by a tab switch.
- A screen that is no longer reachable, or that can't be left, without Back.
- Stop that does nothing because a dialog or another screen is on top of it.

## Approaches
- **A. Keep `lv_tabview`, add Print and Stop to its button matrix.** Stop
  would be a fake tab whose selection is intercepted; Print shown/hidden with
  `LV_BTNMATRIX_CTRL_HIDDEN` (LVGL 8 can't remove tabs); sub-screens still
  need moving by hand. Fragile, and per-screen tab sets would hit its limits.
- **B. Own nav bar + one content area, tabview removed (chosen).** A column
  of buttons on the left; a 740×480 container on the right that holds the
  tab roots and every sub-screen as siblings. Panels already open/close by
  `lv_obj_move_foreground/background`; switching tab = closing open screens
  and moving that tab's root to the front. Sub-screens get 740 px just by
  changing their parent from `lv_scr_act()` to the content area.
- **C. Keep tabview for the five roots, own bar beside it.** Hide the
  tabview's own buttons and still keep a separate container for
  sub-screens: two mechanisms instead of one.

The user picked B.

## Design (decided)

### Screen structure
```
lv_scr_act()
├── nav bar        x 0..59, full height: tab buttons, spacer, Stop
├── content area   x 60..799, 740×480
│   ├── tab roots: main, print (file list or print status), tune, settings
│   └── sub-screens: homing, extruder, fans, led, finetune, limits,
│       retract, pid, pro, inputshaper, belts, bedmesh, exclude, spoolman, sysinfo,
│       macros, console
├── dialogs over the content area (see below)
├── init panel, screen saver (full screen, over the bar)
└── lv_layer_top: lv_msgbox confirmations (modal, full screen) — unchanged
```
- The content area must exist before any panel is constructed
  (`SpoolmanPanel` is built by `GuppyScreen` before `MainPanel`).
- Every panel listed under "sub-screens" is created with the content area
  as its parent instead of `lv_scr_act()` and keeps 100%×100% size.
- Today's tab roots `main_tab`, `printertune_tab`, `setting_tab` become plain
  containers in the content area; `macros_tab` and `console_tab` become
  sub-screen containers opened from Settings.

### Navigation rules
- Tapping a tab: every open sub-screen is closed (with its leave action, see
  below), the tab's root comes to the front, the tab is highlighted.
- A sub-screen opened from a root (e.g. Homing from Home) keeps its tab
  highlighted.
- Second-level screens (Extruder → Spoolman, Settings → Sysinfo, Tune → Bed
  mesh) are left only through a tab (variant "a").
- Buttons that today jump between screens keep doing so through the nav:
  print status temperature/fan buttons → Home tab; mini print status on main
  → Print tab.
- Macros: `macros_panel.foreground()` (builds the list on first show) when
  its screen opens from Settings, `background()` as its leave action (today's
  `handle_tab_change`).
- Settings → Macros / Console are second-level screens like Sysinfo.

### Leave actions
What a Back button did besides `move_background` must still happen when the
screen is closed by the bar:
- Input Shaper: `KUtils::uncache_img(xgraph)`, `uncache_img(ygraph)`.
- Belts: `KUtils::uncache_img(graph)`.
- File list (a root now, so on leaving the Print tab): `file_panel.uncache_thumbnail()`.
- Print status: today Back only hides it.

Each sub-screen registers its container and its leave action with the nav;
closing a screen runs the action.

### Print tab
Always in the bar (decided by the user). Its root depends on the print state:
- **No print** (`print_stats.state` not `printing`/`paused`): the file list
  (`print_panel`'s `files_cont`). Home's Print button is removed (decided by
  the user): the Print tab replaces it.
- **Printing or paused:** the print status screen.

Two nav operations: *switch the tab's root* (follows the state) and
*activate* the tab (closes open screens, runs leave actions). My calls on
when each happens:
- **Root switch** only by the state: `printing`/`paused` → print status,
  anything else → file list. Pressing Cancel does not switch it; that happens
  when Klipper reports `cancelled` (the confirmation for Cancel is on by
  default anyway). If the Print tab is active during a switch, the screens
  open over the old root are closed (e.g. Fine Tune over the print status
  when the print completes), and the tab stays active, now showing the file
  list, ready for the next print.
- **Activated** only when a print starts: a new non-empty
  `print_stats.filename` (the empty one from `SDCARD_RESET_FILE` is
  ignored), and on connect/reconnect when the printer is already printing or
  paused (nothing is open then). Pause and resume do **not** activate it:
  during a filament change (pause → Klipper prompt → Extruder → resume) the
  Extruder screen must not close under the user.
- Leaving the Print tab runs its root's leave action: for the file list,
  `file_panel.uncache_thumbnail()` (what Back did).
- The file list's Status button goes: while printing the Print tab shows the
  status, and the file list is not reachable; when idle there is no status.
- Print tab icon: the `print` image zoomed to fit; position second, under
  Home (my call: the main action on a printer).

### Stop button
- Bottom of the bar, icon only (the `emergency` image, 64×64, zoomed to fit
  the 60 px bar). The background is the bar's usual one; the icon itself is
  red (decided by the user).
- Same behaviour as `ButtonContainer` with a prompt: confirmation msgbox when
  `/prompt_emergency_stop` is true.
- Visible on every screen in the content area and over dialogs in the
  content area. Not on the init panel and screen saver (no connection / screen
  off).

### Dialogs (my call)
Klipper action prompts (`prompt_panel`), the print confirmation in the file
list (`print_panel` `prompt_cont`) and the bed mesh save prompt are dialogs:
- They cover the content area only (740×480), so Stop stays usable.
- While one is open, the tab buttons are disabled (they are answered first);
  a tab switch must not bury a Klipper prompt.

### Layout config and preview
- Removed item ids disappear from the layouts: every `back`, main `stop` and `print`, print_files `status`,
  homing/inputshaper/belts/print_status `emergency`. Their configs in
  `guppyconfig.json` are ignored (unknown ids are already ignored).
- `preview/guppyconfig.json` is updated to drop those items.
- The bar itself is not in `layout` (non-goal).

### Tests
- New tests: the bar on a sub-screen; a tab tap closes the sub-screen and runs
  its leave action; the Print tab shows the file list when idle, the print
  status on print start, the file list again on completion; Stop from a sub-screen sends `printer.emergency_stop` (with and
  without the prompt); a Klipper prompt survives a tab tap.
- Existing tests that tap the Macros tab (`macros_test.cpp` `tap_tab`) open
  Macros from Settings instead; "back to Home" is a tap on the Home tab.
- Snapshot fixtures (`layout_snapshot.txt`, `layout_items.txt`,
  `layout_default.json`) are regenerated on purpose: almost every screen
  changes width (800 → 740) and loses Back/Stop.

### BUGS.md #4
Leaving the file list drops a thumbnail the print status may still draw.
With the file list reachable only when idle, the print status no longer
draws the same file at that moment, so the case mostly goes away; the bug
itself stays out of scope.

## Left to the builder
- Class names and files (e.g. `nav_bar.{h,cpp}`), how panels register.
- How the print status screen sizes its thumbnail and filename (today 35% of
  the display width, `lv_disp_get_physical_hor_res`): base it on the content
  area so it fits 740 px.
- Fixing grids that no longer fit after Back/Stop are removed (empty cells,
  `layout` defaults) — keep each screen readable; the snapshot shows the
  result.
- Bar styling beyond what is above (keep today's look: grey highlight on the
  active tab, `materialdesign_font_40`).

## Constraints
- Project rules (CLAUDE.md): a smoke test before refactoring a path; one
  process per test; no commas in test names; `GUPPY_FF5M` guards kept.
- Must cross-build for the printer and fit in RAM as today (no new large
  allocations; the bar replaces the tabview's button matrix).
- Final verification on the printer (touch, Stop, Print tab during a real print).
