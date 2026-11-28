#ifndef __NOZZLE_TILES_H__
#define __NOZZLE_TILES_H__

#ifdef GUPPY_FF5M

#include "sensor_container.h"
#include "numpad.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// A row of Home-style tiles, one per nozzle (extruder, extruder1, ...), for
// the screens that work on a nozzle (Extrude, PID). They replace the screen's
// single extruder sensor. With several nozzles (C5 Pro) a tap selects one: a
// blue background, square like Home's tiles. Every tap asks the owner's
// callback, which says whether it selects. A nozzle's tool is the number in its
// extruder's name (extruder 0, extruderN N), whichever nozzles the config
// leaves out.
class NozzleTiles {
 public:
  struct Nozzle {
    std::string id;  // extruder, extruder1, ...
    lv_color_t color;
    int tool;  // 0 for extruder, N for extruderN
  };
  // The tool of Klipper object `id`, or -1 if it isn't an extruder's heater.
  static int tool_of(const std::string &id);
  // Whether the tap on `tool`'s tile selects it.
  using TapCb = std::function<bool(int tool)>;

  NozzleTiles(KWebSocketClient &ws, lv_obj_t *parent, Numpad &np);

  // A flex row: the owner places it.
  lv_obj_t *get_container() { return cont; }

  // Rebuilds the tiles and hides `replaced`; with no nozzles, shows it again.
  // `stripe`: Home's colour stripe along the bottom (transparent otherwise,
  // the icon and digits still where Home's are). `selected`: the tool
  // selected at first.
  void create(const std::vector<Nozzle> &nozzles, SensorContainer &replaced, bool stripe, int selected,
              TapCb on_tap);

  bool empty() const { return tiles.empty(); }
  bool selectable() const { return tiles.size() > 1; }

  // The blue background on the tile of `tool`, if selectable.
  void show_selected(int tool);
  void set_disabled(bool disabled);
  // The tool shown selected; else the first tile's (the only one's), or -1
  // without tiles.
  int selected() const;
  bool has_selection() const { return index_of(shown) >= 0; }
  // The tile and nozzle of `tool`, or nullptr.
  SensorContainer *tile(int tool);
  const Nozzle *nozzle(int tool) const;
  // The same target on every tile (PID's calibration temperature).
  void set_targets(int target);

  // The temperatures from a status update, and the targets if `targets`;
  // from State while the tiles are new.
  void update(const json &j, bool targets);

 private:
  KWebSocketClient &ws;
  Numpad &numpad;
  lv_obj_t *cont;
  std::vector<Nozzle> nozzles;
  std::vector<std::unique_ptr<SensorContainer>> tiles;  // as nozzles
  int index_of(int tool) const;  // -1 if none
  TapCb on_tap;
  int shown = -1;  // the tool drawn selected
  bool seeded = false;  // read from State once

  static void _handle_tap(lv_event_t *event);
};

#endif  // GUPPY_FF5M

#endif  // __NOZZLE_TILES_H__
