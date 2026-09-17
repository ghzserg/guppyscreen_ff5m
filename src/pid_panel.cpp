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
  , tools_btnmatrix(NULL)
  , active_tool_id(0)
  , total_tools(1)
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

  // Создание матрицы кнопок выбора инструментов (T0, T1, T2, T3)
  static const char * btnm_map[] = {"T0", "T1", "T2", "T3", ""};
  tools_btnmatrix = lv_btnmatrix_create(temp_cont);
  lv_btnmatrix_set_map(tools_btnmatrix, btnm_map);
  lv_btnmatrix_set_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(tools_btnmatrix, true);
  lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN); // По умолчанию скрыто, покажем если экструдеров > 1
  lv_obj_add_event_cb(tools_btnmatrix, &PidPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_clear_flag(tools_btnmatrix, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(tools_btnmatrix, 180, 40);
  lv_obj_set_style_pad_top(tools_btnmatrix, 15, 0);
  lv_obj_set_style_pad_column(tools_btnmatrix, 10, 0);
  lv_obj_set_style_pad_all(tools_btnmatrix, 2, 0);

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
  static bool dump_done = false;
  static int last_rendered_tool_id = -99;

  // Инициализация при первом проходе (получаем общее количество и текущий активный инструмент)
  if (!dump_done) {
    dump_done = true;
    State *state = State::get_instance();

    auto v_tools = state->get_data("/printer_state/zmod_color/total_tools"_json_pointer);
    if (!v_tools.is_null()) {
      total_tools = v_tools.template get<int>();
    }

    auto v_active = state->get_data("/printer_state/zmod_color/active_tool_id"_json_pointer);
    if (!v_active.is_null()) {
      active_tool_id = v_active.template get<int>();
      if (active_tool_id >= 0 && active_tool_id < total_tools) {
        last_rendered_tool_id = active_tool_id;
        lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
      } else {
        lv_btnmatrix_clear_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKED);
      }
    }

    if (total_tools > 1) {
      lv_obj_clear_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Обновление активного инструмента из входящего сокета
  auto zmod_active = j["/params/0/zmod_color/active_tool_id"_json_pointer];
  if (!zmod_active.is_null()) {
    active_tool_id = zmod_active.template get<int>();
    if (active_tool_id >= 0 && active_tool_id < total_tools) {
      if (active_tool_id != last_rendered_tool_id) {
        last_rendered_tool_id = active_tool_id;
      }
      lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
    } else {
      last_rendered_tool_id = -99;
      lv_btnmatrix_clear_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKED);
    }
  }

  // Вычисляем системное имя текущего нагревателя экструдера (extruder, extruder1, extruder2...)
  int effective_id = (active_tool_id >= 0) ? active_tool_id : 0;
  std::string ext_name = "extruder" + (effective_id > 0 ? std::to_string(effective_id) : "");

  // Динамически запрашиваем температуру для активного экструдера
  auto temp_value = j[json::json_pointer("/params/0/" + ext_name + "/temperature")];
  if (temp_value.is_null()) {
    temp_value = State::get_instance()->get_data(json::json_pointer("/printer_state/" + ext_name + "/temperature"));
  }

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
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    if (selector == tools_btnmatrix) {
      uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
      active_tool_id = idx;
      return;
    }
  }

  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;

  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
  } else if (btn == pid_extruder_btn.get_container()) {
    pid_extruder_btn.disable();

    ws.gcode_script(fmt::format("PID_TUNE_EXTRUDER T={} TEMPERATURE={}", active_tool_id, extruder_temp.get_target_value()));

    pid_extruder_btn.enable();
  } else if (btn == pid_bed_btn.get_container()) {
    pid_bed_btn.disable();
    ws.gcode_script(fmt::format("PID_TUNE_BED TEMPERATURE={}", heater_bed_temp.get_target_value()));
    pid_bed_btn.enable();
  } else if (btn == clear_nozzle_btn.get_container()) {
    clear_nozzle_btn.disable();
    ws.gcode_script(fmt::format("CLEAR_NOZZLE T={} EXTRUDER_TEMP={} BED_TEMP={}", active_tool_id, extruder_temp.get_target_value(), heater_bed_temp.get_target_value()));
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
