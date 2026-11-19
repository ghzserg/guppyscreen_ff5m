#include "pro_panel.h"
#include "nav.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <utility>

LV_IMG_DECLARE(air_circulation_internal_img);
LV_IMG_DECLARE(air_circulation_external_img);
LV_IMG_DECLARE(air_circulation_stop_img);
LV_IMG_DECLARE(code_img);

ProPanel::ProPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(Nav::get().content()))
  , air_circulation_internal_btn(cont, &air_circulation_internal_img, _("Internal circulation") /* "Внутренняя\nциркуляция" */, &ProPanel::_handle_callback, this)
  , air_circulation_external_btn(cont, &air_circulation_external_img, _("External circulation") /* "Внешняя\nциркуляция" */, &ProPanel::_handle_callback, this)
  , air_circulation_stop_btn(cont, &air_circulation_stop_img, _("Stop circulation") /* "Остановить\nциркуляцию" */, &ProPanel::_handle_callback, this)
  , calibrate_extruders_btn(cont, &code_img, _("Calibrate extruders") /* "Калибровка экструдеров" */, &ProPanel::_handle_callback, this)
  , calibrate_vfa_btn(cont, &code_img, _("Calibrate VFA") /* "Калибровка VFA" */, &ProPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(5), LV_GRID_FR(5), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
  lv_obj_set_grid_cell(air_circulation_internal_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(air_circulation_external_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 1, 1);
  lv_obj_set_grid_cell(air_circulation_stop_btn.get_container(),     LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 1, 1);

  // row 2
  lv_obj_set_grid_cell(calibrate_extruders_btn.get_container(),      LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(calibrate_vfa_btn.get_container(),            LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 2, 1);
}

#ifdef GUPPY_FF5M
bool ProPanel::configure(bool circulation, bool calibrate_extruders, bool calibrate_vfa) {
  const std::pair<ButtonContainer *, bool> buttons[] = {
      {&air_circulation_internal_btn, circulation}, {&air_circulation_external_btn, circulation},
      {&air_circulation_stop_btn, circulation}, {&calibrate_extruders_btn, calibrate_extruders},
      {&calibrate_vfa_btn, calibrate_vfa}};
  bool any = false;
  for (const auto &[btn, show] : buttons) {
    if (show) {
      lv_obj_clear_flag(btn->get_container(), LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(btn->get_container(), LV_OBJ_FLAG_HIDDEN);
    }
    any = any || show;
  }
  // Без циркуляции калибровки поднимаются в первый ряд: без пустого ряда.
  const int row = circulation ? 2 : 1;
  lv_obj_set_grid_cell(calibrate_extruders_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, row, 1);
  lv_obj_set_grid_cell(calibrate_vfa_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, row, 1);
  return any;
}
#endif

ProPanel::~ProPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void ProPanel::consume(json &j) {
}

void ProPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void ProPanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == air_circulation_internal_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_INTERNAL");
  } else if (btn == air_circulation_external_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_EXTERNAL");
  } else if (btn == air_circulation_stop_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_STOP");
  } else if (btn == calibrate_extruders_btn.get_container()) {
    ws.gcode_script("CALIBRATE_EXTRUDERS");
  } else if (btn == calibrate_vfa_btn.get_container()) {
    ws.gcode_script("CALIBRATE_VFA");
  }
}
