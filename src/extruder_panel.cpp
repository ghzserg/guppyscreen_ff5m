#include "extruder_panel.h"
#include "state.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <limits>

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(spoolman_img);
LV_IMG_DECLARE(coldpull);
LV_IMG_DECLARE(extrude_img);
LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(code_img);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(cooldown_img);

ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
                             std::mutex &lock,
                             Numpad &numpad
                             ,SpoolmanPanel &sm
)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , spoolman_panel(sm)
  , extruder_temp(ws, panel_cont, &extruder, 150,
          _("Extruder") /* "Экструдер" */, lv_palette_main(LV_PALETTE_RED), false, true, numpad, "extruder", NULL, NULL)
  , temp_selector(panel_cont, _("Extruder temperature") /* "Температура экструдера (C)" */,
                  {"200", "220", "240", "250", "260", "280", "300", "320", ""}, 3, &ExtruderPanel::_handle_callback, this)
  , length_selector(panel_cont, _("Extrusion length") /* "Длина экструзии (мм)" */,
                  {"10", "20", "50", "100", "120", "200", "300", ""}, 3, &ExtruderPanel::_handle_callback, this)
  , speed_selector(panel_cont, _("Extrusion speed (mm/s)") /* "Скорость экструзии (мм/с)" */,
                  {"1", "2", "5", "10", "25", "35", "50", ""}, 2, &ExtruderPanel::_handle_callback, this)
  , rightside_btns_cont(lv_obj_create(panel_cont))
  , leftside_btns_cont(lv_obj_create(panel_cont))
  , tools_btnmatrix(NULL)
  , active_tool_id(0)
  , total_tools(1)
  , load_btn(leftside_btns_cont, &extrude_img, _("Load") /* "Загрузить" */, &ExtruderPanel::_handle_callback, this)
  , retract_btn(leftside_btns_cont, &retract_img, _("Unload") /* "Выгрузить" */, &ExtruderPanel::_handle_callback, this)
  , code_btn(leftside_btns_cont, &code_img, _("Macro") /* "Макрос" */, &ExtruderPanel::_handle_callback, this)
  , spoolman_btn(rightside_btns_cont, &spoolman_img, "Spoolman", &ExtruderPanel::_handle_callback, this)
  , cooldown_btn(rightside_btns_cont, &cooldown_img, _("Cool down") /* "Остудить" */, &ExtruderPanel::_handle_callback, this)
  , coldpull_btn(rightside_btns_cont, &coldpull, "ColdPull", &ExtruderPanel::_handle_callback, this)
  , back_btn(rightside_btns_cont, &back, _("Back") /* "Назад" */, &ExtruderPanel::_handle_callback, this)
  , load_filament_macro("LOAD_FILAMENT")
  , code_macro("_GUPPY_MACRO")
  , cooldown_macro("TURN_OFF_HEATERS")
{
  Config *conf = Config::get_instance();
  auto df = conf->get_json("/default_printer");
  if (!df.empty()) {
    auto v = conf->get_json(conf->df() + "default_macros/load_filament");
    if (!v.is_null()) {
      load_filament_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/code");
    if (!v.is_null()) {
      code_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/cooldown");
    if (!v.is_null()) {
      cooldown_macro = v.template get<std::string>();
    }
  }

  lv_obj_move_background(panel_cont);
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  lv_obj_set_size(rightside_btns_cont, LV_PCT(20), LV_PCT(100));
  lv_obj_set_flex_flow(rightside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rightside_btns_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(rightside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(leftside_btns_cont, LV_PCT(20), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(leftside_btns_cont, 15, 0);
  lv_obj_set_flex_flow(leftside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(leftside_btns_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(leftside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);

  static const char * btnm_map[] = {"T0", "T1", "T2", "T3", ""};
  tools_btnmatrix = lv_btnmatrix_create(panel_cont);
  lv_btnmatrix_set_map(tools_btnmatrix, btnm_map);
  lv_btnmatrix_set_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(tools_btnmatrix, true);
  lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(tools_btnmatrix, &ExtruderPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);

  // Переводим кнопки в плавающий режим и жестко выравниваем по правому верхнему углу
  lv_obj_add_flag(tools_btnmatrix, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(tools_btnmatrix, LV_ALIGN_TOP_RIGHT, -110, 10); // Сдвиг на 110px влево (безопасная зона)
  lv_obj_set_size(tools_btnmatrix, 180, 35);                  // Компактный размер для аккуратного ряда

  // Устанавливаем зазоры между кнопками матрицы, чтобы T0 T1 T2 T3 не слипались
  lv_obj_set_style_pad_column(tools_btnmatrix, 10, 0); // Зазор 10px между кнопками
  lv_obj_set_style_pad_all(tools_btnmatrix, 2, 0);

#ifndef GUPPY_FF5M
  spoolman_btn.disable();
#endif

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(6), LV_GRID_FR(6), LV_GRID_FR(6),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(7), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};

  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);
  lv_obj_add_flag(extruder_temp.get_sensor(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(extruder_temp.get_sensor(), LV_ALIGN_TOP_LEFT, 50, 0);

  // lv_obj_set_size(extruder_temp.get_sensor(), 350, 60);
  // col 0
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 0, 2);
  //lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_END, 0, 2);
  //lv_obj_set_grid_cell(code_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 2);
  //lv_obj_set_grid_cell(cooldown_btn.get_container(), LV_GRID_ALIGN_END, 0, 1, LV_GRID_ALIGN_END, 2, 2);
  lv_obj_set_grid_cell(leftside_btns_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 3);

  // col 1
  // lv_obj_set_grid_cell(extruder_temp.get_sensor(), LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(spoolman_btn.get_container(),    LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START,  0, 1);
  lv_obj_set_grid_cell(temp_selector.get_container(),   LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(length_selector.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(speed_selector.get_container(),  LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 3, 1);

  // col 2
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 0, 2);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);

  lv_obj_set_grid_cell(rightside_btns_cont, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 0, 3);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);

  ws.register_notify_update(this);
}

ExtruderPanel::~ExtruderPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
}

void ExtruderPanel::consume(json& j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  static int last_rendered_tool_id = -99;
  static int last_rendered_target = -99;
  static int last_rendered_value = -99;
  static bool dump_done = false;

  if (!dump_done) { // Init
   dump_done = true;
   State *state = State::get_instance();

   auto v_tools = state->get_data("/printer_state/zmod_color/total_tools"_json_pointer);
   if (!v_tools.is_null()) {
     total_tools = v_tools.template get<int>();
     if (total_tools > 1) {
         lv_obj_clear_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN);
     } else {
         lv_obj_add_flag(tools_btnmatrix, LV_OBJ_FLAG_HIDDEN);
     }
   }

   // Синхронизируем начальный инструмент при открытии экрана
   auto v_active = state->get_data("/printer_state/zmod_color/active_tool_id"_json_pointer);
   if (!v_active.is_null()) {
     int klipper_tool_id = v_active.template get<int>();
     active_tool_id = klipper_tool_id;
     if (active_tool_id >= 0 && active_tool_id < total_tools) {
         last_rendered_tool_id = active_tool_id;
         lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
         load_btn.enable();
         retract_btn.enable();
         coldpull_btn.enable();
     } else {
         lv_btnmatrix_clear_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKED);
         load_btn.disable();
         retract_btn.disable();
         coldpull_btn.disable();
     }
   }
  } // End Init

  auto zmod_active = j["/params/0/zmod_color/active_tool_id"_json_pointer];
  if (!zmod_active.is_null()) {
    int klipper_tool_id = zmod_active.template get<int>();
    active_tool_id = klipper_tool_id;

    if (klipper_tool_id >= 0 && klipper_tool_id < total_tools) {
        if (active_tool_id != last_rendered_tool_id) {
            last_rendered_target = -99;
            last_rendered_value = -99;
            last_rendered_tool_id = active_tool_id;
        }
        lv_btnmatrix_set_btn_ctrl(tools_btnmatrix, active_tool_id, LV_BTNMATRIX_CTRL_CHECKED);
        load_btn.enable();
        retract_btn.enable();
        coldpull_btn.enable();
    } else {
        last_rendered_target = -99;
        last_rendered_value = -99;
        last_rendered_tool_id = -99;

        lv_btnmatrix_clear_btn_ctrl_all(tools_btnmatrix, LV_BTNMATRIX_CTRL_CHECKED);
        load_btn.disable();
        retract_btn.disable();
        coldpull_btn.disable();
    }
  }

  int effective_id = (active_tool_id >= 0) ? active_tool_id : 0;
  std::string ext_name = "extruder" + (effective_id > 0 ? std::to_string(effective_id) : "");

  auto target_value = j[json::json_pointer("/params/0/" + ext_name + "/target")];
  if (target_value.is_null() && last_rendered_target == -99) {
    target_value = State::get_instance()->get_data(json::json_pointer("/printer_state/" + ext_name + "/target"));
  }

  if (!target_value.is_null()) {
    int target = target_value.template get<int>();
    if (target != last_rendered_target) {
        extruder_temp.update_target(target);
        last_rendered_target = target;
    }
  }

  auto temp_value = j[json::json_pointer("/params/0/" + ext_name + "/temperature")];
  if (temp_value.is_null() && last_rendered_value == -99) {
    temp_value = State::get_instance()->get_data(json::json_pointer("/printer_state/" + ext_name + "/temperature"));
  }

  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    if (value != last_rendered_value) {
        extruder_temp.update_value(value);
        last_rendered_value = value;
    }
  }
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  spdlog::trace("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char * v = lv_btnmatrix_get_btn_text(selector, idx);

    if (selector == tools_btnmatrix) {
      active_tool_id = idx;
      ws.gcode_script(fmt::format("_T_USE T={}", active_tool_id));
      return;
    }

    if (selector == temp_selector.get_selector()) {
      temp_selector.set_selected_idx(idx);
    }

    if (selector == length_selector.get_selector()) {
      length_selector.set_selected_idx(idx);
    }

    if (selector == speed_selector.get_selector()) {
      speed_selector.set_selected_idx(idx);
    }

    spdlog::trace("selector {} {} {}, {} {} {}", fmt::ptr(selector), idx, v,
                  fmt::ptr(temp_selector.get_selector()),
                  fmt::ptr(length_selector.get_selector()),
                  fmt::ptr(speed_selector.get_selector()));
  } else if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(panel_cont);
    }

    if (btn == coldpull_btn.get_container()) {
      coldpull_btn.disable();
      ws.gcode_script(fmt::format("COLDPULL T={}", active_tool_id));
      coldpull_btn.enable();
    }

    if (btn == retract_btn.get_container()) {
      retract_btn.disable();
      const char * temp = lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
                                                   temp_selector.get_selected_idx());
      const char * len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
                                                   length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
                                                    speed_selector.get_selected_idx());
      ws.gcode_script(fmt::format("M109 S{} T{}\nM83\nG1 E-{} F{}", temp, active_tool_id, len, std::stoi(speed) * 60));
      retract_btn.enable();
    }

    if (btn == code_btn.get_container()) {
      code_btn.disable();
      const char *temp = lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
                                                     temp_selector.get_selected_idx());
      const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
                                                    length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
                                                    speed_selector.get_selected_idx());
      ws.gcode_script(fmt::format("{} T={} EXTRUDER_TEMP={} EXTRUDE_LEN={} SPEED={}", code_macro, active_tool_id, temp, len, std::stoi(speed) * 60));
      code_btn.enable();
    }

    if (btn == load_btn.get_container()) {
      load_btn.disable();
      const char *temp = lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
                                                   temp_selector.get_selected_idx());
      const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
                                                  length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
                                                    speed_selector.get_selected_idx());
      ws.gcode_script(fmt::format("{} T={} EXTRUDER_TEMP={} EXTRUDE_LEN={} SPEED={}", load_filament_macro, active_tool_id, temp, len, std::stoi(speed) * 60));
      load_btn.enable();
    }

    if (btn == cooldown_btn.get_container()) {
      cooldown_btn.disable();
      ws.gcode_script(cooldown_macro);
      cooldown_btn.enable();
    }

    if (btn == spoolman_btn.get_container()) {
      spoolman_btn.disable();
      spoolman_panel.foreground();
      spoolman_btn.enable();
    }
  }
}
