#include "pro_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(air_circulation_internal_img);
LV_IMG_DECLARE(air_circulation_external_img);
LV_IMG_DECLARE(air_circulation_stop_img);
LV_IMG_DECLARE(back);

ProPanel::ProPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , air_circulation_internal_btn(cont, &air_circulation_internal_img, _("Internal circulation") /* "Внутренняя\nциркуляция" */, &ProPanel::_handle_callback, this)
  , air_circulation_external_btn(cont, &air_circulation_external_img, _("External circulation") /* "Внешняя\nциркуляция" */, &ProPanel::_handle_callback, this)
  , air_circulation_stop_btn(cont, &air_circulation_stop_img, _("Stop circulation") /* "Остановить\nциркуляцию" */, &ProPanel::_handle_callback, this)
  , back_btn(cont, &back, _("Back") /* "Назад" */, &ProPanel::_handle_callback, this)
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

  // row 3
  lv_obj_set_grid_cell(back_btn.get_container(),                     LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_START, 3, 1);
}

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

  if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
  } else if (btn == air_circulation_internal_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_INTERNAL");
  } else if (btn == air_circulation_external_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_EXTERNAL");
  } else if (btn == air_circulation_stop_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_STOP");
  }
}
