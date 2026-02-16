#include "pid_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(pid_extruder_img);
LV_IMG_DECLARE(pid_bed_img);
LV_IMG_DECLARE(clear_nozzle_img);
LV_IMG_DECLARE(bed_level_screws_tune_img);
LV_IMG_DECLARE(load_cell_tare_img);
LV_IMG_DECLARE(heater);
LV_IMG_DECLARE(back);

PidPanel::PidPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , numpad(Numpad(cont))
  , temp_cont(lv_obj_create(cont))
  , temp_chart(lv_chart_create(cont))
  , extruder_temp_series(lv_chart_add_series(temp_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y))
  , heater_bed_temp_series(lv_chart_add_series(temp_chart, lv_palette_main(LV_PALETTE_PURPLE), LV_CHART_AXIS_PRIMARY_Y))
  , weight_temp_series(lv_chart_add_series(temp_chart, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y))
  , extruder_temp(ws, temp_cont, &extruder, 150,
          _("Extruder") /* "Экструдер" */, lv_palette_main(LV_PALETTE_RED), true, true, numpad, "extruder", temp_chart, extruder_temp_series, true)
  , heater_bed_temp(ws, temp_cont, &bed, 150,
          _("Bed") /* "Стол" */, lv_palette_main(LV_PALETTE_PURPLE), true, true, numpad,
 "heater_bed", temp_chart, heater_bed_temp_series, true)
  , weight_temp(ws, temp_cont, &heater, 150,
          _("Weight") /* "Вес" */, lv_palette_main(LV_PALETTE_ORANGE), false, false, numpad, "temperature_sensor weightValue", temp_chart, weight_temp_series, true)
  , pid_extruder_btn(cont, &pid_extruder_img, _("PID Extruder") /* "PID Экструдера" */, &PidPanel::_handle_callback, this)
  , pid_bed_btn(cont, &pid_bed_img, _("PID Bed") /* "PID Стола" */, &PidPanel::_handle_callback, this)
  , clear_nozzle_btn(cont, &clear_nozzle_img, _("Clear nozzle") /* "Очистить сопло" */, &PidPanel::_handle_callback, this)
  , load_cell_tare_btn(cont, &load_cell_tare_img, _("Load Cell") /* "Сбросить вес" */, &PidPanel::_handle_callback, this)
  , bed_level_screws_tune_btn(cont, &bed_level_screws_tune_img, _("Screw adjustment") /* "Регулировка винтов" */, &PidPanel::_handle_callback, this)
  , back_btn(cont, &back, _("Back") /* "Назад" */, &PidPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(temp_cont, LV_PCT(50), LV_PCT(62));
  lv_obj_set_style_pad_top(temp_cont, 4, 0);
  lv_obj_set_style_pad_bottom(temp_cont, 0, 0);

  lv_obj_set_style_pad_all(temp_cont, 0, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 0, 2);

  lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(temp_chart, LV_PCT(45), LV_PCT(40));

  lv_obj_set_style_pad_top(temp_chart, 0, 0);
  lv_obj_set_style_pad_bottom(temp_chart, 4, 0);

  lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 350);
  lv_obj_set_grid_cell(temp_chart, LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_END, 2, 1);
  lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 5, true, 50);

  lv_chart_set_div_line_count(temp_chart, 3, 8);
  lv_chart_set_point_count(temp_chart, 5000);
  lv_chart_set_zoom_x(temp_chart, 5000);
  lv_obj_scroll_to_x(temp_chart, LV_COORD_MAX, LV_ANIM_OFF);

  extruder_temp.update_target(245);
  heater_bed_temp.update_target(80);

//  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(cont, LV_PCT(100));

  lv_obj_set_flex_grow(cont, 1);
  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_style_pad_top(cont, 0, 0);
  lv_obj_set_style_pad_bottom(cont, 0, 0);

  lv_obj_set_grid_cell(pid_extruder_btn.get_container(),          LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(pid_bed_btn.get_container(),               LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(clear_nozzle_btn.get_container(),          LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(bed_level_screws_tune_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(load_cell_tare_btn.get_container(),        LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(back_btn.get_container(),                  LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  ws.register_notify_update(this);
}

PidPanel::~PidPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void PidPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto temp_value = j["/params/0/extruder/temperature"_json_pointer];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    extruder_temp.update_series(value);
    extruder_temp.update_value(value);
  }

  temp_value = j["/params/0/heater_bed/temperature"_json_pointer];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    heater_bed_temp.update_series(value);
    heater_bed_temp.update_value(value);
  }

  temp_value = j["/params/0/temperature_sensor weightValue/temperature"_json_pointer];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    weight_temp.update_series(value);
    weight_temp.update_value(value);
  }
}

void PidPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void PidPanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
  } else if (btn == pid_extruder_btn.get_container()) {
    pid_extruder_btn.disable();
    ws.gcode_script(fmt::format("PID_TUNE_EXTRUDER TEMPERATURE={}", extruder_temp.get_target_value()));
    pid_extruder_btn.enable();
  } else if (btn == pid_bed_btn.get_container()) {
    pid_bed_btn.disable();
    ws.gcode_script(fmt::format("PID_TUNE_BED TEMPERATURE={}", heater_bed_temp.get_target_value()));
    pid_bed_btn.enable();
  } else if (btn == clear_nozzle_btn.get_container()) {
    clear_nozzle_btn.disable();
    ws.gcode_script(fmt::format("CLEAR_NOZZLE EXTRUDER_TEMP={} BED_TEMP={}", extruder_temp.get_target_value(), heater_bed_temp.get_target_value()));
    clear_nozzle_btn.enable();
  } else if (btn == bed_level_screws_tune_btn.get_container()) {
    bed_level_screws_tune_btn.disable();
    ws.gcode_script(fmt::format("BED_LEVEL_SCREWS_TUNE EXTRUDER_TEMP={} BED_TEMP={}", extruder_temp.get_target_value(), heater_bed_temp.get_target_value()));
    bed_level_screws_tune_btn.enable();
  } else if (btn == load_cell_tare_btn.get_container()) {
    load_cell_tare_btn.disable();
    ws.gcode_script("LOAD_CELL_TARE");
    load_cell_tare_btn.enable();
  }
}
