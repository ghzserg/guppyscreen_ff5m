#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#ifdef GUPPY_FF5M
#include "nozzle_tiles.h"
#endif
#include "lvgl/lvgl.h"

#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void enable_spoolman();
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

#ifdef GUPPY_FF5M
  // A tile per nozzle, like Home's without the colour stripe, replaces the
  // extruder line. With several nozzles (C5 Pro) they also replace the T0-T3
  // buttons: the printer's tool has a blue background.
  // На C5 Pro нажатие сразу вызывает _T_USE (кроме печати).
  // One nozzle (AD5M, AD5X) is not selectable. Called by
  // MainPanel::create_sensors, under the LVGL lock, with Home's names and
  // colours and where Home's tiles start in its content area (`left`, `top`).
  void create_nozzle_tiles(const std::vector<NozzleTiles::Nozzle> &nozzles, lv_coord_t left, lv_coord_t top);
#endif

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  SpoolmanPanel &spoolman_panel;
  SensorContainer extruder_temp;
  Selector temp_selector;
  Selector length_selector;
  Selector speed_selector;
  lv_obj_t *rightside_btns_cont;
  lv_obj_t *leftside_btns_cont;
  lv_obj_t *tools_btnmatrix;
  int active_tool_id;
  int total_tools;
  ButtonContainer load_btn;
  ButtonContainer retract_btn;
  ButtonContainer code_btn;
  ButtonContainer spoolman_btn;
  ButtonContainer cooldown_btn;
  ButtonContainer coldpull_btn;
  std::string load_filament_macro;
  std::string code_macro;
  std::string cooldown_macro;
#ifdef GUPPY_FF5M
  ButtonContainer remove_tool_btn;
  NozzleTiles nozzle_tiles;
#endif
  // Load, Unload and ColdPull: on with a tool in the head; on FF5M with
  // selectable nozzles (C5 Pro), while one is picked.
  void enable_actions(bool tool_in_head);
};

#endif // __EXTRUDER_PANEL_H__
